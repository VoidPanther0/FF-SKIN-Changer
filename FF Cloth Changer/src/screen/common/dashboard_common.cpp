#include "dashboard_common.h"
#include "imgui_internal.h"

#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cwchar>
#include <shellapi.h>
#include <tlhelp32.h>
#include <vector>

namespace dash {

using namespace ui;

const AccentPreset kAccentPresets[] = {
	{ "Blue",   0.42f, 0.56f, 1.00f },
	{ "Violet", 0.62f, 0.42f, 1.00f },
	{ "Cyan",   0.25f, 0.78f, 0.95f },
	{ "Green",  0.35f, 0.82f, 0.55f },
	{ "Pink",   0.95f, 0.42f, 0.68f },
	{ "Orange", 1.00f, 0.62f, 0.30f },
};

const int kAccentPresetCount = static_cast<int>(sizeof(kAccentPresets) / sizeof(kAccentPresets[0]));

Shared shared;

// ------------------------------------------------------------------ theme ----

void apply_theme(bool light)
{
	shared.light = light;

	if (light)
	{
		kHomeBg  = ImVec4(0.925f, 0.937f, 0.957f, 1.0f);
		kSidebar = ImVec4(0.882f, 0.898f, 0.929f, 1.0f);
		kCard    = ImVec4(0.984f, 0.988f, 0.996f, 1.0f);
		kCardSoft= ImVec4(0.929f, 0.937f, 0.957f, 1.0f);
		kBorder  = ImVec4(0.22f, 0.30f, 0.55f, 0.25f);
		kNavText = ImVec4(0.32f, 0.36f, 0.46f, 1.0f);
	}
	else
	{
		kHomeBg  = ImVec4(0.020f, 0.024f, 0.043f, 1.0f);
		kSidebar = ImVec4(0.031f, 0.039f, 0.063f, 1.0f);
		kCard    = ImVec4(0.043f, 0.055f, 0.090f, 1.0f);
		kCardSoft= ImVec4(0.055f, 0.071f, 0.115f, 1.0f);
		kBorder  = ImVec4(0.30f, 0.38f, 0.62f, 0.22f);
		kNavText = ImVec4(0.55f, 0.61f, 0.76f, 1.0f);
	}

	ui::apply_theme(light);
	ui::apply_style();
	ui::apply_accent();
}

// ------------------------------------------------------------ preferences ----

std::wstring settings_path(bool create_directory)
{
	wchar_t appdata[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();

	std::wstring directory = std::wstring(appdata) + L"\\SachinDevLoader";
	if (create_directory)
		CreateDirectoryW(directory.c_str(), nullptr);

	return directory + L"\\settings.ini";
}

void save_preferences()
{
	const std::wstring path = settings_path(true);
	if (path.empty())
		return;

	wchar_t accent[48] = {};
	std::swprintf(accent, 48, L"%d,%d,%d",
		static_cast<int>(state.accent[0] * 255.0f + 0.5f),
		static_cast<int>(state.accent[1] * 255.0f + 0.5f),
		static_cast<int>(state.accent[2] * 255.0f + 0.5f));
	WritePrivateProfileStringW(L"ui", L"accent", accent, path.c_str());
	WritePrivateProfileStringW(L"ui", L"show_fps", shared.show_fps ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"background_glow", shared.background_glow ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"card_borders", shared.card_borders ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"animations", shared.animations ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"reduced_effects", shared.reduced_effects ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"theme_light", shared.light ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"toasts", shared.toasts ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"confirm_launch", shared.confirm_launch ? L"1" : L"0", path.c_str());
	WritePrivateProfileStringW(L"ui", L"stop_duplicates", shared.stop_duplicates ? L"1" : L"0", path.c_str());
}

void load_preferences()
{
	const std::wstring path = settings_path(false);
	if (path.empty())
	{
		apply_theme(false);
		return;
	}

	wchar_t accent[48] = {};
	GetPrivateProfileStringW(L"ui", L"accent", L"", accent, 48, path.c_str());
	int r = 0, g = 0, b = 0;
	if (swscanf_s(accent, L"%d,%d,%d", &r, &g, &b) == 3)
	{
		state.accent[0] = clampf(static_cast<float>(r) / 255.0f, 0.0f, 1.0f);
		state.accent[1] = clampf(static_cast<float>(g) / 255.0f, 0.0f, 1.0f);
		state.accent[2] = clampf(static_cast<float>(b) / 255.0f, 0.0f, 1.0f);
	}

	shared.show_fps = GetPrivateProfileIntW(L"ui", L"show_fps", 1, path.c_str()) != 0;
	shared.background_glow = GetPrivateProfileIntW(L"ui", L"background_glow", 1, path.c_str()) != 0;
	shared.card_borders = GetPrivateProfileIntW(L"ui", L"card_borders", 1, path.c_str()) != 0;
	shared.animations = GetPrivateProfileIntW(L"ui", L"animations", 1, path.c_str()) != 0;
	shared.reduced_effects = GetPrivateProfileIntW(L"ui", L"reduced_effects", 0, path.c_str()) != 0;
	shared.light = GetPrivateProfileIntW(L"ui", L"theme_light", 0, path.c_str()) != 0;
	shared.toasts = GetPrivateProfileIntW(L"ui", L"toasts", 1, path.c_str()) != 0;
	shared.confirm_launch = GetPrivateProfileIntW(L"ui", L"confirm_launch", 0, path.c_str()) != 0;
	shared.stop_duplicates = GetPrivateProfileIntW(L"ui", L"stop_duplicates", 1, path.c_str()) != 0;

	// Paints the palette and rebuilds the ImGui style on top of it.
	apply_theme(shared.light);
}

void reset_preferences()
{
	state.accent[0] = kAccentPresets[0].r;
	state.accent[1] = kAccentPresets[0].g;
	state.accent[2] = kAccentPresets[0].b;
	shared.show_fps = true;
	shared.background_glow = true;
	shared.card_borders = true;
	shared.animations = true;
	shared.reduced_effects = false;
	shared.light = false;
	save_preferences();
	apply_theme(false);
}

const ImVec4* card_border()
{
	return shared.card_borders ? &kBorder : nullptr;
}

// ------------------------------------------------------------------ icons ----

void icon_home(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 8.0f, c.y - 1.0f), ImVec2(c.x, c.y - 8.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x + 8.0f, c.y - 1.0f), color, 1.6f);
	draw->AddRect(ImVec2(c.x - 6.0f, c.y - 1.0f), ImVec2(c.x + 6.0f, c.y + 8.0f), color, 2.0f, 0, 1.6f);
	draw->AddLine(ImVec2(c.x, c.y + 3.0f), ImVec2(c.x, c.y + 8.0f), color, 1.6f);
}

