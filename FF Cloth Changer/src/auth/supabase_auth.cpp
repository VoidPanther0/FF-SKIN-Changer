#include "supabase_auth.h"
#include "json_lite.h"
#include "app.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <atomic>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace {

// ---------------------------------------------------------------- config ----

const wchar_t* kBaseUrl = L"https://xdtidxtkvrmurzogkxuv.supabase.co";
const wchar_t* kAuthFunctionPath = L"/functions/v1/loader-auth";
const char* kPublishableKey = "sb_publishable_xoEwnFlQuy-LLO5h8CRW_w_lS_uiEYr";
const std::string kUserAgent = std::string("SachinDevLoader/") + app::kVersion;

const int kHeartbeatSeconds = 300;
const std::size_t kMaxResponseBytes = 256 * 1024;

// ----------------------------------------------------------------- state ----

std::mutex g_mutex;
std::atomic<int> g_status(static_cast<int>(sdauth::StatusIdle));
std::atomic<bool> g_busy(false);
std::atomic<long long> g_next_check(0);
std::atomic<int> g_stage(static_cast<int>(sdauth::StageNone));
std::string g_message;
sdauth::Session g_session;
std::vector<sdauth::Component> g_components;
std::atomic<long long> g_expires_at(0);
std::atomic<int> g_launch_status(static_cast<int>(sdauth::StatusIdle));
std::atomic<int> g_launch_progress(0);
std::atomic<bool> g_launch_cancel(false);
std::atomic<bool> g_launch_busy(false);
std::string g_launch_name;
std::string g_launch_message;
std::string g_hwid;
std::string g_exe_hash;
std::atomic<int> g_startup_status(static_cast<int>(sdauth::StatusIdle));
std::atomic<int> g_startup_stage(static_cast<int>(sdauth::StartupNone));
std::atomic<bool> g_startup_busy(false);
std::string g_startup_message;
std::string g_update_url;
std::string g_release_url;
std::string g_update_sha;
std::string g_server_version;
std::atomic<bool> g_update_available(false);
std::atomic<bool> g_update_required(false);
std::atomic<bool> g_update_busy(false);
std::atomic<int> g_update_status(static_cast<int>(sdauth::UpdateIdle));
std::atomic<int> g_update_progress(0);
std::string g_update_message;
// Path of the swapped loader, started again by RestartLoader().
std::wstring g_update_target;
// Manual component refresh from the dashboard.
std::atomic<int> g_refresh_status(static_cast<int>(sdauth::StatusIdle));
std::atomic<bool> g_refresh_busy(false);
std::string g_refresh_message;

bool IsSha256Hex(const std::string& value);

// Parses up to four dot-separated numbers, e.g. "1.2.10" -> {1,2,10,0}.
bool ParseVersion(const std::string& text, int parts[4]) {
	parts[0] = 0;
	parts[1] = 0;
	parts[2] = 0;
	parts[3] = 0;

	std::size_t index = 0;
	int count = 0;
	while (index < text.size() && count < 4) {
		if (!std::isdigit(static_cast<unsigned char>(text[index])))
			return false;

		int value = 0;
		int digits = 0;
		while (index < text.size() && std::isdigit(static_cast<unsigned char>(text[index]))) {
			value = value * 10 + (text[index] - '0');
			++digits;
			++index;
			if (digits > 4)
				return false;
		}

		parts[count++] = value;
		if (index == text.size())
			break;
		if (text[index] != '.')
			return false;
		++index;
		if (index == text.size())
			return false;
	}

	return count > 0 && index == text.size();
}

// True when candidate is newer than current.
bool VersionNewer(const std::string& candidate, const std::string& current) {
	int a[4];
	int b[4];
	if (!ParseVersion(candidate, a) || !ParseVersion(current, b))
		return false;

	for (int i = 0; i < 4; ++i) {
		if (a[i] != b[i])
			return a[i] > b[i];
	}
	return false;
}

long long NowSeconds() {
	return static_cast<long long>(std::time(nullptr));
}

void SetStatus(int status, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_message = message;
	}
	g_status.store(status);
}

void SetSession(const sdauth::Session& session) {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_session = session;
}

void SetStage(int stage) {
	g_stage.store(stage);
}

void SetComponents(const std::vector<sdauth::Component>& components) {
	std::lock_guard<std::mutex> lock(g_mutex);
	g_components = components;
}

void SetUpdateInfo(const std::string& update_url, const std::string& release_url, const std::string& sha256,
	const std::string& server_version, bool required) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_update_url = update_url;
		g_release_url = release_url;
		g_update_sha = sha256;
		g_server_version = server_version;
		g_update_message.clear();
	}
	g_update_progress.store(0);
	g_update_status.store(static_cast<int>(sdauth::UpdateIdle));
	const bool usable = !update_url.empty() && IsSha256Hex(sha256);
	g_update_required.store(usable && required);
	g_update_available.store(usable && (required || VersionNewer(server_version, app::kVersion)));
}

void SetUpdate(int status, int progress, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_update_message = message;
	}
	g_update_progress.store(progress);
	g_update_status.store(status);
}

// Zeros sensitive buffers on every return path.
struct ZeroOnExit {
	explicit ZeroOnExit(std::string& value) : value_(value) {}

	~ZeroOnExit() {
		if (!value_.empty())
			SecureZeroMemory(value_.data(), value_.size());
	}

	std::string& value_;
};

// ----------------------------------------------------------------- utf-8 ----

std::string WideToUtf8(const std::wstring& text) {
	if (text.empty())
		return std::string();

	const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<std::size_t>(size), '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size, nullptr, nullptr);
	return out;
}

std::wstring Utf8ToWide(const std::string& text) {
	if (text.empty())
		return std::wstring();

	const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	std::wstring out(static_cast<std::size_t>(size), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], size);
	return out;
}

