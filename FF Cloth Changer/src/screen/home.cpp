#include "common/dashboard_common.h"
#include "common/login_common.h"

#include <cstdio>
#include <string>
#include <vector>

// Home: welcome banner, key numbers and quick access to released components,
// following the dashboard reference design.

namespace dash {

using namespace ui;

namespace {

// ------------------------------------------------------------------ banner ---

void draw_hero()
{
	const Rect hero = rect(kContentLeft, kContentTop, kContentRight, 200.0f);
	ImDrawList* draw = ImGui::GetWindowDrawList();

	gradient_box(hero, ImVec4(0.055f, 0.085f, 0.205f, 1.0f), ImVec4(0.150f, 0.075f, 0.285f, 1.0f), 16.0f);

	draw->PushClipRect(hero.min, hero.max, true);

	// Decorative artwork: the logo plus a few cheap geometric strokes. No blur,
	// no per-frame allocation; switched off with the reduced-effects option.
	const ImVec2 art(hero.max.x - 96.0f, hero.min.y + 50.0f);
	if (!shared.reduced_effects)
	{
		draw->AddCircleFilled(ImVec2(art.x + 30.0f, art.y - 10.0f), 150.0f,
			color_u32(fade(kPurple, 0.14f)), 48);
		draw->AddLine(ImVec2(hero.max.x - 300.0f, hero.max.y), ImVec2(hero.max.x - 60.0f, hero.min.y - 20.0f),
			color_u32(fade(kWhite, 0.05f)), 46.0f);
		draw->AddLine(ImVec2(hero.max.x - 230.0f, hero.max.y), ImVec2(hero.max.x + 10.0f, hero.min.y + 40.0f),
			color_u32(fade(kBlue, 0.16f)), 22.0f);
	}

	login_ui::DrawLogo(art, 92.0f);
	draw->PopClipRect();
	draw->AddRect(hero.min, hero.max, color_u32(kBorder), 16.0f, 0, 1.0f);

	// Greeting: "Hi, " in plain text and the name in a blue-purple gradient.
	const sdauth::Session session = sdauth::GetSession();
	const char* name = session.username.empty() ? "friend" : session.username.c_str();

	const ImVec2 greeting_pos(hero.min.x + 32.0f, hero.min.y + 24.0f);
	const float hi_width = text_width(font.title, 30.0f, "Hi, ");
	text(font.title, 30.0f, greeting_pos, kText, "Hi, ");
	gradient_text(font.title, 30.0f, ImVec2(greeting_pos.x + hi_width, greeting_pos.y), name,
		lift(accent_color(), 0.10f), kPurple);

	text_wrapped(font.body, 14.0f, ImVec2(hero.min.x + 32.0f, hero.min.y + 66.0f), kMuted,
		"Your loader is ready. Launch your apps and games from one place.", 320.0f);
}

// ------------------------------------------------------------------- stats ---

void draw_stats()
{
	const std::vector<sdauth::Component> components = sdauth::Components();
	const bool signed_in = sdauth::HasSession();

	char count[16];
	std::snprintf(count, sizeof(count), "%d", static_cast<int>(components.size()));

	char build[32];
	std::snprintf(build, sizeof(build), "v%s", app::kVersion);

	// Access comes from the server: "Lifetime" only when the server sends no
	// expiry at all; otherwise the days that are really left.
	const int days_left = access_days_left();
	const std::string date = access_date();
	char access_value[32];
	char access_hint[48];
	ImVec4 access_color = kGreen;
	if (!signed_in)
	{
		std::snprintf(access_value, sizeof(access_value), "\xE2\x80\x94");
		std::snprintf(access_hint, sizeof(access_hint), "Sign in to check");
		access_color = kMuted;
	}
	else if (days_left < 0)
	{
		std::snprintf(access_value, sizeof(access_value), "Lifetime");
		std::snprintf(access_hint, sizeof(access_hint), "No expiry set");
	}
	else
	{
		std::snprintf(access_value, sizeof(access_value), "%d day%s", days_left, days_left == 1 ? "" : "s");
		std::snprintf(access_hint, sizeof(access_hint), "Until %s", date.c_str());
		if (days_left <= 7)
			access_color = kOrange;
	}

	// Three cards fit the content area; the session time moved to the footer.
	constexpr float gap = 12.0f;
	const float w = (kContentRight - kContentLeft - gap * 2.0f) / 3.0f;
	const float y1 = 216.0f;
	const float y2 = 288.0f;
	float x = kContentLeft;

	stat_card(rect(x, y1, x + w, y2), "COMPONENTS", count, "Released for you", accent_color(), icon_components);
	x += w + gap;
	stat_card(rect(x, y1, x + w, y2), "BUILD", build, "Installed", kGreen, icon_cube);
	x += w + gap;
	stat_card(rect(x, y1, x + w, y2), "ACCESS", access_value, access_hint, access_color, icon_crown);
}

// ------------------------------------------------------------ quick access ---

void draw_component_row(const Rect& row, const sdauth::Component& component, bool hovered)
{
	box(row, hovered ? lift(kCardSoft, 0.02f) : kCardSoft, 10.0f, hovered ? &kLineBright : &kLine);

	letter_badge(rect(row.min.x + 12.0f, row.min.y + 8.0f, row.min.x + 48.0f, row.max.y - 8.0f),
		component.name, 15.0f);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(ImVec2(row.min.x + 60.0f, row.min.y), ImVec2(row.max.x - 150.0f, row.max.y), true);
	text(font.semibold, 13.0f, ImVec2(row.min.x + 60.0f, row.min.y + 6.0f), kText, component.name.c_str());
	char version[48];
	std::snprintf(version, sizeof(version), "v%s", component.version.c_str());
	text(font.caption, 10.0f, ImVec2(row.min.x + 60.0f, row.min.y + 23.0f), kMuted, version);
	draw->PopClipRect();

	// A launch in progress shows up on the row it belongs to.
	const bool launching = sdauth::LaunchStatus() == sdauth::StatusWorking
		&& sdauth::LaunchName() == component.name;

	const ImVec4 status = launching ? accent_color() : status_color(component.status);
	const std::string label = launching ? std::string("Starting...")
		: (component_launchable(component) ? std::string("Ready") : status_label(component.status));
	const float label_w = text_width(font.body, 12.0f, label.c_str());
	const float status_x = row.max.x - 76.0f - label_w;
	draw->AddCircleFilled(ImVec2(status_x - 12.0f, row.min.y + height(row) * 0.5f), 3.5f, color_u32(status), 12);
	text(font.body, 12.0f, ImVec2(status_x, row.min.y + height(row) * 0.5f - 7.0f),
		state.light ? dim(status, 0.25f) : lift(status, 0.10f), label.c_str());

	icon_arrow(ImVec2(row.max.x - 24.0f, row.min.y + height(row) * 0.5f),
		color_u32(hovered ? accent_color() : kNavText));
}

// -------------------------------------------------------------- launching ---
//
// The quick access list is the only place a component can start in this build,
// so a click launches it directly (with the confirmation option honored).

int g_launch_pending = -1;   // row waiting for the confirmation popup, or -1
bool g_launch_popup_open = false;

void launch_component(const sdauth::Component& component)
{
	if (!sdauth::LaunchComponent(component.name))
		toast_error("The component cannot start right now. Try Refresh.");
}

void component_clicked(int index, const sdauth::Component& component)
{
	if (!component_launchable(component))
	{
		toast_error("This component cannot start right now");
		return;
	}

	if (!shared.confirm_launch)
	{
		launch_component(component);
		return;
	}

	g_launch_pending = index;
}

// Confirmation popup opened by component_clicked(). Drawn from the page scope
// so the popup id matches on every frame.
void draw_launch_confirm()
{
	if (g_launch_pending < 0)
	{
		g_launch_popup_open = false;
		return;
	}

	const std::vector<sdauth::Component> components = sdauth::Components();
	if (g_launch_pending >= static_cast<int>(components.size()))
	{
		g_launch_pending = -1;
		g_launch_popup_open = false;
		return;
	}

	const sdauth::Component& component = components[static_cast<std::size_t>(g_launch_pending)];
	const std::string title = "Launch " + component.name + "?";
	const std::string body = "The file is downloaded once, checked against its SHA-256 checksum and then started.";

	if (!g_launch_popup_open)
	{
		ImGui::OpenPopup("##confirm_component");
		g_launch_popup_open = true;
	}

	if (confirm_modal("##confirm_component", title.c_str(), body.c_str(), "Launch"))
	{
		launch_component(component);
		g_launch_pending = -1;
		g_launch_popup_open = false;
	}
	else if (!ImGui::IsPopupOpen("##confirm_component"))
	{
		g_launch_pending = -1;   // cancelled
		g_launch_popup_open = false;
	}
}

// Toasts the result of a launch once it finishes.
void tick_launch_toast()
{
	static int seen = sdauth::StatusIdle;
	const int status = sdauth::LaunchStatus();
	if (status == seen)
		return;

	seen = status;
	if (status == sdauth::StatusOk)
		toast(sdauth::LaunchMessage().c_str());
	else if (status == sdauth::StatusError)
		toast_error(sdauth::LaunchMessage().c_str());
}

void draw_quick_access()
{
	const Rect card = rect(kContentLeft, 304.0f, kContentRight, kContentBottom);
	box(card, kCard, 14.0f, card_border());
	section_header(card, "Quick access", icon_bolt);

	divider(card.min.x + 24.0f, card.max.x - 24.0f, card.min.y + 64.0f);

	const Rect area = rect(card.min.x + 20.0f, card.min.y + 80.0f, card.max.x - 20.0f, card.max.y - 16.0f);
	const std::vector<sdauth::Component> components = sdauth::Components();

	if (components.empty())
	{
		const bool syncing = sdauth::StartupStatus() == sdauth::StatusWorking
			|| sdauth::RefreshStatus() == sdauth::StatusWorking;
		if (syncing)
		{
			spinner(ImVec2(center(area).x, center(area).y - 20.0f), accent_color());
			const char* text_value = "Loading components...";
			const float w = text_width(font.body, 13.0f, text_value);
			text(font.body, 13.0f, ImVec2(center(area).x - w * 0.5f, center(area).y + 6.0f), kMuted, text_value);
			return;
		}

		draw_empty_state(area, "Nothing here yet", "Apps and games released for your account appear here.");
		return;
	}

	constexpr float gap = 12.0f;
	const float row_h = (height(area) - gap) * 0.5f;

	ImGui::SetCursorScreenPos(area.min);
	ImGui::BeginChild("##quick_rows", ImVec2(width(area), height(area)), 0, ImGuiWindowFlags_NoBackground);
	const ImVec2 origin = ImGui::GetCursorScreenPos();

	const int count = static_cast<int>(components.size());
	for (int i = 0; i < count; ++i)
	{
		const Rect row = rect(origin.x, origin.y + static_cast<float>(i) * (row_h + gap),
			origin.x + width(area) - 4.0f, origin.y + static_cast<float>(i) * (row_h + gap) + row_h);

		ImGui::PushID(i);
		ImGui::SetCursorScreenPos(row.min);
		const bool clicked = ImGui::InvisibleButton("##quick_row", ImVec2(width(row), height(row)));
		const bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		if (hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

		draw_component_row(row, components[static_cast<std::size_t>(i)], hovered);
		if (clicked)
			component_clicked(i, components[static_cast<std::size_t>(i)]);
	}

	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy(ImVec2(1.0f, static_cast<float>(count) * (row_h + gap) - gap));
	ImGui::EndChild();
}

} // namespace

void DrawHome()
{
	tick_launch_toast();

	draw_hero();
	draw_stats();
	draw_quick_access();

	draw_launch_confirm();
}

} // namespace dash
