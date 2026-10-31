#pragma once

// Supabase-backed loader authentication.
// Login rule: username + password.
//   The client only calls the server-side Edge Function "loader-auth".
//   The function verifies the password with Supabase Auth, then calls
//   account_login() to bind the HWID, check bans and record the login.
//   At startup a "startup" call sends the client version and the SHA-256 of
//   the running executable; loader_startup() rejects old or modified builds
//   and returns the component list plus the latest release metadata. After the
//   password check a "bootstrap" call repeats those checks with the user token
//   (loader_bootstrap()). No password grant or service key is used by the
//   client itself. Network calls run on worker threads; the UI only polls
//   status.

#include <string>
#include <vector>

namespace sdauth {

enum Status {
	StatusIdle = 0,
	StatusWorking = 1,
	StatusOk = 2,
	StatusError = 3,
};

// Startup progress (first loading screen, before the login form).
enum StartupStage {
	StartupNone = 0,
	StartupConnecting = 1,   // reach the loader-auth function
	StartupIntegrity = 2,    // client version, executable hash, maintenance
	StartupUpdateCheck = 3,  // latest release metadata
	StartupComponents = 4,   // component list released by the server
	StartupDone = 5,
};

// Automatic update state when the server rejects this build or reports a newer
// version on a successful check.
enum UpdateState {
	UpdateIdle = 0,
	UpdateDownloading = 1,
	UpdateReady = 2,   // new exe installed; waiting for the restart countdown
	UpdateFailed = 3,
	UpdateSkipped = 4, // server points at the running build; continue startup
};

// Login progress (second loading screen, after the Login button).
// Every stage is decided by the server, not by the client.
enum Stage {
	StageNone = 0,
	StageAuthenticating = 1, // password check against Supabase Auth
	StageAccount = 2,        // ban, HWID binding and rate limit (account_login)
	StageSecurity = 3,       // account, HWID, version and executable hash again
	StageDone = 4,
};

struct Component {
	std::string name;
	std::string version;
	std::string description;
	std::string status = "online"; // online, updating, offline or beta
	std::string download_url;      // empty until signed in, or when not launchable
	std::string sha256;
};

struct Session {
	bool valid = false;
	std::string username;
	std::string access_token;
	std::string refresh_token;
	long long expires_at = 0; // unix seconds
};

// Computes the hardware id once and stores it. Safe to call more than once.
void Init();

// Starts the startup check on a worker thread: version, executable hash,
// maintenance flag and the component list. Safe to call again to retry.
void StartStartup();
int StartupStatus();
int StartupStageValue();
std::string StartupMessage();

// Re-runs the public startup check and updates the update information only.
// The component list of the signed-in session is NOT touched, so this is safe
// to call after sign-in (for example from an update button).
void StartUpdateCheck();

// ---------------------------------------------------------------- refresh ----

// Re-runs the full bootstrap with the stored session and replaces the
// component list with the signed-in answer. Only while a session exists.
void StartComponentRefresh();
int RefreshStatus();
std::string RefreshMessage();

// Direct executable URL and SHA-256 sent by the server when this build is
// rejected. ReleaseUrl is only a fallback page. Empty when unavailable.
std::string StartupDownloadUrl();
std::string StartupReleaseUrl();
std::string StartupSha256();

// The one version the server runs (loader_config.version). Empty when the
// server sends none.
std::string StartupVersion();

// True when the server version is newer than this executable and the server sent a
// download URL with a SHA-256. The startup screen then updates without a click.
bool StartupUpdateAvailable();

// True when the server rejected this build as outdated or modified, so the
// update must finish before the login form opens.
bool StartupUpdateRequired();

// Downloads the update, verifies its SHA-256, swaps it in place of the running
// exe (the old one becomes <exe>.old) and restarts automatically.
void StartUpdate();

// Gives up on an optional update after repeated failures so startup can finish.
void SkipUpdate();

// Starts the swapped exe after UpdateReady. The caller then quits.
bool RestartLoader();
int UpdateStatus();
int UpdateProgress();
std::string UpdateMessage();

// Call every frame from the render loop. Refreshes the token and re-checks the
// account in the background so a banned or moved account loses its session.
void Tick();

// Starts the login flow on a worker thread. Credentials stay in memory only.
void StartLogin(const std::string& username, const std::string& password);

// Status of the last login attempt.
int LoginStatus();
std::string LoginMessage();

// Current Stage of the running or last login attempt.
int LoginStage();

// Components the server released after the bootstrap checks passed.
std::vector<Component> Components();

// Unix seconds when this account's access ends. 0 means no expiry.
long long AccountExpiresAt();

// Downloads a component (SHA-256 checked, cached in %APPDATA%\SachinDevLoader
// \components) and starts it. Runs on a worker thread; poll the status below.
// Returns false when a launch is already running or the component cannot start.
bool LaunchComponent(const std::string& name);
int LaunchStatus();      // Status: Idle, Working, Ok (started) or Error
int LaunchProgress();    // 0 to 100 while downloading
std::string LaunchName();
std::string LaunchMessage();
void ResetLaunch();      // back to Idle once the UI has shown the result

// Stops a running component download. The worker then reports the cancelled
// state through LaunchStatus()/LaunchMessage().
void CancelLaunch();

// Reports a client-side validation failure without touching the network.
void ReportLocalError(const std::string& message);

// Clears StatusOk or StatusError so the UI reacts once.
void ResetStatus();

bool HasSession();
Session GetSession();
void SignOut();

// Stable machine fingerprint used for account binding.
const std::string& MachineHwid();

// Usernames are lowercase letters, digits and underscore, 2 to 20 characters.
bool ValidateUsername(const std::string& username, std::string& error);

// Trims whitespace and lowercases a username before validation.
std::string NormalizeUsername(const std::string& username);

} // namespace sdauth
