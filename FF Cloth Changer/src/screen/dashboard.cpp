#include "common/dashboard_common.h"
#include "common/login_common.h"

#include <cstdio>
#include <string>

// Single-screen shell: title bar with logo, session pill, window buttons; the
// wardrobe page fills the rest. No sidebar, no page switcher.

namespace screen_dashboard {

namespace {

using namespace ui;
using namespace dash;

void draw_background()
{
	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, color_u32(kHomeBg));

	if (!shared.background_glow || shared.reduced_effects)
		return;

	draw->AddCircleFilled(ImVec2(display.x - 40.0f, 20.0f), 320.0f,
		color_u32(fade(kPurple, state.light ? 0.04f : 0.05f)), 64);
	draw->AddCircleFilled(ImVec2(180.0f, display.y + 60.0f), 260.0f,
		color_u32(fade(kBlue, state.light ? 0.03f : 0.04f)), 64);
}

void draw_title_bar()
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(app::kWidth, kTitleHeight), color_u32(kSidebar));
	draw->AddLine(ImVec2(0.0f, kTitleHeight - 1.0f), ImVec2(app::kWidth, kTitleHeight - 1.0f),
		color_u32(kLine), 1.0f);

	// Logo + wordmark on the left of the title.
	login_ui::DrawLogo(ImVec2(32.0f, kTitleHeight * 0.5f), 36.0f);
	gradient_text(font.semibold, 15.0f, ImVec2(60.0f, 14.0f), "CLOTH CHANGER",
		lift(kBlue, 0.15f), kPurple);
	text(font.caption, 9.0f, ImVec2(60.0f, 34.0f), kDim, "SKINS  •  OUTFITS  •  STYLE");

	// Accent bar + page title near the center-left.
	const Rect accent_bar = rect(220.0f, 20.0f, 224.0f, 44.0f);
	box(accent_bar, accent_color(), 2.0f);
	text(font.semibold, 19.0f, ImVec2(236.0f, 12.0f), kText, "Wardrobe");
	draw->PushClipRect(ImVec2(236.0f, 36.0f), ImVec2(528.0f, 54.0f), true);
	text(font.caption, 11.0f, ImVec2(236.0f, 38.0f), kMuted, "Swap what the game renders on you.");
	draw->PopClipRect();

	// Drag strip: everything left of the session pill.
	ImGui::SetCursorScreenPos(ImVec2(0.0f, 0.0f));
	ImGui::InvisibleButton("##title_drag", ImVec2(528.0f, kTitleHeight));
	if (ImGui::IsItemClicked())
		StartWindowDrag();

	const bool live = sdauth::HasSession();
	pill(rect(544.0f, 20.0f, 664.0f, 44.0f), live ? "SESSION ACTIVE" : "SESSION ENDED", live ? kGreen : kRed);

	if (window_button("##minimize", rect(672.0f, 16.0f, 704.0f, 48.0f), false))
		ShowWindow(g_hwnd, SW_MINIMIZE);

	if (window_button("##close", rect(736.0f, 16.0f, 768.0f, 48.0f), true))
		PostQuitMessage(0);
}

void draw_footer()
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float line_y = kContentBottom + 20.0f;
	const float text_y = app::kHeight - 36.0f;

	draw->AddLine(ImVec2(kContentLeft, line_y), ImVec2(kContentRight, line_y), color_u32(kLine), 1.0f);

	const ImVec4 state_color = server_state_color();
	draw->AddCircleFilled(ImVec2(kContentLeft + 4.0f, text_y + 6.0f), 3.0f, color_u32(state_color));

	char left[160];
	std::snprintf(left, sizeof(left), "Cloth Changer v%s  |  %s  |  session %s", app::kVersion,
		server_state_text(), session_time().c_str());
	text(font.caption, 10.0f, ImVec2(kContentLeft + 14.0f, text_y), kDim, left);

	if (!shared.show_fps)
		return;

	char right[64];
	std::snprintf(right, sizeof(right), "FPS %d", static_cast<int>(state.fps + 0.5f));
	const float right_width = text_width(font.caption, 10.0f, right);
	text(font.caption, 10.0f, ImVec2(kContentRight - right_width, text_y), kDim, right);
}

} // namespace

void LoadPreferences()
{
	load_preferences();
}

void Render()
{
	// New session = new frame break. Reset per-session state.
	static int last_frame = -2;
	const int frame = ImGui::GetFrameCount();
	if (frame != last_frame + 1)
	{
		shared.session_start = GetTickCount64();
		shared.search[0] = '\0';
		shared.selected = 0;
		shared.selected_local = -1;
		state.page = PageCloth;
		reload_local_apps();
	}
	last_frame = frame;

	state.page = PageCloth;

	if (shared.light != state.light)
		dash::apply_theme(shared.light);

	tick_launch();

	draw_background();
	draw_title_bar();

	DrawCloth();

	draw_footer();
	draw_toasts();
}

} // namespace screen_dashboard
