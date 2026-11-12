#include "imgui_app/file_dialogs.hpp"

#include <windows.h>
#include <shobjidl.h>

#include <array>
#include <string>

namespace eaw::gui {
namespace {

template <typename Dialog>
Result<std::optional<std::filesystem::path>> show_dialog(HWND owner, const CLSID& class_id, std::wstring_view title, FILEOPENDIALOGOPTIONS options, const COMDLG_FILTERSPEC* filters,
                                                         const UINT filter_count, std::wstring_view default_extension, std::wstring_view suggested_name)
{
    Dialog* dialog = nullptr;
    const auto created = CoCreateInstance(class_id, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (FAILED(created))
    {
        return make_error("Windows file dialog could not be created");
    }

    dialog->SetTitle(std::wstring(title).c_str());
    FILEOPENDIALOGOPTIONS existing{};
    dialog->GetOptions(&existing);
    dialog->SetOptions(existing | options | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR);

    if (filters != nullptr && filter_count != 0)
    {
        dialog->SetFileTypes(filter_count, filters);
    }
    if (!default_extension.empty())
    {
        dialog->SetDefaultExtension(std::wstring(default_extension).c_str());
    }
    if (!suggested_name.empty())
    {
        dialog->SetFileName(std::wstring(suggested_name).c_str());
    }

    const auto shown = dialog->Show(owner);
    if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        dialog->Release();
        return std::optional<std::filesystem::path>{};
    }
    if (FAILED(shown))
    {
        dialog->Release();
        return make_error("Windows file dialog failed");
    }

    IShellItem* result = nullptr;
    if (FAILED(dialog->GetResult(&result)))
    {
        dialog->Release();
        return make_error("Windows file dialog returned no selection");
    }

    PWSTR raw_path = nullptr;

    const auto got_path = result->GetDisplayName(SIGDN_FILESYSPATH, &raw_path);

    std::optional<std::filesystem::path> path;

    if (SUCCEEDED(got_path) && raw_path != nullptr)
    {
        path = std::filesystem::path(raw_path);
    }

    CoTaskMemFree(raw_path);
    result->Release();
    dialog->Release();

    return path;
}

} // namespace

Result<std::optional<std::filesystem::path>> open_dat_file(HWND owner, const std::wstring_view title)
{
    static constexpr std::array filters{COMDLG_FILTERSPEC{L"Petroglyph string files (*.dat)", L"*.dat"}, COMDLG_FILTERSPEC{L"All files (*.*)", L"*.*"}};

    return show_dialog<IFileOpenDialog>(owner, CLSID_FileOpenDialog, title, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST, filters.data(), static_cast<UINT>(filters.size()), L"dat", {});
}

Result<std::optional<std::filesystem::path>> open_workspace_file(HWND owner, const std::wstring_view title)
{
    static constexpr std::array filters{COMDLG_FILTERSPEC{L"Localization workspaces (*.tsv)", L"*.tsv"}, COMDLG_FILTERSPEC{L"All files (*.*)", L"*.*"}};

    return show_dialog<IFileOpenDialog>(owner, CLSID_FileOpenDialog, title, FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST, filters.data(), static_cast<UINT>(filters.size()), L"tsv", {});
}

Result<std::optional<std::filesystem::path>> save_dat_string_table(HWND owner, const std::wstring_view title, const std::wstring_view suggested_name)
{
    static constexpr std::array filters{COMDLG_FILTERSPEC{L"Petroglyph string files (*.dat)", L"*.dat"}, COMDLG_FILTERSPEC{L"All files (*.*)", L"*.*"}};

    return show_dialog<IFileSaveDialog>(owner, CLSID_FileSaveDialog, title, FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST, filters.data(), static_cast<UINT>(filters.size()), L"dat", suggested_name);
}

Result<std::optional<std::filesystem::path>> save_workspace_file(HWND owner, const std::wstring_view title, const std::wstring_view suggested_name)
{
    static constexpr std::array filters{COMDLG_FILTERSPEC{L"Localization workspaces (*.tsv)", L"*.tsv"}, COMDLG_FILTERSPEC{L"All files (*.*)", L"*.*"}};

    return show_dialog<IFileSaveDialog>(owner, CLSID_FileSaveDialog, title, FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST, filters.data(), static_cast<UINT>(filters.size()), L"tsv", suggested_name);
}

Result<std::optional<std::filesystem::path>> pick_folder(HWND owner, const std::wstring_view title)
{
    return show_dialog<IFileOpenDialog>(owner, CLSID_FileOpenDialog, title, FOS_PICKFOLDERS | FOS_PATHMUSTEXIST, nullptr, 0, {}, {});
}

std::string path_to_utf8(const std::filesystem::path& path)
{
    const auto& wide = path.native();

    if (wide.empty())
    {
        return {};
    }

    // Without WC_ERR_INVALID_CHARS, unpaired surrogates become U+FFFD instead of failing.
    const auto size = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);

    if (size <= 0)
    {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');

    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), result.data(), size, nullptr, nullptr);

    return result;
}

Result<std::filesystem::path> utf8_to_path(const std::string_view text)
{
    if (text.empty())
    {
        return std::filesystem::path{};
    }
    const auto size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

    if (size <= 0)
    {
        return make_error("path is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');

    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), size);

    return std::filesystem::path(std::move(result));
}

} // namespace eaw::gui
