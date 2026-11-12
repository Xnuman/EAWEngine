#include "platform/crash_handler.hpp"

#include "core/fatal_error.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>

#include <windows.h>

namespace eaw {
namespace {

// Settings read by the handlers; written only by install_crash_handlers at startup.
std::string application_title{"EAWEngine"};
bool dialogs_enabled = true;

std::wstring to_wide(const std::string_view text)
{
    if (text.empty())
    {
        return {};
    }
    const auto size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    if (size > 0)
    {
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size);
    }
    return result;
}

// Dialogs need an interactive user: when stderr goes to a file or pipe (tests, CI, scripted runs),
// the stderr report is the only output.
bool stderr_redirected() noexcept
{
    const auto handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    const auto type = GetFileType(handle);
    return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
}

void show_report(const std::string_view report)
{
    if (!dialogs_enabled || stderr_redirected())
    {
        return;
    }
    const auto text = to_wide(std::string(report) + "\n\nRetry: break into the debugger (when one is attached).\nCancel: exit.");
    const auto title = to_wide(application_title + " - fatal error");
    const auto choice = MessageBoxW(nullptr, text.c_str(), title.c_str(), MB_RETRYCANCEL | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND | MB_TASKMODAL);
    if (choice == IDRETRY && IsDebuggerPresent())
    {
        __debugbreak();
    }
}

std::string hex(const std::uint64_t value)
{
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "0x%016llX", static_cast<unsigned long long>(value));
    return buffer.data();
}

// "module.dll+0x1234" for an address inside a loaded module.
std::string describe_address(const void* address)
{
    HMODULE module{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module) && module != nullptr)
    {
        std::array<wchar_t, MAX_PATH> path{};
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        std::wstring_view name(path.data(), length);
        if (const auto slash = name.find_last_of(L"\\/"); slash != std::wstring_view::npos)
        {
            name.remove_prefix(slash + 1U);
        }
        std::string narrow;
        for (const auto character : name)
        {
            narrow.push_back(character < 128 ? static_cast<char>(character) : '?');
        }
        return narrow + "+" + hex(reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module));
    }
    return hex(reinterpret_cast<std::uintptr_t>(address));
}

// EAWEngine is built without C++ exceptions, but code outside it (a driver or system DLL) may still
// raise one. MSVC raises C++ exceptions as structured exception 0xE06D7363 ("msc"). The record
// carries the exception object and its ThrowInfo, which lists every type a handler could match
// (most derived first). These layouts are the documented-by-practice x64 ABI used by the CRT; they are read
// defensively under SEH so a malformed record cannot fault inside the crash handler.
constexpr DWORD msvc_cpp_exception = 0xE06D7363;
constexpr ULONG_PTR msvc_exception_magic = 0x19930520;

struct ThisDisplacement
{
    int member;
    int virtual_base_pointer;
    int virtual_base_offset;
};
struct CatchableType
{
    unsigned int properties;
    int type_descriptor;
    ThisDisplacement displacement;
    int size_or_offset;
    int copy_function;
};
struct CatchableTypeArray
{
    int count;
    int types[1];
};
struct ThrowInfo
{
    unsigned int attributes;
    int unwind;
    int forward_compatibility;
    int catchable_types;
};
struct TypeDescriptor
{
    const void* vftable;
    void* spare;
    char name[1];
};

