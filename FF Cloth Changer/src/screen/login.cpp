#include "common/login_common.h"
#include "../auth/supabase_auth.h"
#include "imgui_internal.h"

#include <cstdio>
#include <string>

// Login form. The startup check and the sign-in progress live in loading.cpp;
// this file owns the form, the remembered username and the sign-in flow.

namespace screen_login {

namespace {

using namespace ui;
using namespace login_ui;

// Startup check, then the form, then the login check. The dashboard opens only
// after the server answered every login step.
enum class Phase { Startup, Form, Signing };

struct LoginState
{
	char username[64] = "";
	char password[64] = "";
	bool show_password = false;
	bool remember = false;
	bool logged_in = false;
	bool focus_username = true;
	bool focus_password = false;
	std::string message;
	bool message_is_error = false;
	Phase phase = Phase::Startup;
};

LoginState login;

// Remembering a username only. Passwords and session tokens never touch disk.
std::wstring login_settings_path(bool create_directory)
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

void load_login_settings()
{
	const std::wstring path = login_settings_path(false);
	if (path.empty())
		return;

	wchar_t stored[64] = {};
	GetPrivateProfileStringW(L"login", L"username", L"", stored, 64, path.c_str());
	if (stored[0] == L'\0')
		return;

	char username[64] = {};
	WideCharToMultiByte(CP_UTF8, 0, stored, -1, username, static_cast<int>(sizeof(username)), nullptr, nullptr);
	if (username[0] == '\0')
		return;

	std::snprintf(login.username, sizeof(login.username), "%s", username);
	login.remember = true;
	login.focus_username = false;
	login.focus_password = true;
}

void store_remembered_username(const char* username, bool remember)
{
	const std::wstring path = login_settings_path(true);
	if (path.empty())
		return;

	if (!remember)
	{
		WritePrivateProfileStringW(L"login", L"username", nullptr, path.c_str());
		return;
	}

	const int length = MultiByteToWideChar(CP_UTF8, 0, username, -1, nullptr, 0);
	if (length <= 1)
		return;

	std::wstring wide(static_cast<std::size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, username, -1, &wide[0], length);
	wide.resize(static_cast<std::size_t>(length - 1));

	WritePrivateProfileStringW(L"login", L"username", wide.c_str(), path.c_str());
}

void icon_user(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddCircle(ImVec2(c.x, c.y - 5.0f), 5.0f, color, 24, 1.7f);
	draw->PathArcTo(ImVec2(c.x, c.y + 11.0f), 9.0f, IM_PI, IM_PI * 2.0f, 20);
	draw->PathStroke(color, ImDrawFlags_Closed, 1.7f);
}

void icon_lock(ImVec2 c, ImU32 color)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PathArcTo(ImVec2(c.x, c.y - 4.0f), 5.0f, IM_PI, IM_PI * 2.0f, 16);
	draw->PathStroke(color, 0, 1.7f);
	draw->AddLine(ImVec2(c.x - 5.0f, c.y - 4.0f), ImVec2(c.x - 5.0f, c.y), color, 1.7f);
	draw->AddLine(ImVec2(c.x + 5.0f, c.y - 4.0f), ImVec2(c.x + 5.0f, c.y), color, 1.7f);
	draw->AddRect(ImVec2(c.x - 8.0f, c.y), ImVec2(c.x + 8.0f, c.y + 11.0f), color, 2.5f, 0, 1.7f);
}

void icon_eye(ImVec2 c, ImU32 color, bool crossed)
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddBezierQuadratic(ImVec2(c.x - 11.0f, c.y), ImVec2(c.x, c.y - 10.0f), ImVec2(c.x + 11.0f, c.y), color, 1.7f);
	draw->AddBezierQuadratic(ImVec2(c.x - 11.0f, c.y), ImVec2(c.x, c.y + 10.0f), ImVec2(c.x + 11.0f, c.y), color, 1.7f);
	draw->AddCircle(c, 3.5f, color, 16, 1.7f);
	if (crossed)
		draw->AddLine(ImVec2(c.x - 9.0f, c.y + 7.0f), ImVec2(c.x + 9.0f, c.y - 7.0f), color, 1.7f);
}

enum class FieldIcon { User, Lock };

