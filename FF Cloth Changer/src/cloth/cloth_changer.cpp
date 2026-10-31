#include "cloth_changer.h"
#include "offsets.h"

#include "../auth/json_lite.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Local wardrobe modifier.
//
// The game keeps the wardrobe as a managed list of pointers. Owning a slot is
// not checked at render time, so pointing a slot the player owns at the entry
// of an item they do not own makes the client draw that item. The original
// pointer is kept and written back on restore, on disable and on shutdown.
//
// No packet is built and nothing is sent: this only changes what the local
// client renders.
// ---------------------------------------------------------------------------

namespace cloth {

namespace {

using offsets::chain;
using offsets::list;
using offsets::timing;

constexpr std::size_t kMessageSize = 192;

std::mutex g_lock;

// ------------------------------------------------------------------ attach --

HANDLE g_process = nullptr;
DWORD g_pid = 0;
std::uint32_t g_base = 0;
std::uint32_t g_base_override = 0;
Status g_attach_status = Status::Idle;
char g_attach_message[kMessageSize] = "Not attached";

// ---------------------------------------------------------------- catalog --

std::vector<Entry> g_catalog;
Status g_catalog_status = Status::Idle;
char g_catalog_message[kMessageSize] = "Not loaded";

// ------------------------------------------------------------------ config --

bool g_enabled = false;

// ------------------------------------------------------------------- swaps --

struct SwapState {
	std::uint32_t owned_id = 0;
	std::uint32_t target_id = 0;
	std::uint32_t value_addr = 0;    // slot whose pointer is rewritten
	std::uint32_t original_ptr = 0;  // written back on restore
	std::uint32_t target_ptr = 0;    // currently wanted pointer
};

std::vector<Applied> g_applied;
std::vector<SwapState> g_swaps;
int g_live = 0;
char g_commit_message[kMessageSize] = "";

// ----------------------------------------------------------------- worker --

std::thread g_worker;
std::atomic<bool> g_stop{ false };

// ----------------------------------------------------------------- helpers --

// Writes a bounded status line. The annotation keeps /W4 quiet about the
// runtime format string.
void SetMessage(char (&buffer)[kMessageSize], _Printf_format_string_ const char* format, ...)
{
	va_list args;
	va_start(args, format);
	std::vsnprintf(buffer, kMessageSize, format, args);
	va_end(args);
	buffer[kMessageSize - 1] = '\0';
}

bool ContainsCi(const std::string& haystack, const char* needle)
{
	if (needle == nullptr || needle[0] == '\0')
		return true;

	const std::size_t needle_len = std::strlen(needle);
	if (needle_len > haystack.size())
		return false;

	for (std::size_t i = 0; i + needle_len <= haystack.size(); ++i)
	{
		bool match = true;
		for (std::size_t j = 0; j < needle_len; ++j)
		{
			if (std::tolower(static_cast<unsigned char>(haystack[i + j]))
				!= std::tolower(static_cast<unsigned char>(needle[j])))
			{
				match = false;
				break;
			}
		}
		if (match)
			return true;
	}
	return false;
}

std::wstring Utf8ToWide(const std::string& text)
{
	if (text.empty())
		return std::wstring();

	const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
	if (needed <= 0)
		return std::wstring();

	std::wstring wide(static_cast<std::size_t>(needed), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &wide[0], needed);
	return wide;
}

// --------------------------------------------------------------- memory -----

bool ReadRaw(std::uint32_t address, void* buffer, std::size_t size)
{
	if (g_process == nullptr || buffer == nullptr || size == 0)
		return false;

	SIZE_T read = 0;
	const BOOL ok = ReadProcessMemory(g_process,
		reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), buffer, size, &read);
	return ok != FALSE && read == size;
}

template <typename T>
bool ReadValue(std::uint32_t address, T& out)
{
	return ReadRaw(address, &out, sizeof(T));
}

bool WriteRaw(std::uint32_t address, const void* buffer, std::size_t size)
{
	if (g_process == nullptr || buffer == nullptr || size == 0)
		return false;

	SIZE_T written = 0;
	const BOOL ok = WriteProcessMemory(g_process,
		reinterpret_cast<LPVOID>(static_cast<std::uintptr_t>(address)), buffer, size, &written);
	return ok != FALSE && written == size;
}

// A base is only accepted when two far-apart addresses in the image are
// readable, which rules out the coincidental hits a module scan can return.
bool ImageLooksValid(std::uint32_t base)
{
	std::uint32_t probe = 0;
	if (!ReadRaw(base, &probe, sizeof(probe)))
		return false;
	if (!ReadRaw(base + offsets::target::kMinImageSpan, &probe, sizeof(probe)))
		return false;
	return true;
}

DWORD FindHostPid()
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE)
		return 0;

	PROCESSENTRY32W entry{};
	entry.dwSize = sizeof(entry);

	DWORD found = 0;
	if (Process32FirstW(snapshot, &entry))
	{
		do
		{
			for (int i = 0; i < offsets::target::kHostProcessCount; ++i)
			{
				if (_wcsicmp(entry.szExeFile, offsets::target::kHostProcesses[i]) == 0)
				{
					found = entry.th32ProcessID;
					break;
				}
			}
		} while (found == 0 && Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return found;
}

// Looks for the game image in the target and returns its base. Fails loudly
// rather than returning a guess: a wrong base turns every later read into a
// random heap address.
bool ResolveImageBase(DWORD pid, std::uint32_t& base)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snapshot == INVALID_HANDLE_VALUE)
		return false;

	MODULEENTRY32W module{};
	module.dwSize = sizeof(module);

	bool found = false;
	if (Module32FirstW(snapshot, &module))
	{
		do
		{
			for (int i = 0; i < offsets::target::kModuleCount && !found; ++i)
			{
				if (_wcsicmp(module.szModule, offsets::target::kModuleNames[i]) != 0)
					continue;

				const std::uintptr_t raw = reinterpret_cast<std::uintptr_t>(module.modBaseAddr);

				// Every address in this module is 32-bit, so a base that does
				// not fit means the model cannot address this image. Reject it
				// instead of truncating into a random low address.
				if (raw == 0 || raw > 0xFFFFFFFFull)
					continue;

				const std::uint32_t candidate = static_cast<std::uint32_t>(raw);
				if (ImageLooksValid(candidate))
				{
					base = candidate;
					found = true;
				}
			}
		} while (!found && Module32NextW(snapshot, &module));
	}

	CloseHandle(snapshot);
	return found;
}

