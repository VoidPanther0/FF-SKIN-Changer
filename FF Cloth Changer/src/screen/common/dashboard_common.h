#pragma once

#include "../../ui.h"
#include "../../auth/supabase_auth.h"

#include <string>
#include <vector>

// Shared pieces of the dashboard: theme colors, layout, icons, small widgets
// and the per-session state. dashboard.cpp draws the window shell, the sidebar
// navigation and the pages without their own file; home.cpp is the only page
// with its own file.

namespace dash {

// ------------------------------------------------------------------ theme ----
//
// Mutable so the palette can switch between dark and light at runtime. Never
// read these during static initialization; call apply_theme() first
// (LoadPreferences does).

inline ImVec4 kHomeBg(0.020f, 0.024f, 0.043f, 1.0f);
inline ImVec4 kSidebar(0.031f, 0.039f, 0.063f, 1.0f);
inline ImVec4 kCard(0.043f, 0.055f, 0.090f, 1.0f);
inline ImVec4 kCardSoft(0.055f, 0.071f, 0.115f, 1.0f);
inline ImVec4 kBorder(0.30f, 0.38f, 0.62f, 0.22f);
inline ImVec4 kNavText(0.55f, 0.61f, 0.76f, 1.0f);
inline const ImVec4 kBlue(0.17f, 0.33f, 0.86f, 1.0f);
inline const ImVec4 kPurple(0.52f, 0.29f, 0.93f, 1.0f);
inline const ImVec4 kOrange(1.00f, 0.62f, 0.30f, 1.0f);

// Paints the dashboard palette for the light or the dark theme, then rebuilds
// the ImGui style on top of it. Safe to call at any time.
void apply_theme(bool light);

// Layout of the shell and the content area. The window is app::kWidth x
// app::kHeight (800x560); everything below fits inside it.
constexpr float kTitleHeight = 64.0f;
constexpr float kSidebarWidth = 232.0f;
constexpr float kContentLeft = 264.0f;
constexpr float kContentRight = 780.0f;
constexpr float kContentTop = 88.0f;
constexpr float kContentBottom = 492.0f;

// Sidebar order. ui::state.page holds one of these. Add a value here when a
// page gets its own file, and a case in dashboard.cpp to draw it.
enum Page
{
	PageHome = 0,
	PageProduct,
	PageGameUpdates,
	PageSocials,
	PageSettings,
	PageCount,
};

struct AccentPreset { const char* name; float r, g, b; };

extern const AccentPreset kAccentPresets[];
extern const int kAccentPresetCount;

// ------------------------------------------------------------------ state ----

// Per-session dashboard state. Reset when a new session opens the dashboard.
struct Shared
{
	// Appearance (persisted).
	bool show_fps = true;
	bool background_glow = true;
	bool card_borders = true;
	bool animations = true;
	bool reduced_effects = false;
	bool light = false;

	// Notifications and launch behaviour (persisted).
	bool toasts = true;
	bool confirm_launch = false;
	bool stop_duplicates = true;