std::string JsonEscape(const std::string& text) {
	std::string out;
	out.reserve(text.size() + 8);

	for (const char raw : text) {
		const unsigned char c = static_cast<unsigned char>(raw);
		switch (c) {
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20) {
				char buffer[8];
				sprintf_s(buffer, "\\u%04x", c);
				out += buffer;
			}
			else {
				out += raw;
			}
		}
	}
	return out;
}

// ---------------------------------------------------------------- hashing ----

std::string Sha256Hex(const std::string& data) {
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	BCRYPT_HASH_HANDLE hash = nullptr;
	std::string result;

	if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
		return result;

	DWORD object_size = 0;
	DWORD hash_size = 0;
	DWORD written = 0;

	const bool sized =
		BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size), &written, 0) == 0 &&
		BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hash_size), sizeof(hash_size), &written, 0) == 0;

	if (sized) {
		std::vector<unsigned char> object(object_size);
		std::vector<unsigned char> digest(hash_size);

		if (BCryptCreateHash(algorithm, &hash, &object[0], object_size, nullptr, 0, 0) == 0) {
			if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())), static_cast<ULONG>(data.size()), 0) == 0 &&
				BCryptFinishHash(hash, &digest[0], hash_size, 0) == 0) {
				static const char* digits = "0123456789abcdef";
				result.reserve(digest.size() * 2);
				for (const unsigned char byte : digest) {
					result += digits[byte >> 4];
					result += digits[byte & 0x0F];
				}
			}
			BCryptDestroyHash(hash);
		}
	}

	BCryptCloseAlgorithmProvider(algorithm, 0);
	return result;
}

// SHA-256 of the running executable. The server compares it with the list of
// released builds, so a patched binary fails the bootstrap check.
const std::string& ExecutableHash() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_exe_hash.empty())
		return g_exe_hash;

	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return g_exe_hash;

	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return g_exe_hash;

	LARGE_INTEGER size = {};
	std::string bytes;
	if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 512LL * 1024 * 1024) {
		bytes.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD read = 0;
		if (!ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size())
			bytes.clear();
	}
	CloseHandle(file);

	if (!bytes.empty())
		g_exe_hash = Sha256Hex(bytes);
	return g_exe_hash;
}

// ------------------------------------------------------------------- http ----

struct HttpResponse {
	bool transport_ok = false;
	int status = 0;
	std::string body;
	std::string error;
};

HttpResponse HttpPost(const std::wstring& url, const std::vector<std::wstring>& headers, const std::string& body) {
	HttpResponse result;

	URL_COMPONENTS parts;
	ZeroMemory(&parts, sizeof(parts));
	parts.dwStructSize = sizeof(parts);

	wchar_t host[256] = {};
	wchar_t path[2048] = {};
	parts.lpszHostName = host;
	parts.dwHostNameLength = 255;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 2047;

	if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts)) {
		result.error = "Bad server URL";
		return result;
	}

	const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
	const std::wstring agent = Utf8ToWide(kUserAgent);

	HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session) {
		result.error = "WinHTTP is not available";
		return result;
	}

	WinHttpSetTimeouts(session, 8000, 8000, 8000, 20000);

	if (secure) {
		DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
		WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
	}

	HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
	if (!connection) {
		WinHttpCloseHandle(session);
		result.error = "Cannot reach the server";
		return result;
	}

	HINTERNET request = WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0);
	if (!request) {
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		result.error = "Cannot create the request";
		return result;
	}

	std::wstring header_block;
	for (const std::wstring& header : headers) {
		header_block += header;
		header_block += L"\r\n";
	}

	const bool sent = WinHttpSendRequest(request, header_block.c_str(), static_cast<DWORD>(header_block.size()),
		body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
		static_cast<DWORD>(body.size()), 0) != FALSE;

	if (sent && WinHttpReceiveResponse(request, nullptr)) {
		DWORD status_code = 0;
		DWORD size = sizeof(status_code);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);

		for (;;) {
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
				break;

			if (result.body.size() + available > kMaxResponseBytes)
				available = static_cast<DWORD>(kMaxResponseBytes - result.body.size());
			if (available == 0)
				break;

			std::string chunk(available, '\0');
			DWORD read = 0;
			if (!WinHttpReadData(request, &chunk[0], available, &read))
				break;

			chunk.resize(read);
			result.body += chunk;
		}

		result.transport_ok = true;
		result.status = static_cast<int>(status_code);
	}
	else {
		result.error = "Cannot reach the server";
	}

	WinHttpCloseHandle(request);
	WinHttpCloseHandle(connection);
	WinHttpCloseHandle(session);
	return result;
}

std::wstring ApiUrl(const wchar_t* path) {
	return std::wstring(kBaseUrl) + path;
}

std::vector<std::wstring> FunctionHeaders() {
	std::vector<std::wstring> headers;
	headers.push_back(L"apikey: " + Utf8ToWide(kPublishableKey));
	headers.push_back(L"Content-Type: application/json");
	headers.push_back(L"Accept: application/json");
	return headers;
}

// The Edge Function returns friendly text in "error" / "message"; use it as-is.
std::string ErrorMessage(const HttpResponse& response, const char* fallback) {
	jsonlite::Value root;
	if (jsonlite::Parse(response.body, root)) {
		std::string message = root.get_string("error");
		if (message.empty())
			message = root.get_string("message");
		if (message.empty())
			message = root.get_string("msg");
		if (!message.empty())
			return message;
	}

	if (response.status == 0)
		return "Cannot reach the server";
	if (response.status >= 500)
		return "Server error, try again later";
	return fallback;
}

// ----------------------------------------------------------------- update ----

const std::size_t kMaxUpdateBytes = 64u * 1024u * 1024u;

bool IsSha256Hex(const std::string& value) {
	if (value.size() != 64)
		return false;
	for (const char c : value) {
		const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
		if (!hex)
			return false;
	}
	return true;
}