void icon_apps(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRect(ImVec2(c.x - 8.0f, c.y - 8.0f), ImVec2(c.x - 1.5f, c.y - 1.5f), color, 2.0f, 0, 1.5f);
	draw->AddRect(ImVec2(c.x + 1.5f, c.y - 8.0f), ImVec2(c.x + 8.0f, c.y - 1.5f), color, 2.0f, 0, 1.5f);
	draw->AddRect(ImVec2(c.x - 8.0f, c.y + 1.5f), ImVec2(c.x - 1.5f, c.y + 8.0f), color, 2.0f, 0, 1.5f);
	draw->AddRect(ImVec2(c.x + 1.5f, c.y + 1.5f), ImVec2(c.x + 8.0f, c.y + 8.0f), color, 2.0f, 0, 1.5f);
}

void icon_download(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x, c.y - 8.0f), ImVec2(c.x, c.y + 3.0f), color, 1.7f);
	draw->AddLine(ImVec2(c.x - 5.0f, c.y - 2.0f), ImVec2(c.x, c.y + 3.5f), color, 1.7f);
	draw->AddLine(ImVec2(c.x + 5.0f, c.y - 2.0f), ImVec2(c.x, c.y + 3.5f), color, 1.7f);
	draw->AddLine(ImVec2(c.x - 8.0f, c.y + 8.0f), ImVec2(c.x + 8.0f, c.y + 8.0f), color, 1.7f);
}

void icon_dll(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRect(ImVec2(c.x - 8.0f, c.y - 4.0f), ImVec2(c.x + 5.0f, c.y + 8.0f), color, 2.0f, 0, 1.5f);
	draw->AddLine(ImVec2(c.x - 3.0f, c.y - 8.0f), ImVec2(c.x - 3.0f, c.y - 4.0f), color, 1.5f);
	draw->AddLine(ImVec2(c.x + 2.0f, c.y - 8.0f), ImVec2(c.x + 2.0f, c.y - 4.0f), color, 1.5f);
	draw->AddLine(ImVec2(c.x + 5.0f, c.y + 2.0f), ImVec2(c.x + 9.0f, c.y + 2.0f), color, 1.5f);
}

void icon_theme(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x, c.y - 9.0f));
	draw->PathBezierCubicCurveTo(ImVec2(c.x + 4.0f, c.y - 4.0f), ImVec2(c.x + 8.0f, c.y), ImVec2(c.x + 7.5f, c.y + 3.5f), 16);
	draw->PathBezierCubicCurveTo(ImVec2(c.x + 7.0f, c.y + 8.0f), ImVec2(c.x - 7.0f, c.y + 8.0f), ImVec2(c.x - 7.5f, c.y + 3.5f), 16);
	draw->PathBezierCubicCurveTo(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x, c.y - 9.0f), 16);
	draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);
}

void icon_account(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(ImVec2(c.x, c.y - 4.0f), 4.0f, color, 20, 1.6f);
	draw->PathArcTo(ImVec2(c.x, c.y + 9.0f), 7.5f, IM_PI, IM_PI * 2.0f, 16);
	draw->PathStroke(color, 0, 1.6f);
}

void icon_settings(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(c, 6.5f, color, 24, 1.6f);
	draw->AddCircleFilled(c, 2.2f, color, 12);
	for (int i = 0; i < 8; ++i)
	{
		const float angle = static_cast<float>(i) * (6.28318530718f / 8.0f);
		const ImVec2 from(c.x + std::cos(angle) * 8.0f, c.y + std::sin(angle) * 8.0f);
		const ImVec2 to(c.x + std::cos(angle) * 10.5f, c.y + std::sin(angle) * 10.5f);
		draw->AddLine(from, to, color, 1.6f);
	}
}

void icon_about(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(c, 9.0f, color, 28, 1.6f);
	draw->AddLine(ImVec2(c.x, c.y - 1.0f), ImVec2(c.x, c.y + 5.0f), color, 1.8f);
	draw->AddCircleFilled(ImVec2(c.x, c.y - 4.5f), 1.2f, color, 8);
}

void icon_product(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 6.0f, c.y - 5.0f), ImVec2(c.x - 2.0f, c.y - 2.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 2.0f, c.y - 2.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 2.0f, c.y), ImVec2(c.x + 6.0f, c.y - 5.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 6.0f, c.y - 5.0f), ImVec2(c.x - 6.0f, c.y + 5.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 6.0f, c.y - 5.0f), ImVec2(c.x + 6.0f, c.y + 5.0f), color, 1.6f);
}

void icon_game_updates(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 5.0f, c.y - 7.0f), ImVec2(c.x - 2.0f, c.y - 4.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 2.0f, c.y), ImVec2(c.x + 5.0f, c.y - 7.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 5.0f, c.y + 7.0f), ImVec2(c.x - 2.0f, c.y + 4.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 2.0f, c.y + 4.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 5.0f, c.y + 7.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
}

void icon_updates(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 5.0f, c.y - 7.0f), ImVec2(c.x - 2.0f, c.y - 4.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 2.0f, c.y), ImVec2(c.x + 5.0f, c.y - 7.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 5.0f, c.y + 7.0f), ImVec2(c.x - 2.0f, c.y + 4.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 2.0f, c.y + 4.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 5.0f, c.y + 7.0f), ImVec2(c.x + 2.0f, c.y), color, 1.6f);
}

void icon_socials(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 7.0f, c.y - 5.0f), ImVec2(c.x - 4.0f, c.y - 2.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x, c.y), ImVec2(c.x + 4.0f, c.y - 2.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 4.0f, c.y - 2.0f), ImVec2(c.x + 7.0f, c.y - 5.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 7.0f, c.y - 5.0f), ImVec2(c.x - 7.0f, c.y - 5.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 7.0f, c.y + 5.0f), ImVec2(c.x - 4.0f, c.y + 2.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 4.0f, c.y + 2.0f), ImVec2(c.x, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x, c.y), ImVec2(c.x + 4.0f, c.y + 2.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 4.0f, c.y + 2.0f), ImVec2(c.x + 7.0f, c.y + 5.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 7.0f, c.y + 5.0f), ImVec2(c.x - 7.0f, c.y + 5.0f), color, 1.6f);
}

