#pragma once

#include "app.h"

#include <string>

// ---------------------------------------------------------------------------
// Shared UI API.
//
// ui.cpp implements this header and owns the app lifecycle. Each screen keeps
// its drawing code in src/screen and includes only this file.
// ---------------------------------------------------------------------------

namespace ui {

// ------------------------------------------------------------------ theme ----

// Base palette. Mutable so the palette can switch between the dark and the
// light theme at runtime; ui::apply_theme() writes these.
inline ImVec4 kBg(0.043f, 0.047f, 0.059f, 1.0f);
inline ImVec4 kTitleBg(0.055f, 0.059f, 0.071f, 1.0f);
inline ImVec4 kPanel(0.075f, 0.082f, 0.098f, 1.0f);
inline ImVec4 kPanel2(0.098f, 0.106f, 0.125f, 1.0f);
inline ImVec4 kPanel3(0.129f, 0.141f, 0.165f, 1.0f);
inline ImVec4 kLine(1.0f, 1.0f, 1.0f, 0.07f);
inline ImVec4 kLineBright(1.0f, 1.0f, 1.0f, 0.13f);
inline ImVec4 kText(0.93f, 0.94f, 0.96f, 1.0f);
inline ImVec4 kMuted(0.55f, 0.58f, 0.65f, 1.0f);
inline ImVec4 kDim(0.38f, 0.40f, 0.46f, 1.0f);
inline const ImVec4 kGreen(0.35f, 0.82f, 0.55f, 1.0f);
inline const ImVec4 kRed(0.86f, 0.32f, 0.38f, 1.0f);
inline const ImVec4 kWhite(1.0f, 1.0f, 1.0f, 1.0f);

float clampf(float value, float low, float high);
ImVec4 lift(const ImVec4& color, float amount);
ImVec4 dim(const ImVec4& color, float amount);
ImVec4 fade(const ImVec4& color, float alpha);
ImU32 color_u32(const ImVec4& color);

// ------------------------------------------------------------------ state ----

struct State
{
	int page = 0;            // dash::Page: the page shown by the dashboard shell
	float accent[3] = { 0.42f, 0.56f, 1.00f };
	float fps = 0.0f;
	bool light = false;      // palette currently applied by ui::apply_theme()
	std::string status = "Ready.";
};

extern State state;

ImVec4 accent_color();

// Loads the dark (false) or light (true) base palette. Callers that need the
// dashboard palette as well use dash::apply_theme(), which calls this.
void apply_theme(bool light);

// ------------------------------------------------------------------ fonts ----

struct Fonts
{
	ImFont* caption = nullptr;
	ImFont* body = nullptr;
	ImFont* semibold = nullptr;
	ImFont* title = nullptr;
};

extern Fonts font;

void load_fonts();

// ------------------------------------------------------------- primitives ----

struct Rect
{
	ImVec2 min;
	ImVec2 max;
};

Rect rect(float x, float y, float x2, float y2);
float width(const Rect& r);
float height(const Rect& r);
ImVec2 center(const Rect& r);

void box(const Rect& r, const ImVec4& fill, float rounding = 10.0f, const ImVec4* border = nullptr);
void gradient_box(const Rect& r, const ImVec4& from, const ImVec4& to, float rounding = 10.0f);
void text(ImFont* f, float size, ImVec2 position, const ImVec4& color, const char* value);
void gradient_text(ImFont* f, float size, ImVec2 position, const char* value, const ImVec4& from, const ImVec4& to);
float text_width(ImFont* f, float size, const char* value);
void text_center(ImFont* f, float size, const Rect& r, const ImVec4& color, const char* value);
void label(const char* value, float x, float y);
void divider(float x1, float x2, float y);
void pill(const Rect& r, const char* value, const ImVec4& color);

// ---------------------------------------------------------------- widgets ----

bool secondary_button(const char* value, const ImVec2& size);
bool window_button(const char* id, const Rect& r, bool close);
void draw_card(const Rect& r, const char* title, const char* description);

// ------------------------------------------------------------------ style ----

void apply_style();
void apply_accent();

} // namespace ui

// ------------------------------------------------------------------ screens ---
//
// One file per screen in src/screen:
//   loading.cpp        startup check and sign-in progress
//   login.cpp          login form
//   dashboard.cpp      window shell: title bar, sidebar, footer, page dispatch
//   home.cpp           the dashboard page

namespace screen_loading {

enum class Result { Running, Failed, Done };

// First loading screen: connect, build check, update check.
void BeginStartup();
Result Startup();

// Second loading screen: password check, device binding, security checks.
void BeginSigning();
Result Signing();

} // namespace screen_loading

namespace screen_login {

// Loads the embedded logo texture, remembered username and starts the startup check.
void Init();

// Releases the logo texture.
void Shutdown();

// Draws the startup, form or signing phase. Shrinks the window on entry.
void Render();

bool IsLoggedIn();

// Clears the session, the password field and returns to the login size.
void SignOut();

// Called when the background auth check reports that the session ended.
void SessionLost();

} // namespace screen_login

namespace screen_dashboard {

// Loads the saved accent color and interface options from settings.ini.
void LoadPreferences();

// Draws the main window: shell plus the current page.
void Render();

} // namespace screen_dashboard