// Downloads a file with WinHTTP. GitHub answers the latest-release URL with a
// redirect, so follow up to five Location headers.
// progress receives 0 to 90 while the body arrives. Without it the loader update
// bar (g_update_progress) is driven, but only while an update is downloading.
bool HttpDownload(const std::wstring& url, std::vector<unsigned char>& bytes, std::string& error,
	std::atomic<int>* progress = nullptr) {
	bytes.clear();
	const std::wstring agent = Utf8ToWide(kUserAgent);
	std::wstring current = url;

	for (int hop = 0; hop <= 5; ++hop) {
		URL_COMPONENTS parts;
		ZeroMemory(&parts, sizeof(parts));
		parts.dwStructSize = sizeof(parts);

		wchar_t host[256] = {};
		wchar_t path[2048] = {};
		parts.lpszHostName = host;
		parts.dwHostNameLength = 255;
		parts.lpszUrlPath = path;
		parts.dwUrlPathLength = 2047;

		if (!WinHttpCrackUrl(current.c_str(), static_cast<DWORD>(current.size()), 0, &parts)) {
			error = "Bad update URL";
			return false;
		}

		const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
		HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (!session) {
			error = "WinHTTP is not available";
			return false;
		}

		WinHttpSetTimeouts(session, 10000, 10000, 10000, 60000);
		if (secure) {
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
			WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
		}

		HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
		HINTERNET request = connection
			? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)
			: nullptr;

		const bool sent = request &&
			WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
			WinHttpReceiveResponse(request, nullptr);

		if (!sent) {
			if (request)
				WinHttpCloseHandle(request);
			if (connection)
				WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			error = "Cannot download the update";
			return false;
		}

		DWORD status_code = 0;
		DWORD size = sizeof(status_code);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);

		if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
			wchar_t location[2048] = {};
			DWORD location_size = sizeof(location);
			const bool found = WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION,
				WINHTTP_HEADER_NAME_BY_INDEX, location, &location_size, WINHTTP_NO_HEADER_INDEX) != FALSE;
			std::wstring next;
			if (found)
				next = location;

			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);

			if (next.empty() || hop == 5) {
				error = "Update download redirect failed";
				return false;
			}
			current = next;
			continue;
		}

		if (status_code != 200) {
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			error = "Update download failed (HTTP " + std::to_string(status_code) + ")";
			return false;
		}

		unsigned long long total = 0;
		wchar_t length_text[64] = {};
		DWORD length_size = sizeof(length_text);
		if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
			WINHTTP_HEADER_NAME_BY_INDEX, length_text, &length_size, WINHTTP_NO_HEADER_INDEX)) {
			total = _wcstoui64(length_text, nullptr, 10);
		}

		for (;;) {
			// The launch path (progress != nullptr) can be cancelled while the
			// body is still arriving.
			if (progress != nullptr && g_launch_cancel.load()) {
				error = "Download cancelled";
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}

			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
				break;

			if (bytes.size() + available > kMaxUpdateBytes) {
				error = "Update file is too large";
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				return false;
			}

			std::vector<unsigned char> chunk(available);
			DWORD read = 0;
			if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
				break;

			chunk.resize(read);
			bytes.insert(bytes.end(), chunk.begin(), chunk.end());

			if (total > 0 && (progress != nullptr || g_update_status.load() == sdauth::UpdateDownloading)) {
				const unsigned long long percent = static_cast<unsigned long long>(bytes.size()) * 90 / total;
				(progress != nullptr ? *progress : g_update_progress).store(static_cast<int>(percent > 90 ? 90 : percent));
			}
		}

		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);

		if (bytes.empty()) {
			error = "Update download is empty";
			return false;
		}
		return true;
	}

	error = "Too many update redirects";
	return false;
}

bool WriteAllBytes(const std::wstring& path, const std::vector<unsigned char>& bytes) {
	HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	DWORD written = 0;
	const bool ok = WriteFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE &&
		written == bytes.size();
	CloseHandle(file);
	if (!ok)
		DeleteFileW(path.c_str());
	return ok;
}