void icon_logout(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 7.0f, c.y - 8.0f), ImVec2(c.x - 7.0f, c.y + 8.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 7.0f, c.y - 8.0f), ImVec2(c.x - 2.0f, c.y - 8.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 7.0f, c.y + 8.0f), ImVec2(c.x - 2.0f, c.y + 8.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 3.0f, c.y), ImVec2(c.x + 8.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 3.0f, c.y - 5.0f), ImVec2(c.x + 8.0f, c.y), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 3.0f, c.y + 5.0f), ImVec2(c.x + 8.0f, c.y), color, 1.6f);
}

void icon_search(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(ImVec2(c.x - 1.5f, c.y - 1.5f), 5.5f, color, 20, 1.6f);
	draw->AddLine(ImVec2(c.x + 2.5f, c.y + 2.5f), ImVec2(c.x + 7.0f, c.y + 7.0f), color, 1.6f);
}

void icon_components(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRect(ImVec2(c.x - 8.0f, c.y - 8.0f), ImVec2(c.x + 1.0f, c.y + 1.0f), color, 2.5f, 0, 1.6f);
	draw->AddRect(ImVec2(c.x - 1.0f, c.y - 1.0f), ImVec2(c.x + 8.0f, c.y + 8.0f), color, 2.5f, 0, 1.6f);
}

void icon_cube(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 top(c.x, c.y - 8.0f);
	const ImVec2 upper_right(c.x + 7.0f, c.y - 4.0f);
	const ImVec2 lower_right(c.x + 7.0f, c.y + 4.0f);
	const ImVec2 bottom(c.x, c.y + 8.0f);
	const ImVec2 lower_left(c.x - 7.0f, c.y + 4.0f);
	const ImVec2 upper_left(c.x - 7.0f, c.y - 4.0f);
	const ImVec2 middle(c.x, c.y);

	draw->PathClear();
	draw->PathLineTo(top);
	draw->PathLineTo(upper_right);
	draw->PathLineTo(lower_right);
	draw->PathLineTo(bottom);
	draw->PathLineTo(lower_left);
	draw->PathLineTo(upper_left);
	draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);

	draw->AddLine(upper_left, middle, color, 1.6f);
	draw->AddLine(upper_right, middle, color, 1.6f);
	draw->AddLine(middle, bottom, color, 1.6f);
}

void icon_crown(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x - 8.0f, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x - 5.0f, c.y - 8.0f));
	draw->PathLineTo(ImVec2(c.x, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x + 5.0f, c.y - 8.0f));
	draw->PathLineTo(ImVec2(c.x + 8.0f, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x + 6.0f, c.y + 7.0f));
	draw->PathLineTo(ImVec2(c.x - 6.0f, c.y + 7.0f));
	draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);
}

void icon_clock(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(c, 8.5f, color, 28, 1.6f);
	draw->AddLine(c, ImVec2(c.x, c.y - 5.0f), color, 1.6f);
	draw->AddLine(c, ImVec2(c.x + 4.0f, c.y + 2.0f), color, 1.6f);
}

void icon_bolt(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x - 2.0f, c.y - 9.0f));
	draw->PathLineTo(ImVec2(c.x + 5.0f, c.y - 9.0f));
	draw->PathLineTo(ImVec2(c.x + 1.0f, c.y - 1.0f));
	draw->PathLineTo(ImVec2(c.x + 6.0f, c.y - 1.0f));
	draw->PathLineTo(ImVec2(c.x - 3.0f, c.y + 9.0f));
	draw->PathLineTo(ImVec2(c.x - 1.0f, c.y + 1.0f));
	draw->PathLineTo(ImVec2(c.x - 6.0f, c.y + 1.0f));
	draw->PathStroke(color, ImDrawFlags_Closed, 1.5f);
}

void icon_activity(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x - 9.0f, c.y));
	draw->PathLineTo(ImVec2(c.x - 4.0f, c.y));
	draw->PathLineTo(ImVec2(c.x - 1.5f, c.y - 7.0f));
	draw->PathLineTo(ImVec2(c.x + 1.5f, c.y + 7.0f));
	draw->PathLineTo(ImVec2(c.x + 4.5f, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x + 6.5f, c.y));
	draw->PathLineTo(ImVec2(c.x + 9.0f, c.y));
	draw->PathStroke(color, 0, 1.7f);
}

void icon_server(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRect(ImVec2(c.x - 8.0f, c.y - 7.0f), ImVec2(c.x + 8.0f, c.y - 1.0f), color, 2.0f, 0, 1.5f);
	draw->AddRect(ImVec2(c.x - 8.0f, c.y + 1.0f), ImVec2(c.x + 8.0f, c.y + 7.0f), color, 2.0f, 0, 1.5f);
	draw->AddCircleFilled(ImVec2(c.x - 5.0f, c.y - 4.0f), 1.2f, color, 8);
	draw->AddCircleFilled(ImVec2(c.x - 5.0f, c.y + 4.0f), 1.2f, color, 8);
}

void icon_plus(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 7.0f, c.y), ImVec2(c.x + 7.0f, c.y), color, 1.8f);
	draw->AddLine(ImVec2(c.x, c.y - 7.0f), ImVec2(c.x, c.y + 7.0f), color, 1.8f);
}

void icon_refresh(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathArcTo(c, 6.5f, 0.7f, 5.3f, 24);
	draw->PathStroke(color, 0, 1.6f);

	const float angle = 0.7f;
	const ImVec2 tip(c.x + std::cos(angle) * 6.5f, c.y + std::sin(angle) * 6.5f);
	draw->AddLine(tip, ImVec2(tip.x + 4.0f, tip.y - 3.5f), color, 1.6f);
	draw->AddLine(tip, ImVec2(tip.x + 4.5f, tip.y + 1.5f), color, 1.6f);
}

void icon_folder(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x - 8.0f, c.y - 5.0f));
	draw->PathLineTo(ImVec2(c.x - 1.0f, c.y - 5.0f));
	draw->PathLineTo(ImVec2(c.x + 0.5f, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x + 8.0f, c.y - 3.0f));
	draw->PathLineTo(ImVec2(c.x + 8.0f, c.y + 6.0f));
	draw->PathLineTo(ImVec2(c.x - 8.0f, c.y + 6.0f));
	draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);
}