// Copies the exception object's decorated type name into the buffer. A plain C buffer only: this
// runs under __try, which cannot unwind C++ objects.
void read_cpp_exception(const EXCEPTION_RECORD& record, char* type_name, const std::size_t type_size) noexcept
{
    __try
    {
        if (record.NumberParameters < 4 || record.ExceptionInformation[0] != msvc_exception_magic)
        {
            return;
        }
        const auto* object = reinterpret_cast<const char*>(record.ExceptionInformation[1]);
        const auto* throw_info = reinterpret_cast<const ThrowInfo*>(record.ExceptionInformation[2]);
        const auto* image = reinterpret_cast<const char*>(record.ExceptionInformation[3]);
        if (object == nullptr || throw_info == nullptr || image == nullptr)
        {
            return;
        }
        const auto* types = reinterpret_cast<const CatchableTypeArray*>(image + throw_info->catchable_types);
        if (types->count > 0)
        {
            const auto* type = reinterpret_cast<const CatchableType*>(image + types->types[0]);
            const auto* descriptor = reinterpret_cast<const TypeDescriptor*>(image + type->type_descriptor);
            strncpy_s(type_name, type_size, descriptor->name, _TRUNCATE);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {}
}

// ".?AVruntime_error@std@@" -> "std::runtime_error" (classes and structs; other names unchanged).
std::string readable_type_name(std::string_view decorated)
{
    if (!(decorated.starts_with(".?AV") || decorated.starts_with(".?AU")) || !decorated.ends_with("@@"))
    {
        return std::string(decorated);
    }
    decorated.remove_prefix(4);
    decorated.remove_suffix(2);
    std::string result;
    while (!decorated.empty())
    {
        const auto at = decorated.rfind('@');
        const auto part = at == std::string_view::npos ? decorated : decorated.substr(at + 1U);
        result += (result.empty() ? "" : "::") + std::string(part);
        decorated = at == std::string_view::npos ? std::string_view{} : decorated.substr(0, at);
    }
    return result;
}

std::string describe_exception(const EXCEPTION_RECORD& record)
{
    if (record.ExceptionCode == msvc_cpp_exception)
    {
        std::array<char, 256> type_name{};
        read_cpp_exception(record, type_name.data(), type_name.size());
        std::string text = "Unhandled C++ exception";
        if (type_name[0] != '\0')
        {
            text += " (" + readable_type_name(type_name.data()) + ")";
        }
        // The record's address is the CRT's RaiseException; the raising code is on the call stack,
        // which a debugger shows after Retry (this filter runs before unwinding).
        return text + "\nRetry under a debugger to stop where it was raised.";
    }

    std::string text;
    switch (record.ExceptionCode)
    {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_IN_PAGE_ERROR:
    {
        text = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION ? "Access violation" : "In-page error";
        if (record.NumberParameters >= 2)
        {
            const auto operation = record.ExceptionInformation[0];
            text += operation == 0 ? " reading " : operation == 1 ? " writing " : " executing ";
            text += hex(record.ExceptionInformation[1]);
        }
        break;
    }
    case EXCEPTION_STACK_OVERFLOW:
        text = "Stack overflow";
        break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        text = "Integer division by zero";
        break;
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        text = "Illegal instruction";
        break;
    case EXCEPTION_PRIV_INSTRUCTION:
        text = "Privileged instruction";
        break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        text = "Array bounds exceeded";
        break;
    case EXCEPTION_BREAKPOINT:
        text = "Breakpoint without a debugger";
        break;
    case STATUS_HEAP_CORRUPTION:
        text = "Heap corruption";
        break;
    case STATUS_STACK_BUFFER_OVERRUN:
        text = "Stack buffer overrun (security check failure)";
        break;
    default:
        text = "Unhandled exception " + hex(record.ExceptionCode);
        break;
    }
    text += "\nAt: " + describe_address(record.ExceptionAddress);
    return text;
}

LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* pointers)
{
    if (pointers == nullptr || pointers->ExceptionRecord == nullptr)
    {
        fatal_error_report("Unhandled structured exception (no exception record)");
    }
    fatal_error_report(describe_exception(*pointers->ExceptionRecord));
}

void terminate_handler()
{
    fatal_error_report("std::terminate was called");
}

void purecall_handler()
{
    fatal_error_report("Pure virtual function call");
}

void invalid_parameter_handler(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    fatal_error_report("Invalid parameter passed to a C runtime function");
}

} // namespace

void install_crash_handlers(const CrashHandlerOptions& options)
{
    application_title = options.application_name;
    dialogs_enabled = options.dialogs;
    set_fatal_error_handler(show_report);
    std::set_terminate(terminate_handler);
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    _set_purecall_handler(purecall_handler);
    _set_invalid_parameter_handler(invalid_parameter_handler);
    // Leave room on this thread's stack so a stack overflow can still be reported.
    ULONG guarantee = 64U * 1024U;
    SetThreadStackGuarantee(&guarantee);
    // abort() reports through our handlers instead of the CRT's own message box.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

} // namespace eaw