std::uint32_t ActiveBase()
{
	return g_base_override != 0 ? g_base_override : g_base;
}

// ----------------------------------------------------------- live objects ---
//
// [base + BSS slot] -> baseMethod -> MethodInfo -> klass -> staticFields
// -> singleton instance -> tree dictionary -> data list -> items array.

bool TryGetInstance(std::uint32_t& instance)
{
	instance = 0;

	const std::uint32_t base = ActiveBase();
	if (base == 0)
		return false;

	std::uint32_t base_method = 0;
	if (!ReadValue(base + chain::kBssSlotRva, base_method) || base_method == 0)
		return false;

	std::uint32_t method_info = 0;
	if (!ReadValue(base_method, method_info) || method_info == 0)
		return false;

	std::uint32_t klass = 0;
	if (!ReadValue(method_info + chain::kMethodInfoToKlass, klass) || klass == 0)
		return false;

	std::uint32_t static_fields = 0;
	if (!ReadValue(klass + chain::kKlassToStaticFields, static_fields) || static_fields == 0)
		return false;

	if (!ReadValue(static_fields + chain::kStaticFieldsInstance, instance) || instance == 0)
		return false;

	return true;
}

bool TryGetMData(std::uint32_t& items_array, int& count)
{
	items_array = 0;
	count = 0;

	std::uint32_t instance = 0;
	if (!TryGetInstance(instance))
		return false;

	std::uint32_t tree_dict = 0;
	if (!ReadValue(instance + list::kInstanceToTreeDict, tree_dict) || tree_dict == 0)
		return false;

	std::uint32_t data_list = 0;
	if (!ReadValue(tree_dict + list::kTreeDictToDataList, data_list) || data_list == 0)
		return false;

	std::uint32_t items = 0;
	if (!ReadValue(data_list + list::kDataListToItems, items) || items == 0)
		return false;

	std::int32_t raw_count = 0;
	if (!ReadValue(data_list + list::kDataListToCount, raw_count))
		return false;

	// A count outside the sanity bound means the chain landed somewhere that
	// is not the wardrobe list. Refuse instead of walking garbage.
	if (raw_count <= 0 || raw_count > list::kMaxEntries)
		return false;

	items_array = items + list::kArrayDataHeader;
	count = raw_count;
	return true;
}