void icon_play(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathClear();
	draw->PathLineTo(ImVec2(c.x - 4.0f, c.y - 7.0f));
	draw->PathLineTo(ImVec2(c.x + 7.0f, c.y));
	draw->PathLineTo(ImVec2(c.x - 4.0f, c.y + 7.0f));
	draw->PathStroke(color, ImDrawFlags_Closed, 1.6f);
}

void icon_trash(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 7.0f, c.y - 4.0f), ImVec2(c.x + 7.0f, c.y - 4.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x - 3.0f, c.y - 4.0f), ImVec2(c.x - 3.0f, c.y - 7.0f), color, 1.6f);
	draw->AddLine(ImVec2(c.x + 3.0f, c.y - 4.0f), ImVec2(c.x + 3.0f, c.y - 7.0f), color, 1.6f);
	draw->AddRect(ImVec2(c.x - 5.5f, c.y - 2.0f), ImVec2(c.x + 5.5f, c.y + 8.0f), color, 2.0f, 0, 1.6f);
	draw->AddLine(ImVec2(c.x - 1.5f, c.y + 1.0f), ImVec2(c.x - 1.5f, c.y + 5.0f), color, 1.4f);
	draw->AddLine(ImVec2(c.x + 1.5f, c.y + 1.0f), ImVec2(c.x + 1.5f, c.y + 5.0f), color, 1.4f);
}

void icon_arrow(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 6.0f, c.y), ImVec2(c.x + 5.0f, c.y), color, 1.7f);
	draw->AddLine(ImVec2(c.x + 1.0f, c.y - 4.0f), ImVec2(c.x + 5.0f, c.y), color, 1.7f);
	draw->AddLine(ImVec2(c.x + 1.0f, c.y + 4.0f), ImVec2(c.x + 5.0f, c.y), color, 1.7f);
}

// ---------------------------------------------------------------- helpers ----

float text_wrapped(ImFont* f, float size, ImVec2 position, const ImVec4& color, const char* value, float wrap_width)
{
	ImGui::GetWindowDrawList()->AddText(f, size, position, color_u32(color), value, nullptr, wrap_width);
	return f->CalcTextSizeA(size, FLT_MAX, wrap_width, value).y;
}

void letter_badge(const Rect& r, const std::string& name, float size)
{
	gradient_box(r, fade(accent_color(), 0.85f), kPurple, 8.0f);
	char letter[2] = { name.empty() ? '?' : static_cast<char>(
		std::toupper(static_cast<unsigned char>(name[0]))), '\0' };
	// Always white: the badge keeps its gradient in both themes.
	text_center(font.semibold, size, r, kWhite, letter);
}

bool contains_ci(const std::string& haystack, const char* needle)
{
	if (needle[0] == '\0')
		return true;

	const std::size_t n = std::char_traits<char>::length(needle);
	if (n > haystack.size())
		return false;

	for (std::size_t i = 0; i + n <= haystack.size(); ++i)
	{
		std::size_t j = 0;
		while (j < n && std::tolower(static_cast<unsigned char>(haystack[i + j]))
			== std::tolower(static_cast<unsigned char>(needle[j])))
			++j;
		if (j == n)
			return true;
	}
	return false;
}

ImVec4 status_color(const std::string& status)
{
	if (status == "online")
		return kGreen;
	if (status == "beta")
		return accent_color();
	if (status == "updating")
		return kOrange;
	return kRed;
}

std::string status_label(const std::string& status)
{
	std::string label = status.empty() ? "online" : status;
	for (char& c : label)
		c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return label;
}

bool component_launchable(const sdauth::Component& component)
{
	return !component.download_url.empty() && (component.status == "online" || component.status == "beta");
}

int access_days_left()
{
	const long long expires = sdauth::AccountExpiresAt();
	if (expires <= 0)
		return -1;

	const long long left = expires - static_cast<long long>(std::time(nullptr));
	return left <= 0 ? 0 : static_cast<int>((left + 86399) / 86400);
}

std::string access_date()
{
	const long long expires = sdauth::AccountExpiresAt();
	if (expires <= 0)
		return std::string();

	const std::time_t stamp = static_cast<std::time_t>(expires);
	std::tm parts = {};
	if (localtime_s(&parts, &stamp) != 0)
		return std::string();

	char text[32];
	std::strftime(text, sizeof(text), "%Y-%m-%d", &parts);
	return text;
}

