#pragma once

// ---------------------------------------------------------------------------
// offsets.h
//
// Every game-build-specific value in one place. A new game patch normally
// means editing this file only; nothing below knows a raw number.
//
// How to update after a patch
//   1. target::kModuleNames / kHostProcesses  - only when the emulator or the
//      game module name changed.
//   2. chain::kBssSlotRva                     - RVA of the static pointer slot
//      of COW.AvatarWardrobeData. This is the one that moves every patch.
//   3. chain::* field offsets                  - re-derived from the class
//      layout of the new build.
//   4. list::*                                 - the wardrobe list layout.
//
// All addresses are 32-bit because the game is a 32-bit IL2CPP image running
// inside the emulator guest.
// ---------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>

namespace offsets {

// --------------------------------------------------------------- targeting ---
//
// The tool drives a game that runs inside an Android emulator, so the host
// process is the emulator itself. Add or remove names here when an emulator
// is renamed or a new one is supported.

namespace target {

inline constexpr const wchar_t* kHostProcesses[] = {
	// BlueStacks 5 / 4 / X
	L"HD-Player.exe",
	L"HD-Agent.exe",
	L"BstkSvc.exe",
	L"Bluestacks.exe",
	L"BlueStacks.exe",
	// LDPlayer 9 / 4
	L"dnplayer.exe",
	L"Ld9BoxHeadless.exe",
	L"LdVBoxHeadless.exe",
	// NoxPlayer
	L"Nox.exe",
	L"NoxVMHandle.exe",
	L"NoxSVC.exe",
	// MEmu Play
	L"MEmu.exe",
	L"MEmuHeadless.exe",
	L"MEmuSVC.exe",
	// MuMu Player
	L"NemuPlayer.exe",
	L"NemuHeadless.exe",
	L"MuMuPlayer.exe",
	// GameLoop
	L"GameLoop.exe",
	L"AppMarket.exe",
	L"TxAppMarket.exe",
	// SmartGaga
	L"ProjectTitan.exe",
	L"EngineService.exe",
	// Genymotion / VirtualBox based
	L"Genymotion.exe",
	L"VBoxHeadless.exe",
};

inline constexpr int kHostProcessCount =
	static_cast<int>(sizeof(kHostProcesses) / sizeof(kHostProcesses[0]));

// Module whose base the BSS RVA is added to. Tried in order; the first one
// found in the target process wins. libil2cpp.so is the emulator build,
// GameAssembly.dll covers a native PC build of the same game.
inline constexpr const wchar_t* kModuleNames[] = {
	L"libil2cpp.so",
	L"GameAssembly.dll",
};

inline constexpr int kModuleCount =
	static_cast<int>(sizeof(kModuleNames) / sizeof(kModuleNames[0]));

// Image size used to sanity-check a resolved base. A base whose readable
// span is smaller than this is treated as a wrong guess and rejected, so a
// bad resolve fails loudly instead of writing into the middle of the heap.
inline constexpr std::uint32_t kMinImageSpan = 0x100000u;

} // namespace target

// ------------------------------------------------------------ pointer chain --
//
// COW.AvatarWardrobeData (TypeDefIndex 10539).
//
//   [image base + kBssSlotRva]          static pointer slot in BSS
//     -> baseMethod
//   [baseMethod]                         MethodInfo*
//   [baseMethod + kMethodInfoToKlass]    Il2CppClass*
//   [klass + kKlassToStaticFields]       StaticFields*
//   [staticFields + kStaticFieldsInstance]  the singleton instance
//
// The BSS slot holds a pointer to a small structure whose first field is the
// MethodInfo of a method belonging to the class. That is the usual IL2CPP way
// to reach a class from its metadata without the type cache.

namespace chain {

// RVA of the static pointer slot. Moves on almost every game patch.
inline constexpr std::uint32_t kBssSlotRva = 0xA987F88u;

inline constexpr std::uint32_t kMethodInfoToKlass = 0x10u;
inline constexpr std::uint32_t kKlassToStaticFields = 0x5Cu;

// Byte offset of the singleton instance inside StaticFields. The first field
// of the static field struct is the instance for this class.
inline constexpr std::uint32_t kStaticFieldsInstance = 0x00u;

} // namespace chain

// ------------------------------------------------------------ live objects ---
//
// The singleton's own layout:
//
//   [instance + kInstanceToTreeDict]     wardrobe tree dictionary
//   [treeDict + kTreeDictToDataList]     list of wardrobe data entries
//   [dataList + kDataListToItems]        managed array of entry pointers
//   [dataList + kDataListToCount]        element count (int32)
//   [items + kArrayDataHeader]           first element (array header skip)

namespace list {

inline constexpr std::uint32_t kInstanceToTreeDict = 0x0Cu;
inline constexpr std::uint32_t kTreeDictToDataList = 0x0Cu;
inline constexpr std::uint32_t kDataListToItems = 0x08u;
inline constexpr std::uint32_t kDataListToCount = 0x0Cu;

// Managed array header size on a 32-bit IL2CPP build: klass + monitor + length.
inline constexpr std::uint32_t kArrayDataHeader = 0x10u;

// Each element of the items array is a 32-bit pointer to a wardrobe entry.
inline constexpr std::uint32_t kElementStride = 0x04u;

// Item id field inside a wardrobe entry. The swap scans for this value.
inline constexpr std::uint32_t kEntryToItemId = 0x28u;

// Upper bound on the number of wardrobe entries. A count above this means the
// chain landed somewhere wrong, so the scan refuses instead of walking heap
// until the process dies.
inline constexpr int kMaxEntries = 8192;

} // namespace list

// ----------------------------------------------------------------- timing ----
//
// The swap loop runs on its own worker thread. Keep the period short enough
// that a re-rolled wardrobe is re-applied inside a second, and long enough
// that the worker never competes with the game.

namespace timing {

inline constexpr unsigned kWorkerSleepMs = 16;     // one frame at 60 fps
inline constexpr int kCommitEveryTicks = 50;       // ~0.8 s between commits
inline constexpr unsigned kAttachRetryMs = 1000;   // re-resolve when not attached

} // namespace timing

// --------------------------------------------------------------- catalog -----
//
// Item database. Not a memory value, but it lives here because it is the
// other thing that breaks when the game changes: the endpoint that serves the
// item list has to match the build the offsets above describe.

namespace catalog {

inline constexpr const char* kDefaultUrl =
	"https://raw.githubusercontent.com/OWNER/REPO/main/clothes.json";

// A response larger than this is rejected as the wrong file.
inline constexpr std::size_t kMaxResponseBytes = 8u * 1024u * 1024u;

} // namespace catalog

} // namespace offsets