// Walks the items array for one item id. Reports the address of the slot that
// holds the pointer, so the caller can rewrite exactly that word.
bool TryFindListEntry(std::uint32_t items_array, int count, std::uint32_t item_id,
	std::uint32_t& slot_addr, std::uint32_t& entry_ptr)
{
	slot_addr = 0;
	entry_ptr = 0;

	for (int i = 0; i < count; ++i)
	{
		const std::uint32_t slot = items_array
			+ static_cast<std::uint32_t>(i) * list::kElementStride;

		std::uint32_t data_ptr = 0;
		if (!ReadValue(slot, data_ptr) || data_ptr == 0)
			continue;

		std::uint32_t found_id = 0;
		if (!ReadValue(data_ptr + list::kEntryToItemId, found_id))
			continue;

		if (found_id != item_id)
			continue;

		slot_addr = slot;
		entry_ptr = data_ptr;
		return true;
	}

	return false;
}

// Makes sure one slot points at the wanted item. Returns true when the slot
// is live after the call.
bool EnsureSwap(std::uint32_t items_array, int count, std::uint32_t owned_id, std::uint32_t target_id)
{
	SwapState* state = nullptr;
	for (SwapState& existing : g_swaps)
	{
		if (existing.owned_id == owned_id)
		{
			state = &existing;
			break;
		}
	}

	if (state == nullptr)
	{
		std::uint32_t owned_slot = 0;
		std::uint32_t owned_ptr = 0;
		if (!TryFindListEntry(items_array, count, owned_id, owned_slot, owned_ptr))
			return false;

		std::uint32_t target_slot = 0;
		std::uint32_t target_ptr = 0;
		if (!TryFindListEntry(items_array, count, target_id, target_slot, target_ptr) || target_ptr == 0)
			return false;

		g_swaps.push_back(SwapState{});
		state = &g_swaps.back();
		state->owned_id = owned_id;
		state->target_id = target_id;
		state->value_addr = owned_slot;
		state->original_ptr = owned_ptr;
		state->target_ptr = target_ptr;
	}
	else if (state->target_id != target_id)
	{
		std::uint32_t unused_slot = 0;
		std::uint32_t new_target = 0;
		if (!TryFindListEntry(items_array, count, target_id, unused_slot, new_target) || new_target == 0)
			return false;

		state->target_id = target_id;
		state->target_ptr = new_target;
	}

	std::uint32_t current = 0;
	if (!ReadValue(state->value_addr, current))
		return false;

	if (current == state->target_ptr)
		return true;

	return WriteRaw(state->value_addr, &state->target_ptr, sizeof(state->target_ptr));
}

// Puts one slot back to the pointer the game originally held.
void RestoreSlot(std::uint32_t owned_id)
{
	for (std::size_t i = 0; i < g_swaps.size(); ++i)
	{
		SwapState& state = g_swaps[i];
		if (state.owned_id != owned_id)
			continue;

		if (state.value_addr != 0 && state.original_ptr != 0)
			WriteRaw(state.value_addr, &state.original_ptr, sizeof(state.original_ptr));

		g_swaps.erase(g_swaps.begin() + static_cast<std::ptrdiff_t>(i));
		return;
	}
}

void CommitUnlocked()
{
	if (g_applied.empty())
	{
		g_live = 0;
		g_commit_message[0] = '\0';
		return;
	}

	std::uint32_t items_array = 0;
	int count = 0;
	if (!TryGetMData(items_array, count))
	{
		g_live = 0;
		SetMessage(g_commit_message, "Wardrobe list not found");
		return;
	}

	int live = 0;
	for (const Applied& applied : g_applied)
	{
		if (EnsureSwap(items_array, count, applied.owned_id, applied.target_id))
			++live;
	}

	g_live = live;
	g_commit_message[0] = '\0';
}

