#include "common/login_common.h"
#include "../auth/supabase_auth.h"

#include <cstdio>
#include <string>

// Loading screens shown in the login window.
//
//   Startup  runs when the exe starts: connect, build check, update check. When
//            the server version differs from this build, a fourth step
//            "Downloading update..." joins the same list: the bar follows the
//            download, the exe is replaced and the loader restarts after a
//            3, 2, 1 countdown. Maintenance and network errors stop here too,
//            with the server's message and a Retry button.
//   Signing  runs after the Login button: password check, device binding and
//            the security checks. Every step is decided by the server.
//
// This is the only place the loader updates. The dashboard never shows it.

namespace screen_loading {

namespace {

using namespace ui;
using namespace login_ui;

// Slow enough to read every step; the server answer is never the limit.
constexpr double kStartupStepSeconds = 0.75;
// The update step stays visible before the download starts.
constexpr double kUpdateNoticeSeconds = 2.0;
constexpr double kRestartCountdownSeconds = 3.0;
constexpr double kUpdateRetrySeconds = 3.0;
// An optional update that keeps failing must not lock the login form.
constexpr int kOptionalUpdateAttempts = 3;

const char* const kStartupSteps[] = {
	"Connecting to server...",
	"Checking integrity...",
	"Checking for update...",
};

// The same list with the update step, used once the server has a newer build.
const char* const kUpdateSteps[] = {
	"Connecting to server...",
	"Checking integrity...",
	"Checking for update...",
	"Downloading update...",
};
constexpr int kStartupStepCount = 3;

const char* const kLoginSteps[] = {
	"Authenticating...",
	"Verifying account and device...",
	"Running security checks...",
};

struct LoadingState
{
	LoadingAnim startup;
	LoadingAnim signing;
	bool restart_failed = false;
	double update_notice_time = -1.0;
	double update_ready_time = -1.0;
	double update_retry_time = -1.0;
	int update_failures = 0;
};

LoadingState loading;

void reset_update_ui()
{
	loading.restart_failed = false;
	loading.update_notice_time = -1.0;
	loading.update_ready_time = -1.0;
	loading.update_retry_time = -1.0;
	loading.update_failures = 0;
}

Rect action_button()
{
	const float fx = 60.0f;
	return rect(fx, 472.0f, app::kLoginWidth - fx, 530.0f);
}

void quit_after_restart()
{
	loading.restart_failed = !sdauth::RestartLoader();
	if (!loading.restart_failed)
		PostQuitMessage(0);
}

// Drives the update step of the startup list: hold, download, retry, then the
// automatic restart. Returns the caption for the line under the bar. A required
// update retries until it works because the form cannot open before this build
// is replaced; an optional one is dropped by the caller after a few failures.
std::string drive_update(bool& caption_is_error)
{
	const Rect button = action_button();
	const int status = sdauth::UpdateStatus();
	const double now = ImGui::GetTime();
	caption_is_error = false;

	if (status == sdauth::UpdateReady)
	{
		loading.update_retry_time = -1.0;
		if (loading.update_ready_time < 0.0)
		{
			loading.update_ready_time = now;
			loading.restart_failed = false;
		}

		if (loading.restart_failed)
		{
			caption_is_error = true;
			if (GradientButton("##update_restart", button, "Retry restart"))
				quit_after_restart();
			return "The restart did not start. Use the button below.";
		}

		const double elapsed = now - loading.update_ready_time;
		if (elapsed >= kRestartCountdownSeconds)
		{
			quit_after_restart();
			loading.update_ready_time = -1.0;
			return "Update installed. Restarting...";
		}

		int remaining = static_cast<int>(kRestartCountdownSeconds - elapsed + 0.999);
		if (remaining < 1)
			remaining = 1;
		char label[64];
		std::snprintf(label, sizeof(label), "Restarting in %d...", remaining);
		BusyButton(button, label);
		return "Update installed. Restarting...";
	}

	loading.update_ready_time = -1.0;

	if (status == sdauth::UpdateFailed)
	{
		caption_is_error = true;
		if (loading.update_retry_time < 0.0)
			loading.update_retry_time = now + kUpdateRetrySeconds;

		const double wait = loading.update_retry_time - now;
		if (wait <= 0.0)
		{
			loading.update_retry_time = -1.0;
			++loading.update_failures;
			sdauth::StartUpdate();
			BusyButton(button, "Retrying...");
			return "Download did not finish. Retrying...";
		}

		int remaining = static_cast<int>(wait + 0.999);
		if (remaining < 1)
			remaining = 1;
		char label[64];
		std::snprintf(label, sizeof(label), "Retrying in %d...", remaining);
		BusyButton(button, label);
		return "Download did not finish. Retrying...";
	}

	if (status == sdauth::UpdateIdle)
	{
		if (loading.update_notice_time < 0.0)
			loading.update_notice_time = now;

		if (now - loading.update_notice_time >= kUpdateNoticeSeconds)
			sdauth::StartUpdate();

		const std::string version = sdauth::StartupVersion();
		BusyButton(button, "Loading...");
		return version.empty() ? std::string("Update found.") : "Update v" + version + " found.";
	}

	BusyButton(button, "Downloading...");
	const std::string message = sdauth::UpdateMessage();
	return message.empty() ? std::string("Downloading update...") : message;
}

} // namespace

void BeginStartup()
{
	StartAnim(loading.startup);
	reset_update_ui();
	sdauth::StartStartup();
}

Result Startup()
{
	const int status = sdauth::StartupStatus();
	const int stage = sdauth::StartupStageValue();
	const bool failed = status == sdauth::StatusError;
	const bool ready = status == sdauth::StatusOk;

	// The server has a different build, or rejects this one as outdated or
	// modified. A failed check only counts when the update is required.
	const bool update = (ready || (failed && sdauth::StartupUpdateRequired())) &&
		sdauth::StartupUpdateAvailable() && !sdauth::StartupDownloadUrl().empty();

	// An optional update that keeps failing is dropped so login stays open.
	if (update && !sdauth::StartupUpdateRequired() && loading.update_failures >= kOptionalUpdateAttempts &&
		sdauth::UpdateStatus() == sdauth::UpdateFailed)
	{
		sdauth::SkipUpdate();
		return Result::Running;
	}

	const int count = update ? kStartupStepCount + 1 : kStartupStepCount;
	const char* const* steps = update ? kUpdateSteps : kStartupSteps;
	const bool installed = update && sdauth::UpdateStatus() == sdauth::UpdateReady;

	// StartupConnecting (1) is step 0; with an update the last step is the
	// download and every step is done once the new exe is installed.
	int real_active = ready ? kStartupStepCount : stage - 1;
	if (update)
		real_active = installed ? count : kStartupStepCount;
	if (!failed || update)
		Advance(loading.startup, real_active, count, kStartupStepSeconds);

	// The bar follows the download once the update step is active.
	LoadingAnim view = loading.startup;
	if (update && loading.startup.shown >= kStartupStepCount && !installed)
	{
		const float download = static_cast<float>(sdauth::UpdateProgress()) / 100.0f;
		const float fraction = (static_cast<float>(kStartupStepCount) + download) / static_cast<float>(count);
		if (fraction > view.progress)
			view.progress = fraction;
	}

	if (update)
	{
		bool caption_is_error = false;
		std::string caption = "Preparing Cloth Changer...";
		if (loading.startup.shown >= kStartupStepCount)
			caption = drive_update(caption_is_error);
		else
			BusyButton(action_button(), "Loading...");

		DrawLoading(steps, count, view, -1, caption, caption_is_error);
		return Result::Running;
	}

	const int failed_step = failed ? (loading.startup.shown < count ? loading.startup.shown : count - 1) : -1;
	std::string caption = failed ? sdauth::StartupMessage() : std::string("Preparing Cloth Changer...");
	if (caption.empty())
		caption = failed ? "Startup check failed. Retry." : "Preparing Cloth Changer...";

	DrawLoading(steps, count, loading.startup, failed_step, caption, failed);

	if (failed)
	{
		if (GradientButton("##startup_retry", action_button(), "Retry"))
			BeginStartup();
		return Result::Failed;
	}

	BusyButton(action_button(), "Loading...");

	if (Finished(loading.startup))
		return Result::Done;
	return Result::Running;
}

void BeginSigning()
{
	StartAnim(loading.signing);
}

Result Signing()
{
	const int count = static_cast<int>(sizeof(kLoginSteps) / sizeof(kLoginSteps[0]));
	const int status = sdauth::LoginStatus();

	// The caller shows the server's reason on the form. Nothing was unlocked.
	if (status == sdauth::StatusError)
		return Result::Failed;

	const bool ok = status == sdauth::StatusOk && sdauth::HasSession();
	const int real_active = ok ? count : sdauth::LoginStage() - 1;
	Advance(loading.signing, real_active, count);

	DrawLoading(kLoginSteps, count, loading.signing, -1, "Signing in...", false);
	BusyButton(action_button(), "Please wait");

	if (ok && Finished(loading.signing))
		return Result::Done;
	return Result::Running;
}

} // namespace screen_loading