void open_url(const std::string& url)
{
	if (url.compare(0, 8, "https://") != 0)
		return;
	ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void open_data_folder()
{
	wchar_t appdata[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return;

	const std::wstring directory = std::wstring(appdata) + L"\\SachinDevLoader";
	CreateDirectoryW(directory.c_str(), nullptr);
	ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string short_hwid()
{
	const std::string& hwid = sdauth::MachineHwid();
	return hwid.size() > 12 ? hwid.substr(0, 12) + "..." : hwid;
}

std::string session_time()
{
	const ULONGLONG seconds = (GetTickCount64() - shared.session_start) / 1000ULL;
	char value[32];
	if (seconds >= 3600ULL)
		std::snprintf(value, sizeof(value), "%lluh %02llum", seconds / 3600ULL, (seconds / 60ULL) % 60ULL);
	else
		std::snprintf(value, sizeof(value), "%llum %02llus", seconds / 60ULL, seconds % 60ULL);
	return value;
}

std::string diagnostics_text()
{
	const sdauth::Session session = sdauth::GetSession();
	const std::string date = access_date();

	char value[768];
	std::snprintf(value, sizeof(value),
		"Sachin Loader diagnostics\r\n"
		"Version: %s\r\n"
		"Account: %s\r\n"
		"Access until: %s\r\n"
		"Components: %d\r\n"
		"Local apps: %d\r\n"
		"Device: %s\r\n"
		"Server: %s\r\n"
		"FPS: %d\r\n",
		app::kVersion,
		session.username.empty() ? "guest" : session.username.c_str(),
		date.empty() ? "no expiry" : date.c_str(),
		static_cast<int>(sdauth::Components().size()),
		static_cast<int>(local_apps().size()),
		short_hwid().c_str(),
		server_state_text(),
		static_cast<int>(state.fps + 0.5f));
	return value;
}

// --------------------------------------------------------- server status ----

const char* server_state_text()
{
	if (sdauth::HasSession())
		return "Connected";
	if (sdauth::StartupStatus() == sdauth::StatusError)
		return "Offline";
	return "Unknown";
}

ImVec4 server_state_color()
{
	if (sdauth::HasSession())
		return kGreen;
	if (sdauth::StartupStatus() == sdauth::StatusError)
		return kRed;
	return kOrange;
}

std::wstring component_cache_path(const std::string& name)
{
	wchar_t appdata[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();

	// Same naming rule as the launcher: letters, digits, dash and underscore.
	std::wstring file;
	for (const char c : name)
	{
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
			file += static_cast<wchar_t>(c);
		else if (c == ' ')
			file += L'_';
	}
	if (file.empty())
		file = L"component";

	return std::wstring(appdata) + L"\\SachinDevLoader\\components\\" + file + L".exe";
}

long long component_cache_size(const std::string& name)
{
	const std::wstring path = component_cache_path(name);
	if (path.empty())
		return -1;

	WIN32_FILE_ATTRIBUTE_DATA info = {};
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
		return -1;

	return (static_cast<long long>(info.nFileSizeHigh) << 32) | static_cast<long long>(info.nFileSizeLow);
}

std::string format_size(long long bytes)
{
	char value[32];
	if (bytes >= 1024LL * 1024LL)
		std::snprintf(value, sizeof(value), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
	else
		std::snprintf(value, sizeof(value), "%lld KB", (bytes + 1023LL) / 1024LL);
	return value;
}

bool clear_component_cache(const std::string& name)
{
	const std::wstring path = component_cache_path(name);
	if (path.empty())
		return false;
	return DeleteFileW(path.c_str()) != FALSE;
}

// --------------------------------------------------------- local apps -------
//
// Executables the user picked himself, stored in settings.ini under
// [localapps]. Only these configured files are ever started.

namespace {

std::vector<LocalApp> g_local_apps;
bool g_local_loaded = false;

std::string to_utf8(const std::wstring& value)
{
	if (value.empty())
		return std::string();
	const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()),
		nullptr, 0, nullptr, nullptr);
	if (length <= 0)
		return std::string();
	std::string out(static_cast<std::size_t>(length), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &out[0], length, nullptr, nullptr);
	return out;
}

std::wstring to_wide(const std::string& value)
{
	if (value.empty())
		return std::wstring();
	const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
	if (length <= 0)
		return std::wstring();
	std::wstring out(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &out[0], length);
	return out;
}

// File name of an absolute path: "C:\apps\Tool.exe" -> "Tool.exe".
std::wstring exe_name(const std::wstring& path)
{
	const std::size_t slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

void save_local_apps()
{
	const std::wstring path = settings_path(true);
	if (path.empty())
		return;

	wchar_t count[16] = {};
	std::swprintf(count, 16, L"%d", static_cast<int>(g_local_apps.size()));
	WritePrivateProfileStringW(L"localapps", L"count", count, path.c_str());

	// Drop entries that are no longer in the list.
	for (int i = static_cast<int>(g_local_apps.size()); i < 64; ++i)
	{
		wchar_t key[16] = {};
		std::swprintf(key, 16, L"%d", i);
		WritePrivateProfileStringW(L"localapps", key, nullptr, path.c_str());
	}

	for (std::size_t i = 0; i < g_local_apps.size(); ++i)
	{
		wchar_t key[16] = {};
		std::swprintf(key, 16, L"%d", static_cast<int>(i));
		const std::wstring entry = to_wide(g_local_apps[i].name) + L"|" + to_wide(g_local_apps[i].path);
		WritePrivateProfileStringW(L"localapps", key, entry.c_str(), path.c_str());
	}
}

// True when a process with this file name is already running.
bool process_is_running(const std::wstring& file_name)
{
	if (file_name.empty())
		return false;

	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;

	bool found = false;
	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry))
	{
		do
		{
			if (lstrcmpiW(entry.szExeFile, file_name.c_str()) == 0)
			{
				found = true;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return found;
}

std::string windows_error_text(const char* fallback)
{
	wchar_t buffer[512] = {};
	const DWORD length = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, GetLastError(), 0, buffer, 512, nullptr);
	if (length == 0)
		return fallback;

	std::wstring text(buffer, length);
	while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' '))
		text.pop_back();

	const std::string out = to_utf8(text);
	return out.empty() ? fallback : out;
}

// Cooldown so a double click cannot start the same program twice.
ULONGLONG g_last_launch_tick = 0;
std::wstring g_last_launch_path;

} // namespace

void reload_local_apps()
{
	g_local_apps.clear();
	g_local_loaded = true;

	const std::wstring path = settings_path(false);
	if (path.empty())
		return;

	const int count = GetPrivateProfileIntW(L"localapps", L"count", 0, path.c_str());
	for (int i = 0; i < count && i < 64; ++i)
	{
		wchar_t key[16] = {};
		std::swprintf(key, 16, L"%d", i);

		wchar_t entry[1024] = {};
		GetPrivateProfileStringW(L"localapps", key, L"", entry, 1024, path.c_str());
		if (entry[0] == L'\0')
			continue;

		const std::wstring value(entry);
		const std::size_t bar = value.find(L'|');
		if (bar == std::wstring::npos || bar == 0)
			continue;

		LocalApp app;
		app.name = to_utf8(value.substr(0, bar));
		app.path = to_utf8(value.substr(bar + 1));
		if (!app.path.empty())
			g_local_apps.push_back(app);
	}
}

const std::vector<LocalApp>& local_apps()
{
	if (!g_local_loaded)
		reload_local_apps();
	return g_local_apps;
}

bool add_local_app(const std::wstring& path, std::string& error)
{
	error.clear();

	const DWORD attributes = GetFileAttributesW(path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		error = "That file does not exist.";
		return false;
	}

	const std::wstring file = exe_name(path);
	const std::size_t dot = file.find_last_of(L'.');
	const std::wstring extension = dot == std::wstring::npos ? std::wstring() : file.substr(dot);
	if (lstrcmpiW(extension.c_str(), L".exe") != 0)
	{
		error = "Only .exe programs can be added.";
		return false;
	}

	for (const LocalApp& app : local_apps())
	{
		if (lstrcmpiW(to_wide(app.path).c_str(), path.c_str()) == 0)
		{
			error = "That program is already in the list.";
			return false;
		}
	}

	if (g_local_apps.size() >= 32)
	{
		error = "The list is full. Remove an entry first.";
		return false;
	}

	LocalApp app;
	app.path = to_utf8(path);
	app.name = to_utf8(dot == std::wstring::npos ? file : file.substr(0, dot));
	g_local_apps.push_back(app);
	save_local_apps();
	return true;
}

void remove_local_app(int index)
{
	if (index < 0 || index >= static_cast<int>(g_local_apps.size()))
		return;

	g_local_apps.erase(g_local_apps.begin() + index);
	save_local_apps();
}

// Running state is refreshed at most twice a second: a full process snapshot
// per tile per frame is too expensive on low-end machines.
ULONGLONG g_running_scan_tick = 0;
std::vector<char> g_running_cache;

bool local_app_running(int index)
{
	if (!shared.stop_duplicates)
		return false;
	if (index < 0 || index >= static_cast<int>(local_apps().size()))
		return false;

	const ULONGLONG now = GetTickCount64();
	if (g_running_cache.size() != local_apps().size() || now - g_running_scan_tick > 500ULL)
	{
		g_running_scan_tick = now;
		g_running_cache.assign(local_apps().size(), 0);
		for (std::size_t i = 0; i < local_apps().size(); ++i)
			g_running_cache[i] = process_is_running(exe_name(to_wide(local_apps()[i].path))) ? 1 : 0;
	}

	return g_running_cache[static_cast<std::size_t>(index)] != 0;
}

bool launch_local_app(int index, std::string& error)
{
	error.clear();

	if (index < 0 || index >= static_cast<int>(local_apps().size()))
	{
		error = "That entry no longer exists.";
		return false;
	}

	const LocalApp& app = local_apps()[static_cast<std::size_t>(index)];
	const std::wstring wide_path = to_wide(app.path);
	if (wide_path.empty())
	{
		error = "The stored path is empty.";
		return false;
	}

	const DWORD attributes = GetFileAttributesW(wide_path.c_str());
	if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
	{
		error = "The file moved or was deleted. Remove the entry and add it again.";
		return false;
	}

	const ULONGLONG now = GetTickCount64();
	if (g_last_launch_path == wide_path && now - g_last_launch_tick < 3000ULL)
	{
		error = "That program was just started.";
		return false;
	}

	if (process_is_running(exe_name(wide_path)))
	{
		error = "It is already running.";
		return false;
	}

	std::wstring directory = wide_path;
	const std::size_t slash = directory.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		directory.resize(slash);
	else
		directory.clear();

	std::wstring command = L"\"" + wide_path + L"\"";
	STARTUPINFOW info = {};
	info.cb = sizeof(info);
	PROCESS_INFORMATION process = {};

	const BOOL started = CreateProcessW(wide_path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
		directory.empty() ? nullptr : directory.c_str(), &info, &process);
	if (!started)
	{
		error = windows_error_text("Windows could not start the file.");
		return false;
	}

	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);

	g_last_launch_tick = now;
	g_last_launch_path = wide_path;
	return true;
}

// -------------------------------------------------------------- toasts -----

namespace {

struct ToastItem
{
	std::string text;
	double time = 0.0;
	ImVec4 color;
};

std::vector<ToastItem> g_toasts;
constexpr double kToastSeconds = 4.5;

void push_toast(const char* message, const ImVec4& color)
{
	if (!shared.toasts || message == nullptr || message[0] == '\0')
		return;

	if (g_toasts.size() >= 4)
		g_toasts.erase(g_toasts.begin());

	ToastItem item;
	item.text = message;
	item.time = ImGui::GetTime();
	item.color = color;
	g_toasts.push_back(item);
}

} // namespace

void toast(const char* message)
{
	push_toast(message, kGreen);
}

void toast_error(const char* message)
{
	push_toast(message, kRed);
}

void draw_toasts()
{
	if (g_toasts.empty())
		return;

	const double now = ImGui::GetTime();
	while (!g_toasts.empty() && now - g_toasts.front().time > kToastSeconds)
		g_toasts.erase(g_toasts.begin());

	if (g_toasts.empty() || !shared.toasts)
		return;

	ImDrawList* draw = ImGui::GetWindowDrawList();
	float y = app::kHeight - 58.0f;

	for (int i = static_cast<int>(g_toasts.size()) - 1; i >= 0; --i)
	{
		const ToastItem& item = g_toasts[static_cast<std::size_t>(i)];
		const double age = now - item.time;

		float alpha = 1.0f;
		float slide = 0.0f;
		if (shared.animations && !shared.reduced_effects)
		{
			if (age < 0.18)
			{
				alpha = clampf(static_cast<float>(age / 0.18), 0.0f, 1.0f);
				slide = (1.0f - alpha) * 24.0f;
			}
			else if (age > kToastSeconds - 0.5)
				alpha = clampf(static_cast<float>((kToastSeconds - age) / 0.5), 0.0f, 1.0f);
		}

		const float text_w = text_width(font.body, 13.0f, item.text.c_str());
		const float w = clampf(text_w + 56.0f, 240.0f, 420.0f);

		const Rect r = rect(app::kWidth - 24.0f - w + slide, y - 44.0f, app::kWidth - 24.0f + slide, y);
		draw->PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(app::kWidth, app::kHeight), true);

		box(r, fade(kCard, 0.96f * alpha), 10.0f);
		const ImVec4 border = fade(item.color, 0.45f * alpha);
		draw->AddRect(r.min, r.max, color_u32(border), 10.0f, 0, 1.0f);
		draw->AddCircleFilled(ImVec2(r.min.x + 16.0f, r.min.y + height(r) * 0.5f), 3.5f,
			color_u32(fade(item.color, alpha)), 12);

		draw->PushClipRect(ImVec2(r.min.x + 26.0f, r.min.y), ImVec2(r.max.x - 10.0f, r.max.y), true);
		text(font.body, 13.0f, ImVec2(r.min.x + 28.0f, r.min.y + (height(r) - 13.0f) * 0.5f),
			fade(kText, alpha), item.text.c_str());
		draw->PopClipRect();

		draw->PopClipRect();
		y -= 54.0f;
	}
}