// ---------------------------------------------------------------- catalog ---

bool HttpGet(const std::string& url, std::string& body, std::string& error)
{
	body.clear();

	const std::wstring wide_url = Utf8ToWide(url);
	if (wide_url.empty())
	{
		error = "Bad catalog URL";
		return false;
	}

	const std::wstring agent = L"ClothChanger";
	std::wstring current = wide_url;

	// GitHub raw and release URLs redirect, so follow a few hops.
	for (int hop = 0; hop <= 5; ++hop)
	{
		URL_COMPONENTS parts{};
		parts.dwStructSize = sizeof(parts);

		wchar_t host[256] = {};
		wchar_t path[2048] = {};
		parts.lpszHostName = host;
		parts.dwHostNameLength = 255;
		parts.lpszUrlPath = path;
		parts.dwUrlPathLength = 2047;

		if (!WinHttpCrackUrl(current.c_str(), static_cast<DWORD>(current.size()), 0, &parts))
		{
			error = "Bad catalog URL";
			return false;
		}

		const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
		HINTERNET session = WinHttpOpen(agent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
			WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
		if (session == nullptr)
		{
			error = "WinHTTP is not available";
			return false;
		}

		WinHttpSetTimeouts(session, 8000, 8000, 8000, 30000);
		if (secure)
		{
			DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
			WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));
		}

		HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
		HINTERNET request = connection != nullptr
			? WinHttpOpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER,
				WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)
			: nullptr;

		const bool sent = request != nullptr
			&& WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
				WINHTTP_NO_REQUEST_DATA, 0, 0, 0) != FALSE
			&& WinHttpReceiveResponse(request, nullptr) != FALSE;

		if (!sent)
		{
			if (request != nullptr) WinHttpCloseHandle(request);
			if (connection != nullptr) WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			error = "Cannot reach the catalog server";
			return false;
		}

		DWORD status_code = 0;
		DWORD size = sizeof(status_code);
		WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
			WINHTTP_HEADER_NAME_BY_INDEX, &status_code, &size, WINHTTP_NO_HEADER_INDEX);

		if (status_code == 301 || status_code == 302 || status_code == 303
			|| status_code == 307 || status_code == 308)
		{
			wchar_t location[2048] = {};
			DWORD location_size = sizeof(location);
			std::wstring next;
			if (WinHttpQueryHeaders(request, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
					location, &location_size, WINHTTP_NO_HEADER_INDEX) != FALSE)
				next = location;

			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);

			if (next.empty() || hop == 5)
			{
				error = "Catalog redirect failed";
				return false;
			}
			current = next;
			continue;
		}

		if (status_code != 200)
		{
			WinHttpCloseHandle(request);
			WinHttpCloseHandle(connection);
			WinHttpCloseHandle(session);
			error = "Catalog server answered " + std::to_string(status_code);
			return false;
		}

		for (;;)
		{
			DWORD available = 0;
			if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
				break;

			if (body.size() + available > offsets::catalog::kMaxResponseBytes)
			{
				WinHttpCloseHandle(request);
				WinHttpCloseHandle(connection);
				WinHttpCloseHandle(session);
				error = "Catalog file is too large";
				return false;
			}

			std::string chunk(available, '\0');
			DWORD read = 0;
			if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
				break;

			chunk.resize(read);
			body += chunk;
		}

		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
		WinHttpCloseHandle(session);
		return true;
	}

	error = "Catalog redirect failed";
	return false;
}

