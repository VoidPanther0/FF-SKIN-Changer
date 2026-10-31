#pragma once

// ---------------------------------------------------------------------------
// offsets.h
//
// Every game-build-specific value in one place. A new game patch normally
// means editing this file only; nothing below knows a raw number.
//
// Verified against
//   Free Fire 1.132.1 (build 2019121229), Il2CppDumper full dump, both the
//   ARMv7 and the V8A image. Identical class layouts in both.
//
// How to update after a patch
//   1. target::kModuleNames / kHostProcesses  - only when the emulator or the
//      game module name changed.
//   2. chain::kBssSlotRva                     - RVA of the static pointer slot
//      of SingletonModule<AvatarWardrobeDataManager>. This is the one that
//      moves on every patch.
//   3. chain::* field offsets                 - runtime struct layout, see the
//      notes on each value.
//   4. list::*                                - the wardrobe list layout, all
//      confirmed from the dump.
//
// Confirmed from the dump (safe to trust until the class changes)
//   COW.AvatarWardrobeData.iID                      0x28   (TypeDefIndex 10833)
//   AvatarWardrobeDataManager.m_dictIdToWardrobeDataTree  0x0C (TypeDefIndex 13488)
//   IntervalTreeDic<T>.m_Data                       0x0C   second field
//   List<T>._items / _size                          0x08 / 0x0C
//   SingletonModule<T>._instance                    0x00   first static field
//
// Not in the dump (runtime struct layout, re-check when the Unity version
// changes, not on every content patch)
//   chain::kMethodInfoToKlass     Il2CppClass* inside MethodInfo
//   chain::kKlassToStaticFields   static_fields inside Il2CppClass
//   list::kArrayDataHeader        0x10 because the vector is ALIGN_TYPE(8);
//                                 older Unity versions use 0x0C here
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
//   COW.AvatarWardrobeDataManager : SingletonModule<AvatarWardrobeDataManager>
//   (TypeDefIndex 13488)
//
//   [image base + kBssSlotRva]              static pointer slot in BSS
//     -> baseMethod
//   [baseMethod]                             MethodInfo*
//   [baseMethod + kMethodInfoToKlass]        Il2CppClass*
//   [klass + kKlassToStaticFields]           StaticFields*
//   [staticFields + kStaticFieldsInstance]   the singleton instance
//
// _instance is a static field of the generic base SingletonModule<T>, so the
// class the chain has to land on is the instantiated generic
// SingletonModule<AvatarWardrobeDataManager>, NOT AvatarWardrobeDataManager
// itself. The derived class declares only consts, so its own static field
// block holds nothing and the chain would read a null instance.
//
// The BSS slot holds a pointer to a small structure whose first field is a
// MethodInfo of a method on that generic class; get_instance() is the usual
// one. That is the normal IL2CPP route to a class without walking the type
// cache.

namespace chain {

// RVA of the static pointer slot. Moves on almost every game patch. Two slots
// resolve to the same singleton on build 2019121229: one is populated early
// in the lifecycle, the other after the lobby load; the loader tries primary
// first, then alt, and takes whichever comes back non-null.
inline constexpr std::uint32_t kBssSlotRva = 0xA344250u;
inline constexpr std::uint32_t kBssSlotRvaAlt = 0xA987F88u;

// MethodInfo::klass. The 32-bit layout is methodPointer, invoker_method, name,
// klass, which puts klass at 0x0C on most Unity versions and at 0x10 on the
// builds that insert virtualMethodPointer first. The game responds to 0x10;
// if a patch stops resolving, try 0x0C before anything else.
inline constexpr std::uint32_t kMethodInfoToKlass = 0x10u;

// Il2CppClass::static_fields. Runtime struct, not in the dump.
inline constexpr std::uint32_t kKlassToStaticFields = 0x5Cu;

// SingletonModule<T>._instance is the only static field of the generic class,
// so it sits at the start of the static field block. Confirmed from the dump.
inline constexpr std::uint32_t kStaticFieldsInstance = 0x00u;

} // namespace chain

// ------------------------------------------------------------ live objects ---
//
// Resolved one field at a time from the singleton instance. Every offset below
// is confirmed against the 1.132.1 dump.
//
//   AvatarWardrobeDataManager
//     +0x0C  IntervalTreeDic<AvatarWardrobeData> m_dictIdToWardrobeDataTree
//   IntervalTreeDic<T>
//     +0x08  IntervalTree m_Tree
//     +0x0C  List<T> m_Data
//   List<T>
//     +0x08  T[] _items
//     +0x0C  int  _size
//   T[]
//     +0x10  first element
//   AvatarWardrobeData
//     +0x08  int recipeHashInLobby    (M)
//     +0x0C  int recipeHashInGame     (M)
//     +0x10  int recipeHashInLobby_F  (F)
//     +0x14  int recipeHashInGame_F   (F)
//     +0x28  uint iID
//     +0x2C  byte wardrobeType  (1=Head, 3=Chest, 4=Legs, 5=Feet,
//                                 8=Face, 11=Hair, 12=Set, 14=HeadAdd)

