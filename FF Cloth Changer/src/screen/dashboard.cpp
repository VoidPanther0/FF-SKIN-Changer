#include "common/dashboard_common.h"
#include "common/login_common.h"

#include <cstdio>
#include <string>

// Window shell shown after sign-in: sidebar with the logo and navigation, the
// title bar with the page title, session pill and window buttons, and the
// current page. Only Home has its own file (home.cpp); the other navigation
// entries draw a placeholder until they get one.

namespace screen_dashboard {

namespace {

using namespace ui;
using namespace dash;

struct NavEntry
{
	const char* label;
	const char* subtitle;
	Page page;
	IconFn icon;
};

const NavEntry kNav[] = {
	{ "Home",  "Your changer at a glance.",  PageHome,  icon_home },
};

constexpr int kNavCount = static_cast<int>(sizeof(kNav) / sizeof(kNav[0]));

constexpr float kNavTop = 126.0f;
constexpr float kNavStep = 46.0f;
constexpr float kNavItemHeight = 40.0f;

// Hover highlight per page; eased so the sidebar never snaps.
float g_nav_hover[kNavCount] = {};

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

// The title bar covers the content area only; the sidebar owns the top-left
// corner with the logo.
void draw_title_bar()
{
	const NavEntry& entry = (state.page >= 0 && state.page < kNavCount) ? kNav[state.page] : kNav[0];

	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(ImVec2(kSidebarWidth, 0.0f), ImVec2(app::kWidth, kTitleHeight), color_u32(kHomeBg));
	draw->AddLine(ImVec2(kSidebarWidth, kTitleHeight - 1.0f), ImVec2(app::kWidth, kTitleHeight - 1.0f),
		color_u32(kLine), 1.0f);

	// Page title with the accent bar from the reference design.
	const Rect accent_bar = rect(256.0f, 20.0f, 260.0f, 44.0f);
	box(accent_bar, accent_color(), 2.0f);
	text(font.semibold, 19.0f, ImVec2(272.0f, 12.0f), kText, entry.label);

	draw->PushClipRect(ImVec2(272.0f, 36.0f), ImVec2(528.0f, 54.0f), true);
	text(font.caption, 11.0f, ImVec2(272.0f, 38.0f), kMuted, entry.subtitle);
	draw->PopClipRect();

	// Drag strip: everything left of the session pill.
	ImGui::SetCursorScreenPos(ImVec2(kSidebarWidth, 0.0f));
	ImGui::InvisibleButton("##title_drag", ImVec2(440.0f, kTitleHeight));
	if (ImGui::IsItemClicked())
		StartWindowDrag();

	// Session state is reported from the real session, never assumed.
	const bool live = sdauth::HasSession();
	pill(rect(544.0f, 20.0f, 664.0f, 44.0f), live ? "SESSION ACTIVE" : "SESSION ENDED", live ? kGreen : kRed);

	if (window_button("##minimize", rect(672.0f, 16.0f, 704.0f, 48.0f), false))
		ShowWindow(g_hwnd, SW_MINIMIZE);

	if (window_button("##close", rect(736.0f, 16.0f, 768.0f, 48.0f), true))
		PostQuitMessage(0);
}

bool nav_item(const NavEntry& entry, float y, float hover)
{
	const Rect r = rect(16.0f, y, kSidebarWidth - 16.0f, y + kNavItemHeight);
	ImGui::PushID(entry.label);
	ImGui::SetCursorScreenPos(r.min);
	const bool clicked = ImGui::InvisibleButton("##nav", ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	ImGui::PopID();
	const bool active = state.page == entry.page;
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (active)
	{
		// Blue to purple gradient, as in the reference design.
		gradient_box(r, accent_color(), kPurple, 10.0f);
		if (!shared.reduced_effects)
			draw->AddRect(r.min, r.max, color_u32(fade(kWhite, 0.18f)), 10.0f, 0, 1.0f);
	}
	else if (hover > 0.01f)
	{
		box(r, fade(kCardSoft, hover), 10.0f);
	}

	const ImU32 icon_color = color_u32(active ? kWhite : fade(kNavText, 0.55f + 0.45f * hover));
	entry.icon(ImVec2(r.min.x + 28.0f, r.min.y + kNavItemHeight * 0.5f), icon_color);

	const ImVec4 label_color = active ? kWhite : fade(kNavText, 0.55f + 0.45f * hover);
	text(font.semibold, 14.0f, ImVec2(r.min.x + 48.0f, r.min.y + 12.0f), label_color, entry.label);
	return clicked;
}

void draw_sidebar()
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(kSidebarWidth, app::kHeight), color_u32(kSidebar));
	draw->AddLine(ImVec2(kSidebarWidth - 1.0f, 0.0f), ImVec2(kSidebarWidth - 1.0f, app::kHeight),
		color_u32(kLine), 1.0f);