// Accepts a root array or an object wrapping it under items / data / clothes,
// which is what the published item lists use.
bool ParseCatalog(const std::string& text, std::vector<Entry>& out)
{
	jsonlite::Value root;
	if (!jsonlite::Parse(text, root))
		return false;

	const jsonlite::Value* array = nullptr;
	if (root.is_array())
	{
		array = &root;
	}
	else
	{
		static const char* const kWrappers[] = { "items", "data", "clothes" };
		for (const char* key : kWrappers)
		{
			const jsonlite::Value* found = root.find(key);
			if (found != nullptr && found->is_array())
			{
				array = found;
				break;
			}
		}
	}

	if (array == nullptr)
		return false;

	for (const jsonlite::Value& item : array->array)
	{
		if (!item.is_object())
			continue;

		// Lists that mix item types are filtered down to wearable clothing.
		const std::string type = item.get_string("itemType");
		if (!type.empty() && _stricmp(type.c_str(), "CLOTHES") != 0)
			continue;

		const std::string id_text = item.get_string("itemID");
		if (id_text.empty())
			continue;

		const unsigned long parsed = std::strtoul(id_text.c_str(), nullptr, 10);
		if (parsed == 0)
			continue;

		Entry entry;
		entry.item_id = static_cast<std::uint32_t>(parsed);
		entry.description = item.get_string("description");
		entry.rarity = item.get_string("Rare", "WHITE");
		entry.icon = item.get_string("icon");
		entry.wardrobe_type = item.get_string("wardrobeType");
		out.push_back(std::move(entry));
	}

	return !out.empty();
}

// ----------------------------------------------------------------- worker ---

void WorkerLoop()
{
	unsigned ticks = 0;
	unsigned attach_wait = 0;

	while (!g_stop.load(std::memory_order_relaxed))
	{
		Sleep(timing::kWorkerSleepMs);

		// Re-attach on a slow timer so a restarted emulator is picked up
		// without hammering the process list.
		if (g_process == nullptr)
		{
			if (++attach_wait * timing::kWorkerSleepMs >= timing::kAttachRetryMs)
			{
				attach_wait = 0;
				Attach();
			}
			continue;
		}

		if (!Enabled())
			continue;

		if (++ticks < static_cast<unsigned>(timing::kCommitEveryTicks))
			continue;

		ticks = 0;

		std::lock_guard<std::mutex> guard(g_lock);
		CommitUnlocked();
	}
}

} // namespace

// ------------------------------------------------------------- lifecycle ----

void Init()
{
	{
		std::lock_guard<std::mutex> guard(g_lock);
		if (g_worker.joinable())
			return;
		g_stop.store(false, std::memory_order_relaxed);
	}

	Attach();

	g_worker = std::thread(WorkerLoop);
}

void Shutdown()
{
	{
		std::lock_guard<std::mutex> guard(g_lock);
		g_stop.store(true, std::memory_order_relaxed);
	}

	if (g_worker.joinable())
		g_worker.join();

	std::lock_guard<std::mutex> guard(g_lock);

	// Put every slot back before the handle closes, so the game is left the
	// way it was found.
	for (const Applied& applied : g_applied)
		RestoreSlot(applied.owned_id);

	g_swaps.clear();
	g_applied.clear();
	g_live = 0;

	if (g_process != nullptr)
	{
		CloseHandle(g_process);
		g_process = nullptr;
	}
	g_pid = 0;
	g_base = 0;
}

// ---------------------------------------------------------------- attach ----

bool Attach()
{
	std::lock_guard<std::mutex> guard(g_lock);

	if (g_process != nullptr)
		return true;

	const DWORD pid = FindHostPid();
	if (pid == 0)
	{
		g_attach_status = Status::Error;
		SetMessage(g_attach_message, "No emulator process found");
		return false;
	}

	HANDLE process = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
		| PROCESS_QUERY_INFORMATION, FALSE, pid);
	if (process == nullptr)
	{
		g_attach_status = Status::Error;
		SetMessage(g_attach_message, "Cannot open the emulator (error %lu)", GetLastError());
		return false;
	}

	// The base is resolved before the handle is committed, so a failed resolve
	// leaves the tool cleanly detached instead of holding a useless handle.
	std::uint32_t base = 0;
	if (!ResolveImageBase(pid, base))
	{
		CloseHandle(process);

		g_attach_status = Status::Error;
		SetMessage(g_attach_message, "Game image not found in the process");
		return false;
	}

	g_process = process;
	g_pid = pid;
	g_base = base;
	g_attach_status = Status::Ok;
	SetMessage(g_attach_message, "Attached to pid %lu, base 0x%08X", pid, base);
	return true;
}

