#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "core/status.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace eaw::tools {

[[nodiscard]] Result<std::optional<std::filesystem::path>> open_dat_file(HWND owner, std::wstring_view title);
[[nodiscard]] Result<std::optional<std::filesystem::path>> open_workspace_file(HWND owner, std::wstring_view title);
[[nodiscard]] Result<std::optional<std::filesystem::path>> save_dat_file(HWND owner, std::wstring_view title, std::wstring_view suggested_name = L"MasterTextFile_ENGLISH.dat");
[[nodiscard]] Result<std::optional<std::filesystem::path>> save_workspace_file(HWND owner, std::wstring_view title, std::wstring_view suggested_name = L"localization.tsv");
[[nodiscard]] Result<std::optional<std::filesystem::path>> pick_folder(HWND owner, std::wstring_view title);

// Dialogs return nothing when cancelled and an error when the dialog itself fails.

// Lossy for display: invalid UTF-16 (unpaired surrogates) becomes U+FFFD.
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] Result<std::filesystem::path> utf8_to_path(std::string_view text);

} // namespace eaw::tools