	// ---------------------------------------------------------------- logo --
	login_ui::DrawLogo(ImVec2(kSidebarWidth * 0.5f, 46.0f), 44.0f);

	const char* wordmark = "CLOTH CHANGER";
	const float wordmark_w = text_width(font.semibold, 15.0f, wordmark);
	gradient_text(font.semibold, 15.0f, ImVec2((kSidebarWidth - wordmark_w) * 0.5f, 78.0f), wordmark,
		lift(kBlue, 0.15f), kPurple);

	const char* dots = "SKINS  \u2022  OUTFITS  \u2022  STYLE";
	const float tagline_w = text_width(font.caption, 9.0f, dots);
	text(font.caption, 9.0f, ImVec2((kSidebarWidth - tagline_w) * 0.5f, 100.0f), kDim, dots);

	divider(24.0f, kSidebarWidth - 24.0f, 116.0f);

	// ------------------------------------------------------------ nav items --
	float y = kNavTop;
	for (int i = 0; i < kNavCount; ++i)
	{
		const NavEntry& entry = kNav[i];
		const bool active = state.page == entry.page;

		// The highlight eases toward the target; nav_item() draws the value
		// from the previous frame and the new one is computed right after the
		// button was submitted, so hover can be read back safely here.
		float& anim = g_nav_hover[i];
		if (nav_item(entry, y, anim))
			state.page = entry.page;

		const float wanted = active ? 1.0f : (ImGui::IsItemHovered() ? 1.0f : 0.0f);
		if (!shared.animations || shared.reduced_effects)
			anim = wanted;
		else
			anim += (wanted - anim) * clampf(ImGui::GetIO().DeltaTime * 14.0f, 0.0f, 1.0f);

		y += kNavStep;
	}
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
	std::snprintf(left, sizeof(left), "Sachin Loader v%s  |  %s  |  session %s", app::kVersion,
		server_state_text(), session_time().c_str());
	text(font.caption, 10.0f, ImVec2(kContentLeft + 14.0f, text_y), kDim, left);

	if (!shared.show_fps)
		return;

	char right[64];
	std::snprintf(right, sizeof(right), "FPS %d", static_cast<int>(state.fps + 0.5f));
	const float right_width = text_width(font.caption, 10.0f, right);
	text(font.caption, 10.0f, ImVec2(kContentRight - right_width, text_y), kDim, right);
}

// Pages that do not have their own file yet. They stay in the navigation, so
// they show a clear placeholder instead of pretending to be the Home page.
void draw_placeholder(const char* title, IconFn icon)
{
	const Rect card = rect(kContentLeft, kContentTop, kContentRight, kContentBottom);
	box(card, kCard, 14.0f, card_border());
	section_header(card, title, icon);
	divider(card.min.x + 24.0f, card.max.x - 24.0f, card.min.y + 64.0f);

	const Rect area = rect(card.min.x + 20.0f, card.min.y + 80.0f, card.max.x - 20.0f, card.max.y - 16.0f);
	draw_empty_state(area, "Coming soon", "This page is not part of this build yet.");
}

} // namespace

void LoadPreferences()
{
	load_preferences();
}

void Render()
{
	// The dashboard is drawn only while signed in, so a break in the frame
	// sequence means a new session: reset the timer, search and page.
	static int last_frame = -2;
	const int frame = ImGui::GetFrameCount();
	if (frame != last_frame + 1)
	{
		shared.session_start = GetTickCount64();
		shared.search[0] = '\0';
		shared.selected = 0;
		shared.selected_local = -1;
		state.page = PageHome;
		reload_local_apps();
	}
	last_frame = frame;

	if (state.page < 0 || state.page >= PageCount)
		state.page = PageHome;

	// The saved palette applies to the dashboard; the login screens keep the
	// dark one (see ui.cpp).
	if (shared.light != state.light)
		dash::apply_theme(shared.light);

	tick_launch();

	draw_background();
	draw_sidebar();
	draw_title_bar();

	// Page dispatch. Home has its own file; the rest of the navigation shows
	// the placeholder above until a page gets its own file and dash::Page value.
	switch (state.page)
	{
	case PageProduct:     draw_placeholder("Product", icon_product); break;
	case PageGameUpdates: draw_placeholder("Game Updates", icon_updates); break;
	case PageSocials:     draw_placeholder("Socials", icon_socials); break;
	case PageSettings:    draw_placeholder("Settings", icon_settings); break;
	default:              DrawHome(); break;
	}

	draw_footer();
	draw_toasts();
}

} // namespace screen_dashboard