void Detach()
{
	std::lock_guard<std::mutex> guard(g_lock);

	for (const Applied& applied : g_applied)
		RestoreSlot(applied.owned_id);

	g_swaps.clear();
	g_live = 0;

	if (g_process != nullptr)
	{
		CloseHandle(g_process);
		g_process = nullptr;
	}
	g_pid = 0;
	g_base = 0;
	g_attach_status = Status::Idle;
	SetMessage(g_attach_message, "Not attached");
}

Status AttachStatus()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_attach_status;
}

const char* AttachMessage()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_attach_message;
}

void SetBaseOverride(std::uint32_t base)
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_base_override = base;
}

void ClearBaseOverride()
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_base_override = 0;
}

// ------------------------------------------------------------- catalog ------

void ReloadCatalog(const std::string& url)
{
	{
		std::lock_guard<std::mutex> guard(g_lock);
		g_catalog_status = Status::Working;
		SetMessage(g_catalog_message, "Downloading...");
	}

	std::thread([url]()
	{
		std::string body;
		std::string error;
		std::vector<Entry> parsed;

		const bool ok = HttpGet(url, body, error) && ParseCatalog(body, parsed);

		std::lock_guard<std::mutex> guard(g_lock);
		if (ok)
		{
			g_catalog = std::move(parsed);
			g_catalog_status = Status::Ok;
			SetMessage(g_catalog_message, "Loaded %d items", static_cast<int>(g_catalog.size()));
		}
		else
		{
			g_catalog_status = Status::Error;
			SetMessage(g_catalog_message, "%s", error.empty() ? "Catalog file could not be read" : error.c_str());
		}
	}).detach();
}

Status CatalogStatus()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_catalog_status;
}

const char* CatalogMessage()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_catalog_message;
}

int CatalogSize()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return static_cast<int>(g_catalog.size());
}

std::vector<Entry> Catalog(Category category, const char* search)
{
	std::lock_guard<std::mutex> guard(g_lock);

	std::vector<Entry> result;
	result.reserve(g_catalog.size());

	for (const Entry& entry : g_catalog)
	{
		if (Categorize(entry) != category)
			continue;

		if (!ContainsCi(entry.description, search))
			continue;

		result.push_back(entry);
	}

	return result;
}

// ---------------------------------------------------------------- config ----

bool Enabled()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_enabled;
}

void SetEnabled(bool enabled)
{
	std::lock_guard<std::mutex> guard(g_lock);
	g_enabled = enabled;

	// Switching off puts every slot back immediately instead of leaving the
	// last written pointers in place until the next commit.
	if (!enabled)
	{
		for (const Applied& applied : g_applied)
			RestoreSlot(applied.owned_id);
		g_live = 0;
	}
}

// ------------------------------------------------------------------ swaps ----

bool Apply(std::uint32_t owned_id, std::uint32_t target_id)
{
	if (owned_id == 0 || target_id == 0)
		return false;

	std::lock_guard<std::mutex> guard(g_lock);

	// A slot already has one meaning; re-target it instead of adding a second
	// writer for the same address.
	for (Applied& applied : g_applied)
	{
		if (applied.owned_id != owned_id)
			continue;

		applied.target_id = target_id;
		for (SwapState& state : g_swaps)
		{
			if (state.owned_id == owned_id)
				state.target_id = target_id;
		}
		return true;
	}

	std::string description;
	std::string rarity = "WHITE";
	for (const Entry& entry : g_catalog)
	{
		if (entry.item_id != target_id)
			continue;
		description = entry.description;
		rarity = entry.rarity;
		break;
	}

	Applied applied;
	applied.owned_id = owned_id;
	applied.target_id = target_id;
	applied.description = description;
	applied.rarity = rarity;
	g_applied.push_back(std::move(applied));
	return true;
}