	// Session only.
	ULONGLONG session_start = 0;
	char search[64] = "";
	int selected = 0;
	int selected_local = -1;
	double copied_time = -10.0;
	double diagnostics_time = -10.0;
};

extern Shared shared;

// settings.ini next to the remembered username.
std::wstring settings_path(bool create_directory);
void save_preferences();
void load_preferences();
void reset_preferences();

// Border color for cards; nullptr when borders are switched off.
const ImVec4* card_border();

// ------------------------------------------------------------------ icons ----

void icon_home(ImVec2 c, ImU32 color);
void icon_apps(ImVec2 c, ImU32 color);
void icon_download(ImVec2 c, ImU32 color);
void icon_dll(ImVec2 c, ImU32 color);
void icon_theme(ImVec2 c, ImU32 color);
void icon_account(ImVec2 c, ImU32 color);
void icon_settings(ImVec2 c, ImU32 color);
void icon_about(ImVec2 c, ImU32 color);
void icon_product(ImVec2 c, ImU32 color);
void icon_game_updates(ImVec2 c, ImU32 color);
void icon_updates(ImVec2 c, ImU32 color);
void icon_socials(ImVec2 c, ImU32 color);
void icon_logout(ImVec2 c, ImU32 color);
void icon_search(ImVec2 c, ImU32 color);

void icon_components(ImVec2 c, ImU32 color); // stat: components
void icon_cube(ImVec2 c, ImU32 color);       // stat: build
void icon_crown(ImVec2 c, ImU32 color);      // stat: access
void icon_clock(ImVec2 c, ImU32 color);      // generic: time
void icon_bolt(ImVec2 c, ImU32 color);       // section: quick access
void icon_activity(ImVec2 c, ImU32 color);   // generic: pulse
void icon_server(ImVec2 c, ImU32 color);     // generic: server/api
void icon_plus(ImVec2 c, ImU32 color);
void icon_refresh(ImVec2 c, ImU32 color);
void icon_folder(ImVec2 c, ImU32 color);
void icon_play(ImVec2 c, ImU32 color);
void icon_trash(ImVec2 c, ImU32 color);

// Right-pointing arrow glyph used by cards and buttons.
void icon_arrow(ImVec2 c, ImU32 color);

// ---------------------------------------------------------------- helpers ----

// Draws text wrapped at wrap_width and returns its height.
float text_wrapped(ImFont* f, float size, ImVec2 position, const ImVec4& color, const char* value, float wrap_width);

// Small square avatar with the first letter of a name.
void letter_badge(const ui::Rect& r, const std::string& name, float size);

bool contains_ci(const std::string& haystack, const char* needle);

// Component state: online, updating, offline or beta.
ImVec4 status_color(const std::string& status);
std::string status_label(const std::string& status);
bool component_launchable(const sdauth::Component& component);

// Days left until the account expires, or -1 for no expiry.
int access_days_left();
std::string access_date();

void open_url(const std::string& url);
void open_data_folder();
std::string short_hwid();
std::string session_time();

// Plain text a user can paste to support. No password, token or full device ID.
std::string diagnostics_text();

// --------------------------------------------------------- server status ----

// "Connected" only while this session really holds a server answer; otherwise
// the reason is shown. Never reports a connection that did not happen.
const char* server_state_text();
ImVec4 server_state_color();

// Cached component file in %APPDATA%\SachinDevLoader\components, or empty.
std::wstring component_cache_path(const std::string& name);
// Size in bytes of the cached file, or -1 when it is not downloaded.
long long component_cache_size(const std::string& name);
std::string format_size(long long bytes);

// Deletes the cached file of one component. False when it could not be removed.
bool clear_component_cache(const std::string& name);

// --------------------------------------------------------- local apps -------
//
// Executables the user picked himself. Stored in settings.ini; nothing is
// downloaded and nothing but the configured file is ever started.

struct LocalApp
{
	std::string name;
	std::string path;    // UTF-8, absolute path to the executable
};

// Reloads the list from settings.ini.
void reload_local_apps();
const std::vector<LocalApp>& local_apps();

// Validates the path and stores the executable. False with a reason on error.
bool add_local_app(const std::wstring& path, std::string& error);
void remove_local_app(int index);

// True while the configured executable already runs (and the option is on).
bool local_app_running(int index);

// Starts the configured executable. Fails with a clear reason on a missing
// file, a duplicate launch or a start error. Never runs anything else.
bool launch_local_app(int index, std::string& error);

// -------------------------------------------------------------- toasts -----

void toast(const char* message);
void toast_error(const char* message);
void draw_toasts(); // shell draws this last each frame

// Modal confirmation. Open with ImGui::OpenPopup(id); true when confirmed.
bool confirm_modal(const char* id, const char* title, const char* body, const char* confirm_label);

// ---------------------------------------------------------------- widgets ----

// Icon drawing signature shared by the sidebar and the page widgets.
using IconFn = void (*)(ImVec2, ImU32);

// Small animated (or static when animations are off) three-dot loader.
void spinner(ImVec2 center, const ImVec4& color);

// Section header inside a card: icon, title.
void section_header(const ui::Rect& card, const char* title, IconFn icon);

// Gradient primary button with a leading icon and a trailing arrow.
bool primary_button(const char* id, const ui::Rect& r, const char* label, IconFn icon = nullptr);

// Pill showing a short state word in the given color.
void status_badge(const ui::Rect& r, const char* value, const ImVec4& color);

// Row used by settings: label on the left, value on the right, divider below.
void info_row(const ui::Rect& card, float y, const char* label, const char* value, const ImVec4& color);

// Settings row that is not implemented in this build. Clearly separated from
// finished options so nothing pretends to work.
void placeholder_row(const ui::Rect& r, const char* label, const char* hint);

void draw_empty_state(const ui::Rect& area, const char* title, const char* subtitle);
bool text_link(const char* id, const char* value, ImVec2 position);
void stat_card(const ui::Rect& r, const char* label, const char* value, const char* hint, const ImVec4& color,
	IconFn icon);
void draw_search_box(const ui::Rect& r, const char* hint);

// Card with a title and an optional one-line description.
void card_frame(const ui::Rect& r, const char* title, const char* description);

// Switch with a label and a hint. Returns true when toggled.
bool toggle_row(const char* id, const ui::Rect& r, const char* label, const char* hint, bool* value);

// Secondary button that fills the given rect.
bool card_button(const char* label, const ui::Rect& r);

// Thin progress bar in dashboard colors.
void progress_bar(const ui::Rect& r, float fraction, const ImVec4& color);

// ------------------------------------------------------------------ pages ----

void DrawHome();

// Called once per frame by the shell; ends a finished launch message.
void tick_launch();

} // namespace dash
