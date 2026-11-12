#include "localization/localization_workspace.hpp"

#include "core/string_utils.hpp"
#include "formats/dat_string_table_helpers.hpp"

#include <algorithm>
#include <array>
#include <tuple>
#include <unordered_map>

namespace eaw {
namespace {

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

} // namespace

Result<std::vector<LocalizationRow>> read_localization_workspace(const std::filesystem::path& path)
{
    EAW_TRY_ASSIGN(const auto text, read_text_file(path));
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
        columns.emplace(ascii_lower(records.front()[index]), index);
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
    std::string text = "\xef\xbb\xbf"
                       "Key\tText\tGroup\tStatus\tContext\tNotes\r\n";
    for (const auto& row : rows)
    {
        text += quote_tsv(row.key) + '\t' + quote_tsv(row.text) + '\t' + quote_tsv(row.group) + '\t' + quote_tsv(row.status) + '\t' + quote_tsv(row.context) + '\t' + quote_tsv(row.notes) + "\r\n";
    }
    auto written = write_text_file(path, text, overwrite ? WriteMode::replace : WriteMode::create_only);
    if (!written)
    {
        return with_context("localization workspace", std::move(written).error());
    }
    return Status::success();
}

Result<std::vector<LocalizationRow>> diff_localization(const DatStringTableData& base, const DatStringTableData& edited)
{
    std::vector<LocalizationRow> rows;
    for (const auto& entry : edited.entries)
    {
        const auto* old = dat_find(base, entry.key);
        if (old == nullptr || old->value != entry.value)
        {
            auto text = utf16_to_utf8(entry.value);
            if (!text)
            {
                return make_error("DAT entry " + entry.key + " is not valid UTF-16");
            }
            rows.push_back({entry.key, std::move(*text), {}, old == nullptr ? "new" : "modified", {}, {}});
        }
    }
    for (const auto& entry : base.entries)
    {
        if (dat_find(edited, entry.key) == nullptr)
        {
            rows.push_back({entry.key, {}, {}, "deleted", {}, {}});
        }
    }
    std::ranges::sort(rows, {}, &LocalizationRow::key);
    return rows;
}

Status apply_localization_workspace(DatStringTableData& target, const std::vector<LocalizationRow>& rows, const bool allow_todo)
{
    for (const auto& row : rows)
    {
        const auto status = ascii_lower(row.status);
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
            static_cast<void>(dat_erase(target, row.key));
        }
        else
        {
            auto text = utf8_to_utf16(row.text);
            if (!text)
            {
                return make_error("localization entry " + row.key + " is not valid UTF-8");
            }
            EAW_TRY(dat_upsert(target, row.key, std::move(*text)));
        }
    }
    return Status::success();
}

Result<std::vector<LocalizationRow>> workspace_rows_from_dat(const DatStringTableData& dat)
{
    std::vector<LocalizationRow> rows;
    rows.reserve(dat.entries.size());
    for (const auto& entry : dat.entries)
    {
        auto text = utf16_to_utf8(entry.value);
        if (!text)
        {
            return make_error("DAT entry " + entry.key + " is not valid UTF-16");
        }
        rows.push_back({entry.key, std::move(*text), {}, "existing", {}, {}});
    }
    std::ranges::sort(rows, {}, &LocalizationRow::key);
    return rows;
}

void sort_localization_rows(std::vector<LocalizationRow>& rows)
{
    std::ranges::sort(rows,
                      [](const LocalizationRow& left, const LocalizationRow& right)
                      {
                          return std::tie(left.group, left.key) < std::tie(right.group, right.key);
                      });
}

std::string normalize_unit_id(const std::string_view id)
{
    std::string result;
    result.reserve(id.size());
    for (const char character : id)
    {
        const auto byte = static_cast<unsigned char>(character);
        const bool keep = (byte >= '0' && byte <= '9') || (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
        const char normalized = keep ? ascii_upper(character) : '_';
        if (normalized == '_' && (result.empty() || result.back() == '_'))
        {
            continue;
        }
        result.push_back(normalized);
    }
    if (!result.empty() && result.back() == '_')
    {
        result.pop_back();
    }
    return result;
}

std::array<LocalizationRow, 2> unit_localization_rows(const std::string& id, std::string name, std::string description)
{
    return {LocalizationRow{"TEXT_UNIT_" + id, std::move(name), "unit:" + id, "ready", "Use in <Text_ID>", "Generated unit display-name key"},
            LocalizationRow{"TEXT_TOOLTIP_" + id, std::move(description), "unit:" + id, "ready", "Use in <Encyclopedia_Text>", "Generated unit description key"}};
}

Result<DatStringTableData> load_dat_string_table(const std::filesystem::path& path)
{
    EAW_TRY_ASSIGN(const auto bytes, read_file(path));
    auto dat = read_dat(bytes);
    if (!dat)
    {
        return with_context(path.string(), std::move(dat).error());
    }
    return dat;
}

Status save_dat_string_table(const std::filesystem::path& path, const DatStringTableData& dat, const WriteMode mode)
{
    EAW_TRY_ASSIGN(const auto bytes, write_dat(dat));
    return write_file(path, bytes, mode);
}

} // namespace eaw
