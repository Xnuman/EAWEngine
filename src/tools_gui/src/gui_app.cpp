#include "tools_gui/gui_internal.hpp"

namespace eaw::tools {
namespace {

void render_status_bar(GuiApp::State& state)
{
    ImGui::Separator();
    ImGui::TextColored(state.message_error ? danger : accent, "%s", state.message_error ? "!" : "●");
    ImGui::SameLine();
    ImGui::TextUnformatted(state.message.c_str());
}

} // namespace

GuiApp::GuiApp(ID3D11Device* device) : state_(std::make_unique<State>(device))
{}
GuiApp::~GuiApp() = default;

void GuiApp::render(const NativeWindowHandle window)
{
    auto& style = ImGui::GetStyle();
    style.WindowRounding = 8.0F;
    style.ChildRounding = 7.0F;
    style.FrameRounding = 5.0F;
    style.PopupRounding = 7.0F;
    style.ScrollbarRounding = 8.0F;
    style.GrabRounding = 5.0F;
    style.WindowPadding = {12.0F, 12.0F};
    style.FramePadding = {9.0F, 6.0F};
    style.ItemSpacing = {8.0F, 7.0F};
    style.Colors[ImGuiCol_WindowBg] = {0.040F, 0.055F, 0.075F, 1.0F};
    style.Colors[ImGuiCol_ChildBg] = {0.055F, 0.073F, 0.095F, 1.0F};
    style.Colors[ImGuiCol_FrameBg] = {0.085F, 0.11F, 0.14F, 1.0F};
    style.Colors[ImGuiCol_FrameBgHovered] = {0.12F, 0.18F, 0.21F, 1.0F};
    style.Colors[ImGuiCol_FrameBgActive] = {0.13F, 0.24F, 0.26F, 1.0F};
    style.Colors[ImGuiCol_Button] = {0.10F, 0.25F, 0.27F, 1.0F};
    style.Colors[ImGuiCol_ButtonHovered] = {0.13F, 0.38F, 0.40F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = {0.14F, 0.48F, 0.49F, 1.0F};
    style.Colors[ImGuiCol_Header] = {0.10F, 0.27F, 0.29F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = {0.13F, 0.38F, 0.40F, 1.0F};
    style.Colors[ImGuiCol_HeaderActive] = {0.14F, 0.47F, 0.48F, 1.0F};
    style.Colors[ImGuiCol_CheckMark] = accent;
    style.Colors[ImGuiCol_SliderGrab] = accent;
    style.Colors[ImGuiCol_SeparatorActive] = accent;
    style.Colors[ImGuiCol_TabSelected] = {0.10F, 0.31F, 0.33F, 1.0F};

    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("EAW Tools Workbench", nullptr, flags);

    ImGui::BeginChild("navigation", {205.0F, -35.0F}, ImGuiChildFlags_Borders);
    ImGui::TextColored(accent, "EAW TOOLS");
    ImGui::TextColored(muted, "Alamo workbench");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Selectable("Asset Inspector", state_->workspace == State::Workspace::assets, 0, {0.0F, 42.0F}))
    {
        state_->workspace = State::Workspace::assets;
    }
    if (ImGui::Selectable("DAT Localization", state_->workspace == State::Workspace::localization, 0, {0.0F, 42.0F}))
    {
        state_->workspace = State::Workspace::localization;
    }
    ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 68.0F));
    ImGui::TextColored(muted, "Native C++20");
    ImGui::TextColored(muted, "Dear ImGui • DirectX 11");
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("workspace", {0.0F, -35.0F}, ImGuiChildFlags_Borders);
    if (state_->workspace == State::Workspace::assets)
    {
        render_asset_workspace(*state_, window);
    }
    else
    {
        render_localization_workspace(*state_, window);
    }
    ImGui::EndChild();
    render_status_bar(*state_);
    ImGui::End();
    render_large_map_preview(*state_);
    render_large_model_preview(*state_);
}

} // namespace eaw::tools
