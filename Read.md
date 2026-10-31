I've completed a thorough analysis of the Old Sources project. Here's how it connects to BlueStacks Free Fire.
Architecture Overview
This is a user-mode, external memory R/W tool for BlueStacks 5. It contains no kernel driver. Instead, it abuses the fact that BlueStacks' own Virtual Machine Monitor (BstkVMM.dll) lives inside the HD-Player.exe process and already holds the entire Android guest RAM mapped as host pointers. The tool reads/writes the emulator process to reach the game's memory inside the VM.
Your Client.exe
   └─ ReadProcessMemory / WriteProcessMemory  (Windows user-mode)
        └─ HD-Player.exe  (BlueStacks player process)
             └─ BstkVMM.dll  (the hypervisor/VMM)
                  └─ VM object → chunk/range trees → host pointers to guest RAM
                       └─ Guest physical pages
                            └─ Guest page tables (CR3) → libil2cpp.so (the game)
The Connection Chain (step by step)
Step 1 — Locate the emulator process
app_config.hpp defines the two targets:
- Process: HD-Player.exe (the BlueStacks Android player)
- Module: BstkVMM.dll (the virtual machine monitor)
memory_engine.cpp:initialize() finds the PID via CreateToolhelp32Snapshot, then opens a handle using a stealth path (open_process_stealth) that:
1. Enables SeDebugPrivilege
2. Dumps the system handle table via NtQuerySystemInformation(SystemExtendedHandleInformation) (class 64)
3. Steals an existing handle to HD-Player.exe from another process using DuplicateHandle (this avoids the noisy OpenProcess call), falling back to NtOpenProcess if that fails.
Step 2 — Find the VM object inside BstkVMM.dll
locate_vm_info() walks the emulator's virtual memory with VirtualQueryEx, looking for a large committed RW region that matches the internal layout of the BlueStacks VM control block. It validates candidates against two hardcoded offset profiles:
- k_profile_bstk_vmm (older/larger layout — vm_cpu_array at 0x11FE00, etc.)
- k_profile_bstk (newer compact layout — vm_cpu_array at 0x24000, etc.)
Validation checks: plausible cpu_count (1–8), a pvcpu pointer with the expected 0x120000 delta, a sane read_counter, and a valid range_tree_root whose nodes look like guest-physical ranges. Once matched, it records pvm (the VM base) and the chosen offset profile — even auto-adjusting the profile by a detected "VM Shift."
It also pattern-scans BstkVMM.dll for the physical-chunk-map function (pgmPhysChunkMap) so it can later call into BlueStacks' own code to force-map a chunk.
Step 3 — Translate guest physical → host pointer
This is the heart of it (map_physical_to_host / map_physical_page_to_host_base). To read any guest-physical address it:
1. Checks a page-map cache (try_page_map_cache)
2. Finds the guest memory range via an rb-tree (find_physical_range)
3. Reads the page descriptor to classify the page, then resolves the host base from the chunk cache, chunk tree, direct-RAM window, or MMIO table
4. If a chunk isn't resident, it injects a small shellcode stub into HD-Player.exe and executes BlueStacks' own pgmPhysChunkMap (via CreateRemoteThread, or stealthier APC / thread-hijack paths) to force the chunk to be mapped, then reads the returned pointer.
All of these are plain ReadProcessMemory/WriteProcessMemory calls against host addresses.
Step 4 — Walk the guest Linux kernel to find Free Fire
BlueStacks runs Android on a Linux kernel, and the tool reads that kernel's memory directly (it's just more guest physical memory). bluestacks.cpp:setup():
1. Starts from the fixed guest symbol kernel::k_init_task (0xFFFFFFFF822147C0) — the init_task address for that specific BlueStacks kernel build.
2. Walks the process list (task_struct.tasks linked list, both directions) reading each task's comm, cmdline, pid, mm.
3. Scores each task against the package name — com.dts.freefireth (Free Fire) or com.dts.freefiremax (Max), with cross-matching.
4. From the winning task's mm_struct, reads mm->pgd and converts it to a physical address → this is the game's CR3 (page-table base), which is set globally for all subsequent user-memory access.
It retries this whole loop up to 20 times, 500 ms apart (so the game must be launching/running).
Step 5 — Locate the game's code (libil2cpp.so)
get_module_base("libil2cpp.so") walks the guest's VMA list (mm_struct.mmap linked list, up to 8192 VMAs), reading each vm_file → dentry → filename to find mappings named libil2cpp.so. It then ranks candidates (preferring armeabi-v7a, then x86 fallbacks), reads the ELF header to get e_machine, and validates by following an internal GameEngine pointer chain to reject linker-namespace stubs. GameMemory::Attach() wraps this and stores the chosen base + architecture (32-bit vs 64-bit).
Step 6 — Read/write the game's memory
With CR3 + libil2cpp.so base in hand, every game read/write goes through:
- translate_guest_va(): full x86-64 4-level page-table walk (PML4→PDPT→PD→PT) reading page-table entries as guest-physical memory, with heavy caching.
- read_guest_user / write_guest_user: page-by-page translation + host read/write.
How the skin changer rides on top
ClothChanger.cpp works entirely in 32-bit guest virtual addresses (the game is armeabi-v7a):
- Hardcoded RVAs into libil2cpp.so (e.g. kInitBaseRva = 0xA342EFC) point at il2cpp statics that resolve to the GameEngine/MatchGame/Player object chain.
- TryGetMatchContext() walks: InitBase → GameFacade/MatchGame → LocalPlayer → AvatarManager → UmaAvatar(s), and distinguishes Lobby vs in-match state.
- CommitCloths() finds the wardrobe DB items array, then PatchCategory() overwrites the per-category cloth ID fields via WriteU32, bumps version counters (BumpVersions), and pushes "recipes" to the UMA avatar to force a visual re-bake.
Key things to know before reusing this
1. Offset fragility. Three hardcoded offset sets must all match your exact setup, or it silently fails:
- kernel::k_init_task — specific to the BlueStacks guest kernel build.
- The two c_dynamic_offsets VM profiles + the pgmPhysChunkMap RVA — specific to the BstkVMM.dll version (there's already a fallback comment for BlueStacks 5.22.210 / 0xDA0C0).
- The il2cpp RVAs and struct offsets in ClothChanger.cpp — specific to the game build.
2. It only supports 32-bit guests (armeabi-v7a / legacy x86) for the actual cloth writes; arm64 is detected but flagged "cloth may fail."
3. The diagnostic binaries (diag_vm.exe, diag_inspect, diag_items, etc.) were clearly used to reverse each offset layer — diag_vm.cpp in particular is the tool that validated the VM-profile offsets against a live HD-Player.exe PID.
Recommended debugging order if it won't connect
1. Run diag_vm-style checks first: confirm HD-Player.exe PID, that BstkVMM.dll loads, and that a VM region validates (CPU count, range tree).
2. Confirm guest init_task walk finds com.dts.freefireth (kernel offsets correct).
3. Confirm libil2cpp.so base resolves with a valid GameEngine chain.
4. Only then attempt CommitCloths.
Would you like me to map the exact offset table to a specific BlueStacks + Free Fire version, or trace one specific path (e.g. the PatchCategory field-write logic) in more detail?