void Remove(std::uint32_t owned_id)
{
	std::lock_guard<std::mutex> guard(g_lock);

	RestoreSlot(owned_id);
	g_applied.erase(std::remove_if(g_applied.begin(), g_applied.end(),
		[owned_id](const Applied& applied) { return applied.owned_id == owned_id; }),
		g_applied.end());
}

void ClearAll()
{
	std::lock_guard<std::mutex> guard(g_lock);

	for (const Applied& applied : g_applied)
		RestoreSlot(applied.owned_id);

	g_applied.clear();
	g_swaps.clear();
	g_live = 0;
	g_commit_message[0] = '\0';
}

std::vector<Applied> AppliedList()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_applied;
}

int AppliedCount()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return static_cast<int>(g_applied.size());
}

int LiveCount()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_live;
}

const char* CommitMessage()
{
	std::lock_guard<std::mutex> guard(g_lock);
	return g_commit_message;
}

// ---------------------------------------------------------------- live ------

bool WardrobeReadable()
{
	std::lock_guard<std::mutex> guard(g_lock);

	if (g_process == nullptr)
		return false;

	std::uint32_t items_array = 0;
	int count = 0;
	return TryGetMData(items_array, count);
}

std::vector<Slot> WardrobeSlots()
{
	std::lock_guard<std::mutex> guard(g_lock);

	std::vector<Slot> slots;
	if (g_process == nullptr)
		return slots;

	std::uint32_t items_array = 0;
	int count = 0;
	if (!TryGetMData(items_array, count))
		return slots;

	// Only the game knows which ids are really loaded, so the list is read
	// back instead of guessed from the catalog.
	slots.reserve(static_cast<std::size_t>(count));
	for (int i = 0; i < count; ++i)
	{
		const std::uint32_t slot = items_array
			+ static_cast<std::uint32_t>(i) * offsets::list::kElementStride;

		std::uint32_t data_ptr = 0;
		if (!ReadValue(slot, data_ptr) || data_ptr == 0)
			continue;

		std::uint32_t item_id = 0;
		if (!ReadValue(data_ptr + offsets::list::kEntryToItemId, item_id))
			continue;

		Slot entry;
		entry.index = i;
		entry.item_id = item_id;
		slots.push_back(entry);
	}

	return slots;
}

// ---------------------------------------------------------------- helpers ----

Category Categorize(const Entry& entry)
{
	// Description keywords win over the id range, because a mask or a
	// facepaint can share a range with a wearable piece.
	if (entry.description.find("(Mask)") != std::string::npos)
		return Category::Mask;
	if (entry.description.find("(Facepaint)") != std::string::npos)
		return Category::Facepaint;
	if (ContainsCi(entry.icon, "hair"))
		return Category::Head;

	switch (entry.item_id / 1000000u)
	{
	case 203: return Category::Top;
	case 204: return Category::Bottom;
	case 205: return Category::Shoes;
	case 211: return Category::Head;
	case 214: return Category::Facepaint;
	default:  return Category::Other;
	}
}

const char* CategoryName(Category category)
{
	switch (category)
	{
	case Category::Top:       return "Top";
	case Category::Bottom:    return "Bottom";
	case Category::Shoes:     return "Shoes";
	case Category::Head:      return "Head";
	case Category::Facepaint: return "Facepaint";
	case Category::Mask:      return "Mask";
	default:                  return "Other";
	}
}

std::vector<std::string> BuildLabels(const std::vector<Entry>& entries)
{
	// Two items can share a display name; number the repeats so both stay
	// pickable in the list. Counted in one pass, then numbered in order, so a
	// few thousand entries do not turn into a quadratic scan.
	std::unordered_map<std::string, int> total;
	total.reserve(entries.size());
	for (const Entry& entry : entries)
		++total[entry.description];

	std::unordered_map<std::string, int> seen;
	seen.reserve(entries.size());

	std::vector<std::string> labels;
	labels.reserve(entries.size());

	for (const Entry& entry : entries)
	{
		const int index = ++seen[entry.description];
		if (total[entry.description] > 1)
			labels.push_back(entry.description + " (#" + std::to_string(index) + ")");
		else
			labels.push_back(entry.description);
	}

	return labels;
}

} // namespace cloth