// Replaces the loader file in place. Windows allows renaming a running exe, so
// the old build moves to <exe>.old and the new one takes its name. No helper
// process and no command-line switches are involved; the next launch deletes
// <exe>.old.
bool SwapExecutable(const std::wstring& target, const std::vector<unsigned char>& bytes, std::string& error) {
	const std::wstring new_file = target + L".new";
	const std::wstring old_file = target + L".old";

	DeleteFileW(old_file.c_str());
	if (!WriteAllBytes(new_file, bytes)) {
		error = "Cannot save the update next to the loader";
		return false;
	}

	bool moved = false;
	for (int attempt = 0; attempt < 20 && !moved; ++attempt) {
		moved = MoveFileExW(target.c_str(), old_file.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
		if (!moved)
			Sleep(150);
	}
	if (!moved) {
		DeleteFileW(new_file.c_str());
		error = "Cannot replace the loader file";
		return false;
	}

	bool installed = false;
	for (int attempt = 0; attempt < 20 && !installed; ++attempt) {
		installed = MoveFileExW(new_file.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
		if (!installed)
			Sleep(150);
	}
	if (!installed) {
		MoveFileExW(old_file.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING);
		DeleteFileW(new_file.c_str());
		error = "Cannot install the update";
		return false;
	}
	return true;
}

void RunUpdate(std::string url, std::string expected_sha) {
	SetUpdate(sdauth::UpdateDownloading, 2, "Downloading update...");

	std::vector<unsigned char> bytes;
	std::string error;
	if (!HttpDownload(Utf8ToWide(url), bytes, error)) {
		SetUpdate(sdauth::UpdateFailed, 0, error);
		return;
	}

	SetUpdate(sdauth::UpdateDownloading, 95, "Verifying update...");
	const std::string blob(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
	const std::string actual_sha = Sha256Hex(blob);
	if (!IsSha256Hex(actual_sha)) {
		SetUpdate(sdauth::UpdateFailed, 0, "Cannot verify the update file");
		return;
	}

	if (actual_sha != expected_sha) {
		SetUpdate(sdauth::UpdateFailed, 0, "Update checksum does not match. Try again");
		return;
	}

	const std::string current_sha = ExecutableHash();
	if (!current_sha.empty() && actual_sha == current_sha) {
		// The server pointed at the build that is already running. Do not lock
		// the loader on a version string mismatch: continue startup instead.
		g_update_available.store(false);
		g_update_required.store(false);
		g_startup_status.store(sdauth::StatusOk);
		{
			std::lock_guard<std::mutex> lock(g_mutex);
			g_startup_message = "Ready";
		}
		SetUpdate(sdauth::UpdateSkipped, 100, "Already up to date");
		return;
	}

	wchar_t target_path[MAX_PATH] = {};
	const DWORD target_length = GetModuleFileNameW(nullptr, target_path, MAX_PATH);
	if (target_length == 0 || target_length >= MAX_PATH) {
		SetUpdate(sdauth::UpdateFailed, 0, "Cannot find the loader file");
		return;
	}

	SetUpdate(sdauth::UpdateDownloading, 98, "Installing update...");
	const std::wstring target(target_path, target_length);
	if (!SwapExecutable(target, bytes, error)) {
		SetUpdate(sdauth::UpdateFailed, 0, error);
		return;
	}

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_update_target = target;
	}
	SetUpdate(sdauth::UpdateReady, 100, "Update installed.");
}

// ----------------------------------------------------------------- launch ----

void SetLaunch(int status, int progress, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_launch_message = message;
	}
	g_launch_progress.store(progress);
	g_launch_status.store(status);
}

// Folder for downloaded components: %APPDATA%\SachinDevLoader\components.
std::wstring ComponentDirectory() {
	wchar_t appdata[MAX_PATH] = {};
	const DWORD length = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return std::wstring();

	std::wstring directory = std::wstring(appdata) + L"\\SachinDevLoader";
	CreateDirectoryW(directory.c_str(), nullptr);
	directory += L"\\components";
	CreateDirectoryW(directory.c_str(), nullptr);
	return directory;
}

// Letters, digits, dash and underscore only, so a server name can never point
// outside the component folder.
std::wstring ComponentFileName(const std::string& name) {
	std::wstring file;
	for (const char c : name) {
		if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')
			file += static_cast<wchar_t>(c);
		else if (c == ' ')
			file += L'_';
	}
	if (file.empty())
		file = L"component";
	return file + L".exe";
}

bool ReadFileBytes(const std::wstring& path, std::string& bytes) {
	bytes.clear();
	HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
		return false;

	LARGE_INTEGER size = {};
	bool ok = GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= static_cast<long long>(kMaxUpdateBytes);
	if (ok) {
		bytes.resize(static_cast<std::size_t>(size.QuadPart));
		DWORD read = 0;
		ok = ReadFile(file, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE && read == bytes.size();
	}
	CloseHandle(file);
	if (!ok)
		bytes.clear();
	return ok;
}

bool StartProgram(const std::wstring& path, const std::wstring& directory, std::string& error) {
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	SHELLEXECUTEINFOW info;
	ZeroMemory(&info, sizeof(info));
	info.cbSize = sizeof(info);
	info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	info.lpVerb = L"open";
	info.lpFile = path.c_str();
	info.lpDirectory = directory.c_str();
	info.nShow = SW_SHOWNORMAL;

	const BOOL started = ShellExecuteExW(&info);
	const DWORD last_error = GetLastError();

	if (SUCCEEDED(com))
		CoUninitialize();

	if (started)
		return true;

	error = last_error == ERROR_CANCELLED ? "Start was cancelled" : "Cannot start the component";
	return false;
}

// Downloads the component unless a verified copy is already on disk, then
// starts it. The file must match the SHA-256 the server sent.
void RunLaunch(std::string name, std::string url, std::string expected_sha) {
	const std::wstring directory = ComponentDirectory();
	if (directory.empty()) {
		SetLaunch(sdauth::StatusError, 0, "Cannot find the loader data folder");
		return;
	}

	const std::wstring target = directory + L"\\" + ComponentFileName(name);

	std::string cached;
	const bool have_copy = ReadFileBytes(target, cached) && Sha256Hex(cached) == expected_sha;

	if (!have_copy) {
		SetLaunch(sdauth::StatusWorking, 2, "Downloading " + name + "...");

		std::vector<unsigned char> bytes;
		std::string error;
		if (!HttpDownload(Utf8ToWide(url), bytes, error, &g_launch_progress)) {
			SetLaunch(sdauth::StatusError, 0, error.empty() ? "Cannot download the component" : error);
			return;
		}

		SetLaunch(sdauth::StatusWorking, 95, "Verifying " + name + "...");
		const std::string blob(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
		if (Sha256Hex(blob) != expected_sha) {
			SetLaunch(sdauth::StatusError, 0, "Checksum does not match. The file was not started");
			return;
		}

		const std::wstring part = target + L".part";
		if (!WriteAllBytes(part, bytes)) {
			SetLaunch(sdauth::StatusError, 0, "Cannot save the component. Close it if it is running");
			return;
		}
		if (!MoveFileExW(part.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
			DeleteFileW(part.c_str());
			SetLaunch(sdauth::StatusError, 0, "Cannot save the component. Close it if it is running");
			return;
		}
	}

	SetLaunch(sdauth::StatusWorking, 98, "Starting " + name + "...");
	if (g_launch_cancel.load()) {
		SetLaunch(sdauth::StatusError, 0, "Cancelled. Nothing was started");
		return;
	}

	std::string error;
	if (!StartProgram(target, directory, error)) {
		SetLaunch(sdauth::StatusError, 0, error);
		return;
	}

	SetLaunch(sdauth::StatusOk, 100, name + " started");
}

// ------------------------------------------------------------------ login ----

bool ParseSession(const std::string& body, const std::string& username, sdauth::Session& session) {
	jsonlite::Value root;
	if (!jsonlite::Parse(body, root))
		return false;

	const std::string access = root.get_string("access_token");
	if (access.empty())
		return false;

	const double expires_in = root.get_number("expires_in", 3600.0);

	session.valid = true;
	session.username = username;
	session.access_token = access;
	session.refresh_token = root.get_string("refresh_token");
	session.expires_at = NowSeconds() + static_cast<long long>(expires_in);
	return true;
}

bool ParseFunctionResult(const std::string& body, std::string& error) {
	jsonlite::Value root;
	if (!jsonlite::Parse(body, root)) {
		error = "Bad server answer";
		return false;
	}

	if (!root.get_bool("ok", false)) {
		error = root.get_string("error");
		if (error.empty())
			error = "Account check failed. Sign in again";
		return false;
	}
	return true;
}

bool ParseComponents(const std::string& body, std::vector<sdauth::Component>& components, std::string& error,
	std::string& update_url, std::string& release_url, std::string& sha256, std::string& server_version,
	bool& update_required) {
	update_required = false;
	jsonlite::Value root;
	if (!jsonlite::Parse(body, root)) {
		error = "Bad server answer";
		return false;
	}

	server_version = root.get_string("version");
	if (server_version.empty())
		server_version = root.get_string("latest_version");

	if (!root.get_bool("ok", false)) {
		std::string code = root.get_string("code");
		std::string message = root.get_string("error");
		// Older servers put the raw code in "error" and send no "code" field.
		if (code.empty() && (message == "outdated" || message == "bad_build"))
			code = message;
		update_required = code == "outdated" || code == "bad_build";
		error = message.empty() ? code : message;
		if (error.empty())
			error = "Loader check failed. Try again";
		update_url = root.get_string("update_url");
		release_url = root.get_string("release_url");
		sha256 = root.get_string("sha256");
		if (release_url.empty())
			release_url = root.get_string("download_url"); // 0.1 server: page link
		if (release_url.empty())
			release_url = root.get_string("url");
		if (sha256.size() == 64)
			for (char& c : sha256)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return false;
	}

	update_url = root.get_string("update_url");
	release_url = root.get_string("release_url");
	sha256 = root.get_string("sha256");
	if (sha256.size() == 64)
		for (char& c : sha256)
			c = static_cast<char>(tolower(static_cast<unsigned char>(c)));

	const jsonlite::Value* list = root.find("components");
	if (list && list->is_array()) {
		for (const jsonlite::Value& item : list->array) {
			sdauth::Component component;
			component.name = item.get_string("name");
			component.version = item.get_string("version");
			component.description = item.get_string("description");
			component.status = item.get_string("status", "online");
			component.download_url = item.get_string("download_url");
			component.sha256 = item.get_string("sha256");
			for (char& c : component.sha256)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (!component.name.empty())
				components.push_back(component);
		}
	}
	return true;
}

// Account expiry from a successful bootstrap answer. Only a signed-in answer
// carries expires_at.
void ParseExtras(const std::string& body, bool signed_in) {
	if (!signed_in)
		return;

	jsonlite::Value root;
	if (!jsonlite::Parse(body, root))
		return;

	g_expires_at.store(static_cast<long long>(root.get_number("expires_at", 0.0)));
}

// Server-side bootstrap: account, HWID, client version and executable hash are
// checked again with the fresh token before any component is released.
bool RunBootstrap(const std::string& access_token, const std::string& hwid, std::string& error) {
	const std::string exe_hash = ExecutableHash();
	if (exe_hash.empty()) {
		error = "Cannot read the loader file";
		return false;
	}

	std::string body =
		"{\"action\":\"bootstrap\",\"access_token\":\"" + JsonEscape(access_token) +
		"\",\"hwid\":\"" + JsonEscape(hwid) +
		"\",\"version\":\"" + JsonEscape(app::kVersion) +
		"\",\"exe_hash\":\"" + JsonEscape(exe_hash) + "\"}";
	ZeroOnExit wipe_body(body);

	HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	ZeroOnExit wipe_response(response.body);

	if (!response.transport_ok) {
		error = response.error.empty() ? "Cannot reach the server" : response.error;
		return false;
	}

	std::vector<sdauth::Component> components;
	std::string update_url;
	std::string release_url;
	std::string sha256;
	std::string server_version;
	bool update_required = false;
	if (response.status != 200 || !ParseComponents(response.body, components, error, update_url, release_url, sha256,
			server_version, update_required)) {
		if (error.empty() || response.status != 200)
			error = ErrorMessage(response, "Loader check failed. Try again");
		SetUpdateInfo(update_url, release_url, sha256, server_version, update_required);
		return false;
	}

	SetUpdateInfo(update_url, release_url, sha256, server_version, update_required);
	SetComponents(components);
	ParseExtras(response.body, true);
	return true;
}

// --------------------------------------------------------------- startup ----

void SetStartup(int status, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_startup_message = message;
	}
	g_startup_status.store(status);
}

// Runs before the login form. No account yet: the server checks the build
// and maintenance flag and returns the public component list.
void RunStartup(std::string hwid) {
	g_startup_stage.store(sdauth::StartupConnecting);

	const std::string exe_hash = ExecutableHash();
	if (exe_hash.empty()) {
		SetStartup(sdauth::StatusError, "Cannot read the loader file");
		return;
	}

	std::string body =
		"{\"action\":\"startup\",\"hwid\":\"" + JsonEscape(hwid) +
		"\",\"version\":\"" + JsonEscape(app::kVersion) +
		"\",\"exe_hash\":\"" + JsonEscape(exe_hash) + "\"}";

	const HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	if (!response.transport_ok) {
		SetStartup(sdauth::StatusError, response.error.empty() ? "Cannot reach the server" : response.error);
		return;
	}

	g_startup_stage.store(sdauth::StartupIntegrity);

	std::string error;
	std::vector<sdauth::Component> components;
	std::string update_url;
	std::string release_url;
	std::string sha256;
	std::string server_version;
	bool update_required = false;
	g_startup_stage.store(sdauth::StartupUpdateCheck);
	if (response.status != 200 || !ParseComponents(response.body, components, error, update_url, release_url, sha256,
			server_version, update_required)) {
		if (error.empty() || response.status != 200)
			error = ErrorMessage(response, "Loader check failed. Try again");
		SetUpdateInfo(update_url, release_url, sha256, server_version, update_required);
		SetStartup(sdauth::StatusError, error);
		return;
	}

	g_startup_stage.store(sdauth::StartupComponents);
	SetUpdateInfo(update_url, release_url, sha256, server_version, update_required);
	SetComponents(components);
	ParseExtras(response.body, false);

	g_startup_stage.store(sdauth::StartupDone);
	SetStartup(sdauth::StatusOk, "Ready");
}

// ------------------------------------------------- manual check / refresh ----

void SetRefresh(int status, const std::string& message) {
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_refresh_message = message;
	}
	g_refresh_status.store(status);
}

// Update check. Re-runs the public startup request
// but only refreshes the update information; the signed-in component list is
// never replaced with the non-launchable public list.
void RunUpdateCheck() {
	SetStartup(sdauth::StatusWorking, "Checking for updates...");

	const std::string exe_hash = ExecutableHash();
	if (exe_hash.empty()) {
		SetStartup(sdauth::StatusError, "Cannot read the loader file");
		return;
	}

	std::string body =
		"{\"action\":\"startup\",\"hwid\":\"" + JsonEscape(sdauth::MachineHwid()) +
		"\",\"version\":\"" + JsonEscape(app::kVersion) +
		"\",\"exe_hash\":\"" + JsonEscape(exe_hash) + "\"}";

	const HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	if (!response.transport_ok) {
		SetStartup(sdauth::StatusError, response.error.empty() ? "Cannot reach the server" : response.error);
		return;
	}

	std::string error;
	std::vector<sdauth::Component> components;
	std::string update_url;
	std::string release_url;
	std::string sha256;
	std::string server_version;
	bool update_required = false;
	if (response.status != 200 || !ParseComponents(response.body, components, error, update_url, release_url, sha256,
			server_version, update_required)) {
		if (error.empty() || response.status != 200)
			error = ErrorMessage(response, "Update check failed. Try again");
		SetStartup(sdauth::StatusError, error);
		return;
	}

	// Update information only: g_components and the session stay untouched.
	SetUpdateInfo(update_url, release_url, sha256, server_version, update_required);
	if (sdauth::StartupUpdateAvailable())
		SetStartup(sdauth::StatusOk,
			server_version.empty() ? "An update is available" : "Update available: " + server_version);
	else
		SetStartup(sdauth::StatusOk, "You are on the latest version");
}

// Re-runs the signed-in bootstrap so the component list, expiry and update
// information come fresh from the server.
void RunRefresh() {
	const sdauth::Session session = sdauth::GetSession();
	if (!session.valid) {
		SetRefresh(sdauth::StatusError, "No active session");
		return;
	}

	std::string error;
	if (!RunBootstrap(session.access_token, sdauth::MachineHwid(), error)) {
		SetRefresh(sdauth::StatusError, error.empty() ? "Refresh failed. Try again" : error);
		return;
	}

	SetRefresh(sdauth::StatusOk, "Application list updated");
}

void RevokeToken(const std::string& token) {
	std::string body = "{\"action\":\"logout\",\"access_token\":\"" + JsonEscape(token) + "\"}";
	ZeroOnExit wipe_body(body);
	HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	ZeroOnExit wipe_response(response.body);
}

void RunLogin(std::string username, std::string password, std::string hwid) {
	ZeroOnExit wipe_password(password);
	SetStage(sdauth::StageAuthenticating);

	std::string body =
		"{\"action\":\"login\",\"username\":\"" + JsonEscape(username) +
		"\",\"password\":\"" + JsonEscape(password) +
		"\",\"hwid\":\"" + JsonEscape(hwid) + "\"}";
	ZeroOnExit wipe_body(body);

	HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	ZeroOnExit wipe_response(response.body);

	if (!response.transport_ok) {
		SetStatus(sdauth::StatusError, response.error.empty() ? "Cannot reach the server" : response.error);
		return;
	}

	sdauth::Session session;
	if (response.status != 200 || !ParseSession(response.body, username, session)) {
		SetStatus(sdauth::StatusError, ErrorMessage(response, "Login failed. Try again"));
		return;
	}

	ZeroOnExit wipe_access(session.access_token);
	ZeroOnExit wipe_refresh(session.refresh_token);

	// The login answer already passed account_login (ban, HWID, rate limit).
	SetStage(sdauth::StageAccount);
	SetStage(sdauth::StageSecurity);

	std::string error;
	if (!RunBootstrap(session.access_token, hwid, error)) {
		RevokeToken(session.access_token);
		SetStatus(sdauth::StatusError, error);
		return;
	}

	SetSession(session);
	g_next_check.store(NowSeconds() + kHeartbeatSeconds);
	SetStage(sdauth::StageDone);
	SetStatus(sdauth::StatusOk, "Welcome back, " + session.username);
}

// --------------------------------------------------------------- heartbeat ----

bool RefreshSession(std::string& error) {
	const sdauth::Session current = sdauth::GetSession();
	if (!current.valid || current.refresh_token.empty()) {
		error = "Session expired. Sign in again";
		return false;
	}

	std::string body =
		"{\"action\":\"refresh\",\"refresh_token\":\"" + JsonEscape(current.refresh_token) +
		"\",\"hwid\":\"" + JsonEscape(sdauth::MachineHwid()) + "\"}";
	ZeroOnExit wipe_body(body);

	HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), body);
	ZeroOnExit wipe_response(response.body);
	if (!response.transport_ok) {
		error = response.error.empty() ? "Cannot reach the server" : response.error;
		return false;
	}

	sdauth::Session refreshed;
	if (response.status != 200 || !ParseSession(response.body, current.username, refreshed)) {
		error = ErrorMessage(response, "Session expired. Sign in again");
		return false;
	}

	ZeroOnExit wipe_access(refreshed.access_token);
	ZeroOnExit wipe_refresh(refreshed.refresh_token);

	SetSession(refreshed);
	return true;
}

