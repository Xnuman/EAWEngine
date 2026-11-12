// The DAT localization workspace.

#include "core/string_utils.hpp"
#include "tools_gui/gui_internal.hpp"

namespace eaw::tools {
namespace {

bool status_matches(const std::string& filter, const LocalizationRow& row)
{
    return filter == "all" || ascii_lower(row.status) == filter;
}

int compare_case_insensitive(const std::string_view left, const std::string_view right)
{
    const auto count = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto left_folded = static_cast<unsigned char>(ascii_lower(left[index]));
        const auto right_folded = static_cast<unsigned char>(ascii_lower(right[index]));
        if (left_folded != right_folded)
        {
            return left_folded < right_folded ? -1 : 1;
        }
    }
    if (left.size() == right.size())
    {
        return 0;
    }
    return left.size() < right.size() ? -1 : 1;
}

std::string_view localization_sort_value(const LocalizationRow& row, const ImGuiID column)
{
    switch (column)
    {
    case 1:
        return row.key;
    case 2:
        return row.text;
    case 3:
        return row.group;
    case 4:
        return row.status;
    default:
        return row.key;
    }
}

void sort_localization_rows(std::vector<int>& visible, const std::vector<LocalizationRow>& rows, const ImGuiTableSortSpecs& specifications)
{
    std::ranges::stable_sort(visible,
                             [&](const int left_index, const int right_index)
                             {
                                 const auto& left = rows[static_cast<std::size_t>(left_index)];
                                 const auto& right = rows[static_cast<std::size_t>(right_index)];
                                 for (int sort_index = 0; sort_index < specifications.SpecsCount; ++sort_index)
                                 {
                                     const auto& specification = specifications.Specs[sort_index];
                                     const auto comparison =
                                         compare_case_insensitive(localization_sort_value(left, specification.ColumnUserID), localization_sort_value(right, specification.ColumnUserID));
                                     if (comparison == 0)
                                     {
                                         continue;
                                     }
                                     return specification.SortDirection == ImGuiSortDirection_Ascending ? comparison < 0 : comparison > 0;
                                 }
                                 return left_index < right_index;
                             });
}

void render_add_key_popup(GuiApp::State& state)
{
    if (ImGui::BeginPopupModal("Add localization key", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(muted, "Use the same TEXT_* identifier referenced by mod XML.");
        ImGui::SetNextItemWidth(480.0F);
        ImGui::InputTextWithHint("##new-key", "TEXT_MY_NEW_KEY", &state.new_key);
        if (ImGui::Button("Add", {110.0F, 0.0F}))
        {
            if (const auto status = state.add_localization_row({state.new_key, {}, "manual", "todo", {}, "Added in EAWTools"}); !status)
            {
                state.fail(status.error());
            }
            else
            {
                state.new_key.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void render_add_unit_popup(GuiApp::State& state)
{
    if (ImGui::BeginPopupModal("Add unit localization", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextColored(muted, "Creates a display name and encyclopedia description together.");
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint("##unit-id", "Unit ID, e.g. REPUBLIC_VENATOR", &state.unit_id);
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextWithHint("##unit-name", "Display name", &state.unit_name);
        ImGui::SetNextItemWidth(520.0F);
        ImGui::InputTextMultiline("##unit-description", &state.unit_description, {520.0F, 130.0F});
        if (ImGui::Button("Create keys", {130.0F, 0.0F}))
        {
            const auto id = normalize_unit_id(state.unit_id);
            const auto additions = unit_localization_rows(id, state.unit_name, state.unit_description);
            const auto conflicts = [&](const auto& row)
            {
                const auto key = ascii_lower(row.key);
                return key == ascii_lower(additions[0].key) || key == ascii_lower(additions[1].key);
            };
            if (id.empty())
            {
                state.fail(make_error("Unit ID cannot be empty"));
            }
            else if (std::ranges::any_of(state.rows, conflicts))
            {
                state.fail(make_error("One of the generated unit keys already exists"));
            }
            else
            {
                state.rows.insert(state.rows.end(), additions.begin(), additions.end());
                state.selected_row = static_cast<int>(state.rows.size() - 2);
                state.dirty = true;
                state.unit_id.clear();
                state.unit_name.clear();
                state.unit_description.clear();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {110.0F, 0.0F}))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace

void render_localization_workspace(GuiApp::State& state, HWND owner)
{
    ImGui::TextUnformatted("DAT LOCALIZATION STUDIO");
    ImGui::SameLine();
    ImGui::TextColored(muted, "Edit, scan, validate, and build mod text");
    ImGui::Spacing();

    if (ImGui::Button("Open DAT..."))
    {
        if (const auto path = open_dat_file(owner, L"Open baseline localization DAT"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.open_dat(**path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Open workspace..."))
    {
        if (const auto path = open_workspace_file(owner, L"Open localization workspace"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.open_workspace(**path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save workspace"))
    {
        auto path = state.workspace_path;
        if (!path)
        {
            if (auto chosen = save_workspace_file(owner, L"Save localization workspace"); !chosen)
            {
                state.fail(chosen.error());
            }
            else
            {
                path = std::move(chosen).value();
            }
        }
        if (path)
        {
            state.save_workspace(*path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save as..."))
    {
        if (const auto path = save_workspace_file(owner, L"Save localization workspace"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.save_workspace(**path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Scan mod..."))
    {
        if (const auto path = pick_folder(owner, L"Select mod root to scan"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.scan_mod(**path);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add key"))
    {
        ImGui::OpenPopup("Add localization key");
    }
    ImGui::SameLine();
    if (ImGui::Button("Add unit"))
    {
        ImGui::OpenPopup("Add unit localization");
    }
    ImGui::SameLine();
    if (ImGui::Button("Validate"))
    {
        state.validate_workspace();
    }
    ImGui::SameLine();
    if (ImGui::Button("Build DAT..."))
    {
        if (const auto path = save_dat_string_table(owner, L"Build localization DAT"); !path)
        {
            state.fail(path.error());
        }
        else if (*path)
        {
            state.build_dat(**path);
        }
    }

    render_add_key_popup(state);
    render_add_unit_popup(state);

    if (state.base_dat_path)
    {
        label_value("Baseline", path_to_utf8(*state.base_dat_path));
    }
    else
    {
        label_value("Baseline", "Not selected (required for scan/build)");
    }
    if (state.workspace_path)
    {
        label_value("Workspace", path_to_utf8(*state.workspace_path));
    }
    ImGui::Checkbox("Skip unresolved TODO rows when building", &state.skip_todo);
    if (state.dirty)
    {
        ImGui::SameLine();
        ImGui::TextColored(warning, "Unsaved changes");
    }

    ImGui::SetNextItemWidth(-510.0F);
    ImGui::InputTextWithHint("##row-search", "Search keys, text, group, or context...", &state.row_query);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0F);
    if (ImGui::BeginCombo("##status-filter", state.status_filter.c_str()))
    {
        for (const char* status : {"all", "existing", "new", "modified", "ready", "todo", "deleted"})
        {
            const bool selected = state.status_filter == status;
            if (ImGui::Selectable(status, selected))
            {
                state.status_filter = status;
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextColored(accent, "%zu rows", state.rows.size());

    std::vector<int> visible;
    visible.reserve(state.rows.size());
    const auto query = ascii_lower(state.row_query);
    for (std::size_t index = 0; index < state.rows.size(); ++index)
    {
        const auto& row = state.rows[index];
        if (!status_matches(state.status_filter, row))
        {
            continue;
        }
        if (!query.empty())
        {
            const auto searchable = ascii_lower(row.key + "\n" + row.text + "\n" + row.group + "\n" + row.context);
            if (searchable.find(query) == std::string::npos)
            {
                continue;
            }
        }
        visible.push_back(static_cast<int>(index));
    }

    if (ImGui::BeginTable("localization-layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, {-1.0F, -1.0F}))
    {
        ImGui::TableSetupColumn("Entries", ImGuiTableColumnFlags_WidthStretch, 0.62F);
        ImGui::TableSetupColumn("Editor", ImGuiTableColumnFlags_WidthStretch, 0.38F);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (ImGui::BeginTable("localization-table", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable,
                              {-1.0F, -1.0F}))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortAscending, 0.44F, 1);
            ImGui::TableSetupColumn("Text", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending, 0.36F, 2);
            ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_PreferSortAscending, 0.20F, 3);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_PreferSortAscending, 70.0F, 4);
            if (auto* sort_specs = ImGui::TableGetSortSpecs(); sort_specs != nullptr && sort_specs->SpecsCount > 0)
            {
                sort_localization_rows(visible, state.rows, *sort_specs);
                sort_specs->SpecsDirty = false;
            }
            ImGui::TableHeadersRow();
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible.size()));
            while (clipper.Step())
            {
                for (int table_row = clipper.DisplayStart; table_row < clipper.DisplayEnd; ++table_row)
                {
                    const int index = visible[static_cast<std::size_t>(table_row)];
                    const auto& row = state.rows[static_cast<std::size_t>(index)];
                    ImGui::PushID(index);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(row.key.c_str(), state.selected_row == index, ImGuiSelectableFlags_SpanAllColumns))
                    {
                        state.selected_row = index;
                    }
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(clean_one_line(row.text, 72).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.group.c_str());
                    ImGui::TableSetColumnIndex(3);
                    const auto color = ascii_lower(row.status) == "todo" ? warning : (ascii_lower(row.status) == "deleted" ? danger : accent);
                    ImGui::TextColored(color, "%s", row.status.c_str());
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::BeginChild("row-editor", {-1.0F, -1.0F}, ImGuiChildFlags_Borders);
        if (state.selected_row >= 0 && static_cast<std::size_t>(state.selected_row) < state.rows.size())
        {
            auto& row = state.rows[static_cast<std::size_t>(state.selected_row)];
            ImGui::TextColored(accent, "%s", row.key.c_str());
            ImGui::TextColored(muted, "Key");
            const bool existing = ascii_lower(row.status) == "existing";
            if (existing)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::InputText("##edit-key", &row.key))
            {
                state.dirty = true;
            }
            if (existing)
            {
                ImGui::EndDisabled();
            }
            ImGui::TextColored(muted, "Localized text");
            if (ImGui::InputTextMultiline("##edit-text", &row.text, {-1.0F, 150.0F}))
            {
                const auto current_status = ascii_lower(row.status);
                if (current_status == "existing")
                {
                    row.status = "modified";
                }
                else if (current_status == "todo" && !row.text.empty())
                {
                    row.status = "ready";
                }
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Group");
            if (ImGui::InputText("##edit-group", &row.group))
            {
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Status");
            if (ImGui::BeginCombo("##edit-status", row.status.c_str()))
            {
                for (const char* status : {"existing", "new", "modified", "ready", "todo", "deleted"})
                {
                    if (ImGui::Selectable(status, row.status == status))
                    {
                        row.status = status;
                        state.dirty = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::TextColored(muted, "Context");
            if (ImGui::InputTextMultiline("##edit-context", &row.context, {-1.0F, 74.0F}))
            {
                state.dirty = true;
            }
            ImGui::TextColored(muted, "Notes");
            if (ImGui::InputTextMultiline("##edit-notes", &row.notes, {-1.0F, 74.0F}))
            {
                state.dirty = true;
            }
            if (ImGui::Button("Delete entry"))
            {
                if (ascii_lower(row.status) == "new" || ascii_lower(row.status) == "todo")
                {
                    state.rows.erase(state.rows.begin() + state.selected_row);
                    state.selected_row = state.rows.empty() ? -1 : std::min(state.selected_row, static_cast<int>(state.rows.size() - 1));
                }
                else
                {
                    row.status = "deleted";
                }
                state.dirty = true;
            }
        }
        else
        {
            ImGui::TextColored(muted, "Select a localization row to edit it.");
        }
        if (!state.validation_messages.empty())
        {
            ImGui::SeparatorText("Validation");
            for (const auto& item : state.validation_messages)
            {
                ImGui::TextColored(item.starts_with("Error:") ? danger : item.starts_with("TODO:") ? warning : muted, "%s", item.c_str());
            }
        }
        ImGui::EndChild();
        ImGui::EndTable();
    }
}

} // namespace eaw::tools