bool confirm_modal(const char* id, const char* title, const char* body, const char* confirm_label)
{
	bool confirmed = false;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
	if (ImGui::BeginPopupModal(id, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
	{
		ImGui::PushFont(font.semibold, 17.0f);
		ImGui::TextUnformatted(title);
		ImGui::PopFont();

		ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
		ImGui::PushTextWrapPos(ImGui::GetCursorScreenPos().x + 330.0f);
		ImGui::Text("%s", body);
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();

		ImGui::Spacing();
		ImGui::Spacing();

		if (ImGui::Button(confirm_label, ImVec2(170.0f, 38.0f)))
		{
			confirmed = true;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(100.0f, 38.0f)))
			ImGui::CloseCurrentPopup();

		ImGui::EndPopup();
	}
	ImGui::PopStyleVar();

	return confirmed;
}

void spinner(ImVec2 center, const ImVec4& color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const float phase = (shared.animations && !shared.reduced_effects)
		? static_cast<float>(ImGui::GetTime()) * 3.0f
		: 0.0f;

	for (int i = 0; i < 3; ++i)
	{
		const float wave = shared.animations && !shared.reduced_effects
			? 0.5f + 0.5f * std::sin(phase - static_cast<float>(i) * 1.1f)
			: 0.8f;
		draw->AddCircleFilled(ImVec2(center.x + (static_cast<float>(i) - 1.0f) * 12.0f, center.y),
			3.5f, color_u32(fade(color, 0.30f + 0.70f * wave)), 12);
	}
}

// ---------------------------------------------------------------- widgets ----

void draw_empty_state(const Rect& area, const char* title, const char* subtitle)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec4* border = card_border();
	draw->AddRect(area.min, area.max, color_u32(border != nullptr ? *border : kLine), 12.0f, 0, 1.0f);

	const ImVec2 c = center(area);
	draw->AddCircleFilled(ImVec2(c.x, c.y - 34.0f), 26.0f, color_u32(fade(accent_color(), 0.09f)), 40);
	draw->AddCircle(ImVec2(c.x, c.y - 34.0f), 26.0f, color_u32(fade(accent_color(), 0.35f)), 40, 1.2f);
	icon_plus(ImVec2(c.x, c.y - 34.0f), color_u32(accent_color()));

	const float title_w = text_width(font.semibold, 16.0f, title);
	text(font.semibold, 16.0f, ImVec2(c.x - title_w * 0.5f, c.y + 6.0f), kText, title);

	const float sub_w = text_width(font.caption, 11.0f, subtitle);
	draw->PushClipRect(ImVec2(area.min.x + 8.0f, c.y + 24.0f), ImVec2(area.max.x - 8.0f, area.max.y), true);
	text(font.caption, 11.0f, ImVec2(c.x - sub_w * 0.5f, c.y + 30.0f), kMuted, subtitle);
	draw->PopClipRect();
}

