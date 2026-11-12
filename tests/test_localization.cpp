// Localization: workspaces (TSV round trips, applying rows to a DAT), TEXT_* references in mod XML,
// and translation checks.
#include "test_support.hpp"

#include "formats/dat_string_table_helpers.hpp"
#include "localization/localization_validation.hpp"

namespace {

using namespace eaw_test;

// The TSV workspace round-trips text with tabs, quotes, and new lines; applying it fills completed
// TODO rows and rejects (or, when asked, skips) empty ones.
void test_localization_workspace()
{
    const auto original = make_test_dat();
    const auto workspace = std::filesystem::temp_directory_path() / "eawengine-localization-test.tsv";
    std::error_code ignored;
    std::filesystem::remove(workspace, ignored);
    const std::vector<eaw::LocalizationRow> rows{{"TEXT_UNIT_NEW", "Name\twith tab", "unit:new", "ready", "Data/XML/Test.xml:2", "A \"quoted\" note\nwith a new line"}};
    expect_ok(eaw::write_localization_workspace(workspace, rows), "writing the workspace");
    const auto loaded = expect_ok(eaw::read_localization_workspace(workspace), "reading the workspace");
    require(loaded.size() == 1 && loaded[0].text == rows[0].text && loaded[0].notes == rows[0].notes, "localization TSV round trip failed");

    auto resolved_todo = expect_ok(eaw::read_dat(original), "DAT parse");
    expect_ok(eaw::apply_localization_workspace(resolved_todo, {{"TEXT_UNIT_TEST", "Completed TODO value", "unit:test", "todo", {}, {}}}), "applying a completed TODO row");
    require(eaw::utf16_to_utf8(dat_find(resolved_todo, "TEXT_UNIT_TEST")->value) == "Completed TODO value", "completed TODO localization value was skipped");

    auto unresolved_todo = expect_ok(eaw::read_dat(original), "DAT parse");
    const auto unresolved = eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}});
    require(!unresolved.ok() && unresolved.error().message.find("unresolved TODO localization entry: TEXT_UNIT_EMPTY") != std::string::npos, "empty TODO localization row was not rejected");
    expect_ok(eaw::apply_localization_workspace(unresolved_todo, {{"TEXT_UNIT_EMPTY", {}, "unit:empty", "todo", {}, {}}}, true), "skipping an empty TODO row");
    require(dat_find(unresolved_todo, "TEXT_UNIT_EMPTY") == nullptr, "empty TODO localization row was not skipped");
    std::filesystem::remove(workspace, ignored);
}

// A scan of mod XML finds TEXT_* references outside comments and names missing keys with their file
// and line.
void test_localization_scan()
{
    const auto root = std::filesystem::temp_directory_path() / "eawengine-localization-scan-test";
    const auto xml_root = root / "Data" / "XML";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(xml_root);
    {
        std::ofstream xml(xml_root / "Units.xml", std::ios::binary);
        xml << "<Units>\n"
               "  <Text_ID>TEXT_UNIT_TEST</Text_ID>\n"
               "  <Text_ID>TEXT_UNIT_MISSING</Text_ID>\n"
               "  <!-- TEXT_COMMENTED_OUT -->\n"
               "</Units>\n";
    }

    const auto baseline = expect_ok(eaw::read_dat(make_test_dat()), "DAT parse");
    const auto result = expect_ok(eaw::scan_mod_localization(root, baseline), "localization scan");
    require(result.reference_count == 2, "localization scan reference count failed");
    require(result.missing.size() == 1 && result.missing.front().key == "TEXT_UNIT_MISSING" && result.missing.front().context.find("Units.xml:3") != std::string::npos,
            "localization scan missing-key context failed");
    std::filesystem::remove_all(root, ignored);
}