// Rounded field with a leading icon. Returns true when Enter is pressed.
bool login_field(const char* id, const Rect& r, char* buffer, size_t size, const char* hint,
	FieldIcon icon, bool* reveal, bool focus)
{
	box(r, kLoginField, 10.0f);

	const float icon_x = r.min.x + 30.0f;
	const float mid_y = r.min.y + height(r) * 0.5f;
	const ImU32 icon_color = color_u32(kLoginIcon);
	if (icon == FieldIcon::User)
		icon_user(ImVec2(icon_x, mid_y), icon_color);
	else
		icon_lock(ImVec2(icon_x, mid_y), icon_color);

	const float font_size = 18.0f;
	const float input_x = r.min.x + 56.0f;
	const float input_w = width(r) - 56.0f - (reveal ? 56.0f : 18.0f);

	ImGui::PushFont(font.body, font_size);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, (height(r) - font_size) * 0.5f - 1.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_TextDisabled, kLoginHint);

	ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue;
	if (reveal && !*reveal)
		flags |= ImGuiInputTextFlags_Password;

	if (focus)
		ImGui::SetKeyboardFocusHere();
	ImGui::SetCursorScreenPos(ImVec2(input_x, r.min.y));
	ImGui::SetNextItemWidth(input_w);
	const bool submitted = ImGui::InputTextWithHint(id, hint, buffer, size, flags);
	const bool active = ImGui::IsItemActive();
	const bool hovered = ImGui::IsItemHovered();

	ImGui::PopStyleColor(4);
	ImGui::PopStyleVar();
	ImGui::PopFont();

	if (reveal)
	{
		const Rect eye = rect(r.max.x - 50.0f, r.min.y + 6.0f, r.max.x - 8.0f, r.max.y - 6.0f);
		ImGui::SetCursorScreenPos(eye.min);
		ImGui::PushID(id);
		if (ImGui::InvisibleButton("##reveal", ImVec2(width(eye), height(eye))))
			*reveal = !*reveal;
		const bool eye_hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		if (eye_hovered)
			ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		icon_eye(center(eye), color_u32(eye_hovered ? kText : kLoginIcon), *reveal);
	}

	const ImVec4 border = active ? fade(kLoginPurple, 0.85f) : (hovered ? fade(kLoginBorder, 0.9f) : kLoginFieldBorder);
	ImGui::GetWindowDrawList()->AddRect(r.min, r.max, color_u32(border), 10.0f, 0, active ? 1.5f : 1.0f);
	return submitted;
}

bool login_checkbox(const char* id, ImVec2 position, const char* value, bool* checked)
{
	const float side = 22.0f;
	const float label_w = text_width(font.body, 17.0f, value);

	ImGui::SetCursorScreenPos(position);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(side + 12.0f + label_w, side));
	const bool hovered = ImGui::IsItemHovered();
	if (clicked)
		*checked = !*checked;
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	ImDrawList* draw = ImGui::GetWindowDrawList();
	const Rect r = rect(position.x, position.y, position.x + side, position.y + side);
	if (*checked)
	{
		gradient_box(r, kLoginBlue, kLoginPurple, 5.0f);
		draw->AddLine(ImVec2(r.min.x + 6.0f, r.min.y + 11.5f), ImVec2(r.min.x + 9.5f, r.min.y + 15.0f), IM_COL32_WHITE, 2.0f);
		draw->AddLine(ImVec2(r.min.x + 9.5f, r.min.y + 15.0f), ImVec2(r.min.x + 16.5f, r.min.y + 7.5f), IM_COL32_WHITE, 2.0f);
	}
	else
	{
		box(r, kLoginField, 5.0f);
		draw->AddRect(r.min, r.max, color_u32(hovered ? kLoginBorder : fade(kLoginIcon, 0.55f)), 5.0f, 0, 1.4f);
	}

	text(font.body, 17.0f, ImVec2(r.max.x + 12.0f, position.y), hovered ? kText : lift(kLoginIcon, 0.18f), value);
	return clicked;
}

bool login_link(const char* id, ImVec2 position, const char* value)
{
	const float size = 17.0f;
	const float w = text_width(font.body, size, value);

	ImGui::SetCursorScreenPos(position);
	const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, size + 4.0f));
	const bool hovered = ImGui::IsItemHovered();
	if (hovered)
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

	const ImVec4 color = hovered ? lift(kLoginLink, 0.12f) : kLoginLink;
	text(font.body, size, position, color, value);
	if (hovered)
		ImGui::GetWindowDrawList()->AddLine(ImVec2(position.x, position.y + size + 2.0f),
			ImVec2(position.x + w, position.y + size + 2.0f), color_u32(fade(color, 0.7f)), 1.0f);
	return clicked;
}

void submit_login()
{
	if (sdauth::LoginStatus() == sdauth::StatusWorking)
		return;

	const std::string username = sdauth::NormalizeUsername(login.username);
	std::string error;

	if (!sdauth::ValidateUsername(username, error))
	{
		login.message = error;
		login.message_is_error = true;
		login.focus_username = true;
		return;
	}

	if (login.password[0] == '\0')
	{
		login.message = "Enter your password.";
		login.message_is_error = true;
		login.focus_password = true;
		return;
	}

	std::snprintf(login.username, sizeof(login.username), "%s", username.c_str());
	login.message.clear();
	login.message_is_error = false;

	sdauth::StartLogin(username, login.password);
	if (sdauth::LoginStatus() == sdauth::StatusWorking)
	{
		login.phase = Phase::Signing;
		screen_loading::BeginSigning();
	}
	else
	{
		login.message = sdauth::LoginMessage();
		if (login.message.empty())
			login.message = "Login could not start. Try again.";
		login.message_is_error = true;
		sdauth::ResetStatus();
	}
}