bool text_link(const char* id, const char* value, ImVec2 position)
{
	const float w = text_width(font.semibold, 12.0f, value);
	ImGui::SetCursorScreenPos(position);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, 16.0f));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	text(font.semibold, 12.0f, position, hovered ? lift(accent_color(), 0.2f) : accent_color(), value);
	return clicked;
}

void section_header(const Rect& card, const char* title, IconFn icon)
{
	const ImVec4 accent = accent_color();
	if (icon != nullptr)
		icon(ImVec2(card.min.x + 34.0f, card.min.y + 33.0f), color_u32(lift(accent, 0.12f)));
	text(font.semibold, 17.0f, ImVec2(card.min.x + 54.0f, card.min.y + 24.0f), kText, title);
}

bool primary_button(const char* id, const Rect& r, const char* label, IconFn icon)
{
	ImGui::SetCursorScreenPos(r.min);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	const ImVec4 from = hovered ? lift(accent_color(), 0.06f) : accent_color();
	const ImVec4 to = hovered ? lift(kPurple, 0.06f) : kPurple;
	gradient_box(r, from, to, 10.0f);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	if (!shared.reduced_effects)
	{
		// Thin highlight along the top edge; cheap, no blur.
		draw->AddLine(ImVec2(r.min.x + 10.0f, r.min.y + 1.0f), ImVec2(r.max.x - 10.0f, r.min.y + 1.0f),
			color_u32(fade(kWhite, 0.16f)), 1.0f);
	}

	const ImVec2 c = center(r);
	IconFn glyph = icon != nullptr ? icon : icon_apps;
	glyph(ImVec2(r.min.x + 24.0f, c.y), color_u32(kWhite));

	// Clip the label so it can never run into the arrow.
	draw->PushClipRect(ImVec2(r.min.x + 42.0f, r.min.y), ImVec2(r.max.x - 38.0f, r.max.y), true);
	text(font.semibold, 14.0f, ImVec2(r.min.x + 42.0f, c.y - 7.0f), kWhite, label);
	draw->PopClipRect();
	icon_arrow(ImVec2(r.max.x - 22.0f, c.y), color_u32(kWhite));

	return clicked;
}

void status_badge(const Rect& r, const char* value, const ImVec4& color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(r.min, r.max, color_u32(fade(color, 0.12f)), height(r) * 0.5f);
	draw->AddRect(r.min, r.max, color_u32(fade(color, 0.30f)), height(r) * 0.5f, 0, 1.0f);
	draw->AddCircleFilled(ImVec2(r.min.x + 13.0f, r.min.y + height(r) * 0.5f), 3.0f, color_u32(color), 12);

	const ImVec4 text_color = state.light ? dim(color, 0.32f) : lift(color, 0.15f);
	text(font.caption, 10.0f, ImVec2(r.min.x + 22.0f, r.min.y + (height(r) - 10.0f) * 0.5f - 1.0f),
		text_color, value);
}

void stat_card(const Rect& r, const char* label, const char* value, const char* hint, const ImVec4& color,
	IconFn icon)
{
	box(r, kCard, 14.0f, card_border());

	const Rect tile = rect(r.min.x + 14.0f, r.min.y + 14.0f, r.min.x + 50.0f, r.min.y + 50.0f);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled(tile.min, tile.max, color_u32(fade(color, 0.14f)), 12.0f);
	draw->AddRect(tile.min, tile.max, color_u32(fade(color, 0.32f)), 12.0f, 0, 1.0f);
	if (icon != nullptr)
		icon(center(tile), color_u32(lift(color, 0.10f)));

	// Keep long values inside the card and clear of the arrow.
	draw->PushClipRect(ImVec2(r.min.x + 58.0f, r.min.y), ImVec2(r.max.x - 28.0f, r.max.y), true);
	text(font.caption, 10.0f, ImVec2(r.min.x + 58.0f, r.min.y + 14.0f), kMuted, label);
	text(font.semibold, 17.0f, ImVec2(r.min.x + 58.0f, r.min.y + 30.0f), kText, value);
	draw->PopClipRect();

	text(font.caption, 10.0f, ImVec2(r.min.x + 14.0f, r.min.y + 56.0f), kDim, hint);

	icon_arrow(ImVec2(r.max.x - 16.0f, r.min.y + 32.0f), color_u32(fade(accent_color(), 0.85f)));
}

