#pragma once

// ---------------------------------------------------------------------------
// cloth_changer.h
//
// Local wardrobe modifier. It never touches the server: it rewrites the item
// pointers the game reads, so the client renders an item on a slot that does
// not own it. Every build-specific number lives in offsets.h.
//
// Threading: the public setters copy state under a lock and return. A worker
// thread owns the memory writes. The UI only polls status.
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace cloth {

enum class Status {
	Idle = 0,
	Working = 1,
	Ok = 2,
	Error = 3,
};

// Wardrobe slot a piece of clothing belongs to.
enum class Category {
	Top = 0,
	Bottom,
	Shoes,
	Head,
	Facepaint,
	Mask,
	Other,
};

struct Entry {
	std::uint32_t item_id = 0;
	std::string description;
	std::string rarity = "WHITE";
	std::string icon;
	std::string wardrobe_type;
};

// One intended swap: the slot the player owns should render another item.
struct Applied {
	std::uint32_t owned_id = 0;
	std::uint32_t target_id = 0;
	std::string description;
	std::string rarity = "WHITE";
};

// ------------------------------------------------------------- lifecycle ----
//
// Init starts the worker and tries to attach. Shutdown restores every live
// swap before the process handle closes, so leaving the tool never leaves the
// game with a half-rewritten wardrobe.
void Init();
void Shutdown();

// ---------------------------------------------------------------- attach ----

// Attaches to the emulator process and resolves the IL2CPP image base. Safe
// to call again; it is a no-op while already attached.
bool Attach();
void Detach();

Status AttachStatus();
const char* AttachMessage();

// Addresses are 32-bit because the game image is 32-bit. Use this when the
// module snapshot cannot see the image and the base has to be supplied by
// hand; it takes precedence over the resolved one until cleared.
void SetBaseOverride(std::uint32_t base);
void ClearBaseOverride();

// ------------------------------------------------------------- catalog ------
//
// Item list downloaded once and cached in memory. Not required for a swap:
// without it the picker is empty but a swap by id still works.

void ReloadCatalog(const std::string& url);
Status CatalogStatus();
const char* CatalogMessage();
int CatalogSize();

// Entries of one category whose description contains the search text
// (case-insensitive). Empty search returns the whole category.
std::vector<Entry> Catalog(Category category, const char* search);

// ---------------------------------------------------------------- config ----

bool Enabled();
void SetEnabled(bool enabled);

// ------------------------------------------------------------------ swaps ----

// Records the intent: owned_id should render target_id. Returns false when
// either id is zero. A slot that already has an intent is re-targeted rather
// than given a second one.
bool Apply(std::uint32_t owned_id, std::uint32_t target_id);

// Restores the original pointer for this slot and forgets it.
void Remove(std::uint32_t owned_id);

// Restores every slot and clears the list.
void ClearAll();

std::vector<Applied> AppliedList();
int AppliedCount();

// ---------------------------------------------------------------- live ------

// One entry of the live wardrobe list exactly as the game holds it.
struct Slot {
	int index = 0;          // position in the managed array
	std::uint32_t item_id = 0;
};

// True while the wardrobe list can be reached: the game is loaded and the
// pointer chain resolved. False during loading, which is normal.
bool WardrobeReadable();

// Snapshot of the live list, empty when it cannot be read. Picking a slot
// from here is what gives a swap its owned side, because only the game knows
// which ids are really loaded.
std::vector<Slot> WardrobeSlots();

// Swaps that are currently written into the game. Lower than AppliedCount
// while the game is still loading and the list cannot be found yet.
int LiveCount();

// Why the last commit did nothing. Empty when the last commit worked.
const char* CommitMessage();

// ---------------------------------------------------------------- helpers ----

Category Categorize(const Entry& entry);
const char* CategoryName(Category category);

// Display labels for a list of entries. Duplicate descriptions get a (#2),
// (#3) suffix so two items with the same name stay selectable.
std::vector<std::string> BuildLabels(const std::vector<Entry>& entries);

} // namespace cloth