// Translation checks against a reference DAT, and the workspace helpers the DAT editor and EAWTools
// share (rows from a DAT, unit ids, new-unit rows, workspace order).
void test_localization_validation()
{
    const auto tokens = [](const std::u16string_view text)
    {
        return expect_ok(eaw::localization_formatting_tokens(text), "formatting tokens");
    };
    require(tokens(u"%d of %1$s, 100%% and %-5.2f %*d") == std::multiset<std::string>{"%d", "%1$s", "%%", "%-5.2f", "%*d"}, "printf tokens failed");
    require(tokens(u"Line\\nnext [PLAYER] <b> {0} [] <\n> 5%.") == std::multiset<std::string>{"\\n", "[PLAYER]", "<b>", "{0}"}, "escape and placeholder tokens failed");

    auto reference = expect_ok(eaw::read_dat(make_test_dat()), "DAT parse");
    auto translation = reference;
    require(dat_upsert(reference, "TEXT_COUNT", u"%d units").ok() && dat_upsert(translation, "TEXT_COUNT", u"unités: %d").ok(), "upsert failed");
    require(dat_upsert(reference, "TEXT_NAME", u"Hello [NAME]").ok() && dat_upsert(translation, "TEXT_NAME", u"Bonjour [NOM]").ok(), "upsert failed");
    require(dat_upsert(reference, "TEXT_SPACE", u" Padded").ok() && dat_upsert(translation, "TEXT_SPACE", u"Padded").ok(), "upsert failed");
    require(dat_upsert(reference, "TEXT_ONLY_REFERENCE", u"x").ok() && dat_upsert(translation, "TEXT_ONLY_TRANSLATION", u"y").ok(), "upsert failed");
    const auto issues = expect_ok(eaw::compare_translation(translation, reference), "compare translation");
    const auto has = [&](const std::string_view key, const eaw::DatIssueSeverity severity, const std::string_view message)
    {
        return std::ranges::any_of(issues,
                                   [&](const eaw::TranslationIssue& issue)
                                   {
                                       return issue.key == key && issue.severity == severity && issue.message.find(message) != std::string::npos;
                                   });
    };
    require(issues.size() == 4 && has("TEXT_NAME", eaw::DatIssueSeverity::error, "formatting tokens") && has("TEXT_SPACE", eaw::DatIssueSeverity::warning, "whitespace") &&
                has("TEXT_ONLY_REFERENCE", eaw::DatIssueSeverity::error, "missing") && has("TEXT_ONLY_TRANSLATION", eaw::DatIssueSeverity::warning, "not present"),
            "translation comparison failed");

    const auto rows = expect_ok(eaw::workspace_rows_from_dat(reference), "workspace rows");
    require(rows.size() == reference.entries.size() && std::ranges::is_sorted(rows, {}, &eaw::LocalizationRow::key) &&
                std::ranges::all_of(rows,
                                    [](const eaw::LocalizationRow& row)
                                    {
                                        return row.status == "existing";
                                    }),
            "workspace rows from a DAT failed");

    require(eaw::normalize_unit_id(" republic  venator-2 ") == "REPUBLIC_VENATOR_2" && eaw::normalize_unit_id("__x__") == "X" && eaw::normalize_unit_id("--").empty(), "unit id normalization failed");
    const auto unit = eaw::unit_localization_rows("VENATOR", "Venator", "A star destroyer.");
    require(unit[0].key == "TEXT_UNIT_VENATOR" && unit[0].text == "Venator" && unit[1].key == "TEXT_TOOLTIP_VENATOR" && unit[1].group == "unit:VENATOR" && unit[1].status == "ready",
            "new-unit rows failed");
    std::vector<eaw::LocalizationRow> ordered{{"B", {}, "g2", {}, {}, {}}, {"C", {}, "g1", {}, {}, {}}, {"A", {}, "g2", {}, {}, {}}};
    eaw::sort_localization_rows(ordered);
    require(ordered[0].key == "C" && ordered[1].key == "A" && ordered[2].key == "B", "workspace order failed");
}

const ModuleRegistrar registrar("localization", {
                                                    {"test_localization_workspace", &test_localization_workspace},
                                                    {"test_localization_scan", &test_localization_scan},
                                                    {"test_localization_validation", &test_localization_validation},
                                                });

} // namespace