void RunHeartbeat() {
	sdauth::Session current = sdauth::GetSession();
	if (!current.valid) {
		g_busy.store(false);
		return;
	}

	if (current.expires_at - 60 <= NowSeconds()) {
		std::string refresh_error;
		if (!RefreshSession(refresh_error)) {
			const bool had_session = sdauth::HasSession();
			sdauth::SignOut();
			if (had_session)
				SetStatus(sdauth::StatusError, refresh_error.empty() ? "Session expired. Sign in again" : refresh_error);
			g_busy.store(false);
			return;
		}
		current = sdauth::GetSession();
		if (!current.valid) {
			g_busy.store(false);
			return;
		}
	}

	std::string check_body =
		"{\"action\":\"check\",\"access_token\":\"" + JsonEscape(current.access_token) +
		"\",\"hwid\":\"" + JsonEscape(sdauth::MachineHwid()) + "\"}";
	ZeroOnExit wipe_check(check_body);

	const HttpResponse response = HttpPost(ApiUrl(kAuthFunctionPath), FunctionHeaders(), check_body);

	if (!response.transport_ok) {
		g_busy.store(false); // network hiccup: keep the session, retry later
		return;
	}

	std::string error;
	if (response.status != 200 || !ParseFunctionResult(response.body, error)) {
		const bool had_session = sdauth::HasSession();
		sdauth::SignOut();
		if (had_session) {
			if (response.status == 401 && error.empty())
				error = "Session expired. Sign in again";
			SetStatus(sdauth::StatusError, error.empty() ? "Account check failed. Sign in again" : error);
		}
		g_busy.store(false);
		return;
	}

	g_busy.store(false);
}

} // namespace