void finish_login()
{
	const sdauth::Session session = sdauth::GetSession();
	login.logged_in = true;
	login.phase = Phase::Form;
	login.message.clear();
	login.message_is_error = false;
	SecureZeroMemory(login.password, sizeof(login.password));
	login.show_password = false;
	store_remembered_username(session.username.c_str(), login.remember);
	state.page = 0;
	state.status = std::string("Signed in as ") + session.username + ".";
	sdauth::ResetStatus();
	ResizeWindow(static_cast<int>(app::kWidth), static_cast<int>(app::kHeight));
}

// ------------------------------------------------------------------- form ----

void draw_form()
{
	const float w = app::kLoginWidth;
	const float cx = w * 0.5f;

	// Fields.
	const float fx = 60.0f;
	const float fw = w - 120.0f;

	const bool focus_user = login.focus_username;
	login.focus_username = false;
	if (login_field("##username", rect(fx, 258.0f, fx + fw, 316.0f), login.username, sizeof(login.username),
		"Username", FieldIcon::User, nullptr, focus_user))
		login.focus_password = true;

	const bool focus_pass = login.focus_password;
	login.focus_password = false;
	const bool enter = login_field("##password", rect(fx, 332.0f, fx + fw, 390.0f), login.password,
		sizeof(login.password), "Password", FieldIcon::Lock, &login.show_password, focus_pass);

	login_checkbox("##remember", ImVec2(fx, 410.0f), "Remember username", &login.remember);

	const char* forgot = "Forgot password?";
	if (login_link("##forgot", ImVec2(fx + fw - text_width(font.body, 17.0f, forgot), 410.0f), forgot))
	{
		login.message = "Ask the admin to reset your password.";
		login.message_is_error = false;
	}

	if (!login.message.empty())
	{
		const ImVec4 color = login.message_is_error ? kRed : kLoginSubtitle;
		const float mw = text_width(font.body, 15.0f, login.message.c_str());
		text(font.body, 15.0f, ImVec2(cx - mw * 0.5f, 446.0f), color, login.message.c_str());
	}

	const bool submitted = GradientButton("##login", rect(fx, 472.0f, fx + fw, 530.0f), "Login");
	if (submitted || enter)
		submit_login();
}

void draw_login_screen()
{
	DrawFrame(login.phase == Phase::Form);

	switch (login.phase)
	{
	case Phase::Startup:
		if (screen_loading::Startup() == screen_loading::Result::Done)
			login.phase = Phase::Form;
		break;

	case Phase::Signing:
	{
		const screen_loading::Result result = screen_loading::Signing();
		if (result == screen_loading::Result::Failed)
		{
			// Back to the form with the server's reason. Nothing was unlocked.
			login.phase = Phase::Form;
			login.message = sdauth::LoginMessage();
			if (login.message.empty())
				login.message = "Sign-in failed. Try again.";
			login.message_is_error = true;
			login.focus_password = true;
			SecureZeroMemory(login.password, sizeof(login.password));
			sdauth::ResetStatus();
		}
		else if (result == screen_loading::Result::Done)
		{
			finish_login();
		}
		break;
	}

	default:
		draw_form();
		break;
	}
}

} // namespace

void Init()
{
	EnsureLogo();
	load_login_settings();

	if (g_updated)
	{
		char notice[64];
		std::snprintf(notice, sizeof(notice), "Updated to v%s", app::kVersion);
		SetNotice(notice);
	}

	// First loading screen: build check and components before the form.
	login.phase = Phase::Startup;
	screen_loading::BeginStartup();
}

void Shutdown()
{
	ReleaseLogo();
}

void Render()
{
	draw_login_screen();
}

bool IsLoggedIn()
{
	return login.logged_in;
}

void SignOut()
{
	sdauth::SignOut();
	sdauth::ResetStatus();

	login.logged_in = false;
	login.message.clear();
	login.message_is_error = false;
	SecureZeroMemory(login.password, sizeof(login.password));
	login.show_password = false;
	login.focus_username = true;
	login.focus_password = false;
	state.page = 0;

	ResizeWindow(static_cast<int>(app::kLoginWidth), static_cast<int>(app::kLoginHeight));
}

void SessionLost()
{
	login.logged_in = false;
	login.message = sdauth::LoginStatus() == sdauth::StatusError
		? sdauth::LoginMessage()
		: "Session ended. Sign in again.";
	login.message_is_error = true;
	sdauth::ResetStatus();
	SecureZeroMemory(login.password, sizeof(login.password));
	login.show_password = false;
	login.focus_username = true;
	state.page = 0;
	ResizeWindow(static_cast<int>(app::kLoginWidth), static_cast<int>(app::kLoginHeight));
}

} // namespace screen_login