void draw_search_box(const Rect& r, const char* hint)
{
	const bool has_text = shared.search[0] != '\0';
	box(r, kCardSoft, 9.0f, &kLine);
	icon_search(ImVec2(r.min.x + 18.0f, r.min.y + height(r) * 0.5f), color_u32(kNavText));

	constexpr float font_size = 13.0f;
	ImGui::PushFont(font.body, font_size);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (height(r) - font_size) * 0.5f - 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, kDim);

	ImGui::SetCursorScreenPos(ImVec2(r.min.x + 34.0f, r.min.y));
	ImGui::SetNextItemWidth(width(r) - 34.0f - (has_text ? 30.0f : 10.0f));
	ImGui::InputTextWithHint("##dash_search", hint, shared.search, sizeof(shared.search));

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
	ImGui::PopFont();

	if (!has_text)
		return;

	const Rect clear = rect(r.max.x - 28.0f, r.min.y + 6.0f, r.max.x - 6.0f, r.max.y - 6.0f);
	ImGui::SetCursorScreenPos(clear.min);
	if (ImGui::InvisibleButton("##search_clear", ImVec2(width(clear), height(clear))))
		shared.search[0] = '\0';
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	const ImVec2 c = center(clear);
	const ImU32 glyph = color_u32(hovered ? kText : kMuted);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddLine(ImVec2(c.x - 4.0f, c.y - 4.0f), ImVec2(c.x + 4.0f, c.y + 4.0f), glyph, 1.5f);
	draw->AddLine(ImVec2(c.x + 4.0f, c.y - 4.0f), ImVec2(c.x - 4.0f, c.y + 4.0f), glyph, 1.5f);
}

void card_frame(const Rect& r, const char* title, const char* description)
{
	box(r, kCard, 14.0f, card_border());
	text(font.semibold, 17.0f, ImVec2(r.min.x + 28.0f, r.min.y + 22.0f), kText, title);
	if (description != nullptr && description[0] != '\0')
		text(font.caption, 11.0f, ImVec2(r.min.x + 28.0f, r.min.y + 48.0f), kMuted, description);
}

void info_row(const Rect& card, float y, const char* label, const char* value, const ImVec4& color)
{
	text(font.caption, 10.0f, ImVec2(card.min.x + 28.0f, y + 3.0f), kDim, label);

	const float value_w = text_width(font.body, 14.0f, value);
	const float right = card.max.x - 28.0f;
	const float x = right - value_w < card.min.x + 120.0f ? card.min.x + 120.0f : right - value_w;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(ImVec2(card.min.x + 110.0f, y - 4.0f), ImVec2(right + 2.0f, y + 20.0f), true);
	text(font.body, 14.0f, ImVec2(x, y - 2.0f), color, value);
	draw->PopClipRect();

	divider(card.min.x + 28.0f, card.max.x - 28.0f, y + 22.0f);
}

void placeholder_row(const Rect& r, const char* label, const char* hint)
{
	box(r, kCardSoft, 10.0f, &kLine);
	text(font.semibold, 14.0f, ImVec2(r.min.x + 14.0f, r.min.y + 9.0f), kMuted, label);
	text(font.caption, 11.0f, ImVec2(r.min.x + 14.0f, r.min.y + 29.0f), kDim, hint);

	const char* badge = "NOT AVAILABLE";
	const float badge_w = text_width(font.caption, 9.0f, badge) + 18.0f;
	const Rect b = rect(r.max.x - 14.0f - badge_w, r.min.y + (height(r) - 20.0f) * 0.5f,
		r.max.x - 14.0f, r.min.y + (height(r) + 20.0f) * 0.5f);
	box(b, fade(kDim, 0.12f), 10.0f);
	text_center(font.caption, 9.0f, b, kDim, badge);
}

bool toggle_row(const char* id, const Rect& r, const char* label, const char* hint, bool* value)
{
	ImGui::SetCursorScreenPos(r.min);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(width(r), height(r)));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
	if (clicked)
		*value = !*value;

	if (hovered)
		box(r, kCardSoft, 10.0f);

	text(font.semibold, 14.0f, ImVec2(r.min.x + 14.0f, r.min.y + 9.0f), kText, label);
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect(ImVec2(r.min.x + 14.0f, r.min.y + 26.0f), ImVec2(r.max.x - 70.0f, r.max.y - 4.0f), true);
	text(font.caption, 11.0f, ImVec2(r.min.x + 14.0f, r.min.y + 29.0f), kMuted, hint);
	draw->PopClipRect();

	// Switch.
	const float sw = 42.0f;
	const float sh = 22.0f;
	const Rect track = rect(r.max.x - 14.0f - sw, r.min.y + (height(r) - sh) * 0.5f,
		r.max.x - 14.0f, r.min.y + (height(r) + sh) * 0.5f);
	box(track, *value ? accent_color() : kPanel3, sh * 0.5f);
	const float knob_x = *value ? track.max.x - sh * 0.5f : track.min.x + sh * 0.5f;
	draw->AddCircleFilled(ImVec2(knob_x, track.min.y + sh * 0.5f), sh * 0.5f - 3.0f,
		color_u32(kText), 16);
	return clicked;
}

bool card_button(const char* label, const Rect& r)
{
	ImGui::SetCursorScreenPos(r.min);
	return secondary_button(label, ImVec2(width(r), height(r)));
}

void progress_bar(const Rect& r, float fraction, const ImVec4& color)
{
	box(r, kCardSoft, height(r) * 0.5f);
	const float fill = clampf(fraction, 0.0f, 1.0f);
	if (fill > 0.01f)
		box(rect(r.min.x, r.min.y, r.min.x + width(r) * fill, r.max.y), color, height(r) * 0.5f);
}

void tick_launch()
{
	static int seen = 0;
	static double done_time = -10.0;

	// Clear a finished launch message a few seconds after it appears.
	const int status = sdauth::LaunchStatus();
	if (status != seen)
	{
		seen = status;
		done_time = ImGui::GetTime();
	}
	if ((status == sdauth::StatusOk || status == sdauth::StatusError) && ImGui::GetTime() - done_time > 8.0)
		sdauth::ResetLaunch();
}

} // namespace dash