namespace sdauth {

void Init() {
	MachineHwid();
}

void Tick() {
	if (!HasSession())
		return;

	const long long now = NowSeconds();
	if (now < g_next_check.load())
		return;

	if (g_busy.exchange(true))
		return;

	g_next_check.store(now + kHeartbeatSeconds);
	std::thread([]() { RunHeartbeat(); }).detach();
}

void StartLogin(const std::string& username, const std::string& password) {
	const std::string name = NormalizeUsername(username);
	std::string error;

	if (!ValidateUsername(name, error)) {
		ReportLocalError(error);
		return;
	}

	if (password.empty()) {
		ReportLocalError("Enter your password");
		return;
	}

	if (g_busy.exchange(true)) {
		ReportLocalError("Please wait for the current request to finish");
		return;
	}

	const std::string hwid = MachineHwid();
	SetStage(StageAuthenticating);
	SetStatus(StatusWorking, "Signing in...");

	std::thread([name, password, hwid]() mutable {
		RunLogin(name, std::move(password), std::move(hwid));
		g_busy.store(false);
	}).detach();
}

int LoginStatus() {
	return g_status.load();
}

std::string LoginMessage() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_message;
}

void StartStartup() {
	if (g_startup_busy.exchange(true))
		return;

	const std::string hwid = MachineHwid();
	g_startup_stage.store(StartupConnecting);
	SetUpdateInfo("", "", "", "", false);
	SetStartup(StatusWorking, "Connecting...");

	std::thread([hwid]() {
		RunStartup(hwid);
		g_startup_busy.store(false);
	}).detach();
}

