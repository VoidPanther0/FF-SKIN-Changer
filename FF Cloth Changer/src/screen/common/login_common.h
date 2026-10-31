#pragma once

#include "../../ui.h"

#include <string>

// Pieces shared by the loading and login screens: colors, the card frame with
// logo and title, the step list with progress bar and the gradient buttons.

namespace login_ui {

// ------------------------------------------------------------------ theme ----

inline const ImVec4 kLoginCard(0.020f, 0.031f, 0.067f, 1.0f);
inline const ImVec4 kLoginBorder(0.24f, 0.36f, 0.78f, 0.45f);
inline const ImVec4 kLoginField(0.035f, 0.051f, 0.094f, 1.0f);
inline const ImVec4 kLoginFieldBorder(0.30f, 0.38f, 0.60f, 0.30f);
inline const ImVec4 kLoginIcon(0.62f, 0.68f, 0.82f, 1.0f);
inline const ImVec4 kLoginHint(0.52f, 0.58f, 0.72f, 1.0f);
inline const ImVec4 kLoginBlue(0.17f, 0.33f, 0.86f, 1.0f);
inline const ImVec4 kLoginPurple(0.52f, 0.29f, 0.93f, 1.0f);
inline const ImVec4 kLoginSubtitle(0.36f, 0.53f, 0.95f, 1.0f);
inline const ImVec4 kLoginLink(0.58f, 0.48f, 0.96f, 1.0f);

// ------------------------------------------------------------------- logo ----

// Decodes the embedded Slogo.png (IDR_LOGO) and uploads it as a texture.
// Safe to call again: it does nothing once the texture exists, and retries a
// failed load at most once every few seconds.
void EnsureLogo();
void ReleaseLogo();

// Null while the logo is not loaded. Callers draw a letter instead.
ID3D11ShaderResourceView* Logo();

// Draws the logo centered at (cx, cy) with the given side, or an "S" fallback.
void DrawLogo(ImVec2 center, float side);

// ------------------------------------------------------------------ frame ----

// One-line message under the subtitle (for example "Updated to v1.0.1"). It is
// shown for a few seconds once the form is visible, then fades out.
void SetNotice(const std::string& value);

// Card background, window buttons, logo, title and subtitle.
void DrawFrame(bool form_visible);

// ---------------------------------------------------------------- loading ----

// Visual pacing for a loading screen. The worker reports the real stage; each
// step stays on screen for at least its step time so the user can read it.
struct LoadingAnim
{
	int shown = 0;           // index of the active step; == count when all done
	double step_time = 0.0;  // when the shown step became active
	float progress = 0.0f;
	double done_time = -1.0; // when the bar reached 100%
};

constexpr double kStepSeconds = 0.32;

void StartAnim(LoadingAnim& anim);

// Moves the visible step toward the real one, one step per step_seconds.
// real_active is the index of the step the worker is on; count means all done.
void Advance(LoadingAnim& anim, int real_active, int count, double step_seconds = kStepSeconds);
bool Finished(const LoadingAnim& anim);

// Step list and progress bar. failed_step < 0 means no failure.
void DrawLoading(const char* const* steps, int count, const LoadingAnim& anim, int failed_step,
	const std::string& caption, bool caption_is_error);

// ---------------------------------------------------------------- buttons ----

bool GradientButton(const char* id, const ui::Rect& r, const char* value);

// Faded button shown while a loading screen runs.
void BusyButton(const ui::Rect& r, const char* value);

} // namespace login_ui