namespace list {

inline constexpr std::uint32_t kInstanceToTreeDict = 0x0Cu;   // m_dictIdToWardrobeDataTree
inline constexpr std::uint32_t kTreeDictToDataList = 0x0Cu;   // IntervalTreeDic<T>.m_Data
inline constexpr std::uint32_t kDataListToItems = 0x08u;      // List<T>._items
inline constexpr std::uint32_t kDataListToCount = 0x0Cu;      // List<T>._size

// Managed array header on a 32-bit build: klass + monitor + max_length is
// 0x0C, but the element vector is ALIGN_TYPE(8), which rounds the data start
// to 0x10. Older Unity versions leave it at 0x0C; a scan that finds nothing
// on a known-good list is usually this one.
inline constexpr std::uint32_t kArrayDataHeader = 0x10u;

// Each element of the items array is a 32-bit pointer to a wardrobe entry.
inline constexpr std::uint32_t kElementStride = 0x04u;

// AvatarWardrobeData.iID, the item id. Confirmed at 0x28.
inline constexpr std::uint32_t kEntryToItemId = 0x28u;

// Four 32-bit recipe hashes starting at +0x08: InLobby(M), InGame(M),
// InLobby(F), InGame(F). Overwriting these with another item's hashes is what
// actually re-skins the avatar — swapping the slot pointer alone doesn't
// re-bake the character mesh.
inline constexpr std::uint32_t kEntryToRecipeHashes = 0x08u;
inline constexpr std::uint32_t kRecipeHashesSize = 0x10u;

// AvatarWardrobeData.wardrobeType, the slot a piece belongs to.
inline constexpr std::uint32_t kEntryToWardrobeType = 0x2Cu;

// EWardrobeType constants. The game writes any item of a slot to the same
// bucket; a top id lives next to another top id, never next to a mask.
inline constexpr std::uint8_t kWtHead     = 1;
inline constexpr std::uint8_t kWtChest    = 3;
inline constexpr std::uint8_t kWtLegs     = 4;
inline constexpr std::uint8_t kWtFeet     = 5;
inline constexpr std::uint8_t kWtFace     = 8;
inline constexpr std::uint8_t kWtHair     = 11;
inline constexpr std::uint8_t kWtSet      = 12;
inline constexpr std::uint8_t kWtHeadAdd  = 14;

// The manager sizes its own table at CapacityClothData = 7000, so a sane
// ceiling sits just above that. A count past this means the chain landed
// somewhere else, so the scan refuses instead of walking heap until the
// process dies.
inline constexpr int kMaxEntries = 8192;

} // namespace list

// ----------------------------------------------------------- lobby avatar ----
//
// After the recipe hashes change the game needs to know its avatar is stale,
// otherwise the lobby holds the cached mesh until the player leaves. These
// fields are the dirty flags the game checks on its own rebuild tick.
//
//   UIAvatarBase.m_PendingChangesForBuild  @ 0x79  (write 1)
//   UIAvatar.IsDirty                       @ 0xAA  (write 1)
//   UIStatedAvatar.m_InternalState         @ 0x284
//   InternalState.CurrentStateDirty        @ 0x51  (write 1)
//
// Static-fields RVA of UIMaleAvatar.LocalAvatar lives at kGetLocalAvatarRva;
// walking from there finds the local UIAvatar instance without a scene scan.

namespace avatar {

inline constexpr std::uint32_t kInitBaseRva = 0xA342EFCu;      // GameFacade / MatchGame chain head
inline constexpr std::uint32_t kGetLocalAvatarRva = 0x1AD4758u; // UIMaleAvatar.get_LocalAvatar

inline constexpr std::uint32_t kUIAvatarPendingBuild = 0x79u;
inline constexpr std::uint32_t kUIAvatarIsDirty = 0xAAu;
inline constexpr std::uint32_t kUIStatedInternal = 0x284u;
inline constexpr std::uint32_t kInternalDirty = 0x51u;

} // namespace avatar

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

// Local file next to the exe is tried first; the loader also accepts an
// http(s):// URL and switches to the HTTP path automatically.
inline constexpr const char* kDefaultUrl = "clothes.json";

// A response larger than this is rejected as the wrong file.
inline constexpr std::size_t kMaxResponseBytes = 8u * 1024u * 1024u;

} // namespace catalog

} // namespace offsets