void StartUpdateCheck() {
	if (g_startup_busy.exchange(true))
		return;

	SetUpdateInfo("", "", "", "", false);
	SetStartup(StatusWorking, "Checking for updates...");

	std::thread([]() {
		RunUpdateCheck();
		g_startup_busy.store(false);
	}).detach();
}

void StartComponentRefresh() {
	if (!HasSession()) {
		SetRefresh(StatusError, "Sign in first");
		return;
	}

	if (g_refresh_busy.exchange(true))
		return;

	SetRefresh(StatusWorking, "Refreshing...");

	std::thread([]() {
		RunRefresh();
		g_refresh_busy.store(false);
	}).detach();
}

int RefreshStatus() {
	return g_refresh_status.load();
}

std::string RefreshMessage() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_refresh_message;
}

int StartupStatus() {
	return g_startup_status.load();
}

int StartupStageValue() {
	return g_startup_stage.load();
}

std::string StartupMessage() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_startup_message;
}

std::string StartupDownloadUrl() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_update_url;
}

std::string StartupReleaseUrl() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_release_url;
}

std::string StartupSha256() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_update_sha;
}

std::string StartupVersion() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_server_version;
}

bool StartupUpdateAvailable() {
	return g_update_available.load();
}

bool StartupUpdateRequired() {
	return g_update_required.load();
}

