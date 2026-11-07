#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <optional>
#include <string_view>

namespace eaw::tools {

[[nodiscard]] std::optional<std::filesystem::path> open_dat_file(
    HWND owner,
    std::wstring_view title);
[[nodiscard]] std::optional<std::filesystem::path> open_workspace_file(
    HWND owner,
    std::wstring_view title);
[[nodiscard]] std::optional<std::filesystem::path> save_dat_file(
    HWND owner,
    std::wstring_view title,
    std::wstring_view suggested_name = L"MasterTextFile_ENGLISH.dat");
[[nodiscard]] std::optional<std::filesystem::path> save_workspace_file(
    HWND owner,
    std::wstring_view title,
    std::wstring_view suggested_name = L"localization.tsv");
[[nodiscard]] std::optional<std::filesystem::path> pick_folder(
    HWND owner,
    std::wstring_view title);

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path utf8_to_path(std::string_view text);

} // namespace eaw::tools

