#include "eaw/localization_workspace.hpp"

#include "eaw/text_encoding.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace eaw {
namespace {

std::string lower(std::string value)
{
    std::ranges::transform(value, value.begin(),
                           [](const unsigned char character)
                           {
                               return static_cast<char>(std::tolower(character));
                           });
    return value;
}

Result<std::vector<std::vector<std::string>>> parse_tsv(const std::string& text)
{
    std::string input = text;
    if (input.empty() || input.back() != '\n')
    {
        input.push_back('\n');
    }
    std::vector<std::vector<std::string>> records;
    std::vector<std::string> record;
    std::string field;
    bool quoted = false;
    bool after_quote = false;
    std::size_t index = input.starts_with("\xef\xbb\xbf") ? 3 : 0;
    while (index < input.size())
    {
        const char character = input[index++];
        if (quoted)
        {
            if (character == '"')
            {
                if (index < input.size() && input[index] == '"')
                {
                    field.push_back('"');
                    ++index;
                }
                else
                {
                    quoted = false;
                    after_quote = true;
                }
            }
            else
            {
                field.push_back(character);
            }
            continue;
        }
        if (after_quote && character != '\t' && character != '\r' && character != '\n')
        {
            return make_error("unexpected character after a quoted TSV field");
        }
        if (character == '"' && field.empty() && !after_quote)
        {
            quoted = true;
        }
        else if (character == '\t')
        {
            record.push_back(std::move(field));
            field.clear();
            after_quote = false;
        }
        else if (character == '\n')
        {
            if (!record.empty() || !field.empty() || after_quote)
            {
                if (!field.empty() && field.back() == '\r' && !after_quote)
                {
                    field.pop_back();
                }
                record.push_back(std::move(field));
                records.push_back(std::move(record));
                field.clear();
                record.clear();
            }
            after_quote = false;
        }
        else if (character != '\r')
        {
            field.push_back(character);
        }
    }
    if (quoted)
    {
        return make_error("unterminated quoted TSV field");
    }
    return records;
}

std::string quote_tsv(const std::string_view value)
{
    const bool requires_quotes = value.find_first_of("\t\r\n\"") != std::string_view::npos;
    if (!requires_quotes)
    {
        return std::string(value);
    }
    std::string result{"\""};
    for (const char character : value)
    {
        if (character == '"')
        {
            result += "\"\"";
        }
        else
        {
            result.push_back(character);
        }
    }
    result.push_back('"');
    return result;
}

Result<std::string> read_text(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
    {
        return make_error("could not open localization workspace: " + path.string());
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

} // namespace

Result<std::vector<LocalizationRow>> read_localization_workspace(const std::filesystem::path& path)
{
    EAW_TRY_ASSIGN(const auto text, read_text(path));
    auto parsed = parse_tsv(text);
    if (!parsed)
    {
        return with_context(path.string(), std::move(parsed).error());
    }
    const auto records = std::move(parsed).value();
    if (records.empty())
    {
        return make_error("localization workspace is empty");
    }

    std::unordered_map<std::string, std::size_t> columns;
    for (std::size_t index = 0; index < records.front().size(); ++index)
    {
        columns.emplace(lower(records.front()[index]), index);
    }
    if (!columns.contains("key") || !columns.contains("text"))
    {
        return make_error("workspace requires Key and Text columns");
    }

    const auto field = [&](const std::vector<std::string>& record, const std::string_view name) -> std::string
    {
        const auto found = columns.find(std::string(name));
        return found != columns.end() && found->second < record.size() ? record[found->second] : std::string{};
    };

    std::vector<LocalizationRow> rows;
    rows.reserve(records.size() - 1);
    for (std::size_t index = 1; index < records.size(); ++index)
    {
        if (records[index].empty() || field(records[index], "key").empty())
        {
            continue;
        }
        rows.push_back({field(records[index], "key"), field(records[index], "text"), field(records[index], "group"), field(records[index], "status"), field(records[index], "context"),
                        field(records[index], "notes")});
    }
    return rows;
}

Status write_localization_workspace(const std::filesystem::path& path, const std::vector<LocalizationRow>& rows, const bool overwrite)
{
    std::error_code filesystem_error;
    if (std::filesystem::exists(path, filesystem_error) && !overwrite)
    {
        return make_error("workspace already exists; pass --force to replace it: " + path.string());
    }
    if (const auto parent = path.parent_path(); !parent.empty())
    {
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error)
        {
            return make_error("could not create localization workspace directory (" + filesystem_error.message() + "): " + parent.string());
        }
    }

    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream)
        {
            return make_error("could not create localization workspace: " + temporary.string());
        }
        stream << "\xef\xbb\xbf"
               << "Key\tText\tGroup\tStatus\tContext\tNotes\r\n";
        for (const auto& row : rows)
        {
            stream << quote_tsv(row.key) << '\t' << quote_tsv(row.text) << '\t' << quote_tsv(row.group) << '\t' << quote_tsv(row.status) << '\t' << quote_tsv(row.context) << '\t'
                   << quote_tsv(row.notes) << "\r\n";
        }
        stream.flush();
        if (!stream)
        {
            return make_error("could not write localization workspace: " + temporary.string());
        }
    }
#ifdef _WIN32
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0U);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags))
    {
        const auto error = GetLastError();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return make_error("could not commit localization workspace (Windows error " + std::to_string(error) + "): " + path.string());
    }
#else
    std::filesystem::rename(temporary, path, filesystem_error);
    if (filesystem_error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return make_error("could not commit localization workspace (" + filesystem_error.message() + "): " + path.string());
    }
#endif
    return Status::success();
}

Result<std::vector<LocalizationRow>> diff_localization(const DatFile& base, const DatFile& edited)
{
    std::vector<LocalizationRow> rows;
    for (const auto& entry : edited.entries())
    {
        const auto* old = base.find(entry.key);
        if (old == nullptr || old->value != entry.value)
        {
            auto text = utf16_to_utf8(entry.value);
            if (!text)
            {
                return with_context("DAT entry " + entry.key, std::move(text).error());
            }
            rows.push_back({entry.key, std::move(text).value(), {}, old == nullptr ? "new" : "modified", {}, {}});
        }
    }
    for (const auto& entry : base.entries())
    {
        if (edited.find(entry.key) == nullptr)
        {
            rows.push_back({entry.key, {}, {}, "deleted", {}, {}});
        }
    }
    std::ranges::sort(rows, {}, &LocalizationRow::key);
    return rows;
}

Status apply_localization_workspace(DatFile& target, const std::vector<LocalizationRow>& rows, const bool allow_todo)
{
    for (const auto& row : rows)
    {
        const auto status = lower(row.status);
        const bool unresolved_todo = status == "todo" && row.text.empty();
        if (unresolved_todo && !allow_todo)
        {
            return make_error("unresolved TODO localization entry: " + row.key);
        }
        if (unresolved_todo)
        {
            continue;
        }
        if (status == "deleted" || status == "delete")
        {
            static_cast<void>(target.erase(row.key));
        }
        else
        {
            auto text = utf8_to_utf16(row.text);
            if (!text)
            {
                return with_context("localization entry " + row.key, std::move(text).error());
            }
            EAW_TRY(target.upsert(row.key, std::move(text).value()));
        }
    }
    return Status::success();
}

} // namespace eaw