void StartUpdate() {
	if (g_update_busy.exchange(true))
		return;

	std::string url;
	std::string sha;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		url = g_update_url;
		sha = g_update_sha;
	}

	if (url.empty()) {
		SetUpdate(sdauth::UpdateFailed, 0, "No update download is available");
		g_update_busy.store(false);
		return;
	}

	if (!IsSha256Hex(sha)) {
		SetUpdate(sdauth::UpdateFailed, 0, "The update checksum is missing");
		g_update_busy.store(false);
		return;
	}

	SetUpdate(sdauth::UpdateDownloading, 1, "Downloading update...");
	std::thread([url, sha]() {
		RunUpdate(url, sha);
		g_update_busy.store(false);
	}).detach();
}

void SkipUpdate() {
	g_update_available.store(false);
	g_update_required.store(false);
	SetUpdate(sdauth::UpdateSkipped, 0, "Update skipped");
}

bool RestartLoader() {
	std::wstring target;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		target = g_update_target;
	}
	if (target.empty())
		return false;

	std::wstring directory = target;
	const std::size_t slash = directory.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		directory.resize(slash);
	else
		directory.clear();

	std::wstring command = L"\"" + target + L"\"";
	STARTUPINFOW info;
	ZeroMemory(&info, sizeof(info));
	info.cb = sizeof(info);
	PROCESS_INFORMATION process;
	ZeroMemory(&process, sizeof(process));

	if (!CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
			directory.empty() ? nullptr : directory.c_str(), &info, &process))
		return false;

	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return true;
}

int UpdateStatus() {
	return g_update_status.load();
}

int UpdateProgress() {
	return g_update_progress.load();
}

std::string UpdateMessage() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_update_message;
}

int LoginStage() {
	return g_stage.load();
}

std::vector<Component> Components() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_components;
}

long long AccountExpiresAt() {
	return g_expires_at.load();
}

bool LaunchComponent(const std::string& name) {
	if (!HasSession())
		return false;

	std::string url;
	std::string sha;
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		for (const Component& component : g_components) {
			if (component.name != name)
				continue;
			if (component.status == "online" || component.status == "beta") {
				url = component.download_url;
				sha = component.sha256;
			}
			break;
		}
	}

	if (url.compare(0, 8, "https://") != 0 || !IsSha256Hex(sha))
		return false;

	if (g_launch_busy.exchange(true))
		return false;

	{
		std::lock_guard<std::mutex> lock(g_mutex);
		g_launch_name = name;
	}
	SetLaunch(StatusWorking, 1, "Preparing " + name + "...");
	g_launch_cancel.store(false);

	std::thread([name, url, sha]() {
		RunLaunch(name, url, sha);
		g_launch_busy.store(false);
	}).detach();
	return true;
}

int LaunchStatus() {
	return g_launch_status.load();
}

int LaunchProgress() {
	return g_launch_progress.load();
}

std::string LaunchName() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_launch_name;
}

std::string LaunchMessage() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_launch_message;
}

void ResetLaunch() {
	if (g_launch_busy.load())
		return;
	g_launch_status.store(StatusIdle);
}

// Stops a running component download. The worker reports the cancelled state
// as a normal launch error, so the UI never waits forever.
void CancelLaunch() {
	g_launch_cancel.store(true);
}

void ReportLocalError(const std::string& message) {
	SetStatus(StatusError, message);
}

void ResetStatus() {
	g_status.store(StatusIdle);
}

bool HasSession() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_session.valid;
}

Session GetSession() {
	std::lock_guard<std::mutex> lock(g_mutex);
	return g_session;
}

void SignOut() {
	const Session current = GetSession();
	SetSession(Session());
	SetStage(StageNone);
	g_next_check.store(0);
	g_expires_at.store(0);
	if (!g_launch_busy.load())
		g_launch_status.store(StatusIdle);

	if (!current.valid || current.access_token.empty())
		return;

	// Revoke the session in the background, never block the render loop.
	const std::string token = current.access_token;
	std::thread([token]() { RevokeToken(token); }).detach();
}

const std::string& MachineHwid() {
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_hwid.empty()) {
		DWORD volume_serial = 0;
		GetVolumeInformationW(L"C:\\", nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0);

		wchar_t computer_name[256] = {};
		DWORD computer_name_size = 255;
		GetComputerNameW(computer_name, &computer_name_size);

		wchar_t cpu_name[256] = {};
		DWORD cpu_size = sizeof(cpu_name);
		HKEY cpu_key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &cpu_key) == ERROR_SUCCESS) {
			RegQueryValueExW(cpu_key, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(cpu_name), &cpu_size);
			RegCloseKey(cpu_key);
		}

		const std::string raw = std::to_string(volume_serial) + "|" + WideToUtf8(computer_name) + "|" + WideToUtf8(cpu_name);
		const std::string digest = Sha256Hex(raw);

		std::string formatted;
		for (std::size_t i = 0; i + 4 <= digest.size() && formatted.size() < 24; i += 4) {
			if (!formatted.empty())
				formatted += '-';
			formatted += digest.substr(i, 4);
		}

		for (char& c : formatted)
			c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

		g_hwid = formatted;
	}
	return g_hwid;
}

bool ValidateUsername(const std::string& username, std::string& error) {
	if (username.size() < 2 || username.size() > 20) {
		error = "Username must be 2 to 20 characters";
		return false;
	}

	for (const char raw : username) {
		const char c = static_cast<char>(tolower(static_cast<unsigned char>(raw)));
		const bool allowed = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
		if (!allowed) {
			error = "Username can only use letters, digits and _";
			return false;
		}
	}
	return true;
}

std::string NormalizeUsername(const std::string& username) {
	const std::size_t begin = username.find_first_not_of(" \t\r\n");
	if (begin == std::string::npos)
		return std::string();

	const std::size_t end = username.find_last_not_of(" \t\r\n");
	std::string out = username.substr(begin, end - begin + 1);
	for (char& c : out)
		c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
	return out;
}

} // namespace sdauth
