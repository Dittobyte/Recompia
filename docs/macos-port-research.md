# macOS on Apple Silicon: research

The pickup point for native macOS (arm64) support. It compares the two ports that exist, says what each would need to merge, and
proposes an order of work. No port code is in this change.

Method: every statement about a port comes from reading its diff. Line references are `A:n` for the anonymous patch and `P:n` for
the diff of #525 (against its merge base on `main`, `a799f18`). What has to be shown on a Mac is listed in section 7.

## 1. The two ports

| | **Port P**: pull request #525 (continues #479) | **Port A**: a patch contributed anonymously |
|---|---|---|
| Size | 126 files, +17,068 / -282, 46 new files | 102 files, +4,571 / -463, no new file |
| Shape | A POSIX layer behind the Win32 headers: on non-Windows `<windows.h>` resolves to `runtime/src/os/compat/windows.h`, and `runtime/src/os` implements about 230 Win32 entry points on POSIX and Mach (6,270 lines in 26 files). The runtime keeps calling Win32. | Platform branches inside the existing files: 104 blocks test `__APPLE__`, 357 touched blocks test `_WIN32`. `memory.cpp` gains 593 lines, `write_watch.cpp` 275, `fs_volume.cpp` 206, and CoreAudio sits inside `audio_xaudio2.cpp`. |
| Existing runtime sources touched | 42 files under `runtime/src` (+809 / -79) | 73 files under `runtime/src` (+3,720 / -311) |
| State by its own account | Boots into gameplay on an M3; milestones M0 to M3 and M5 done, M4 and M6 partial (`docs/macos-port.md` in the branch) | README text promises a setup script and a tarball; `game-build/scripts/setup-macos.sh`, the packaging and the `tools/macos` bundles are not in the patch |
| Applies to `testing` today | Made against current `main`; conflicts to be measured when it is rebased | 92 of 102 files apply; 10 fail (`.gitignore`, `pkp_main.cpp`, `gamebuild.rs`, `shaders.rs`, `gfx.cmake`, `memguard.cpp`, `nvn_pool_copy.cpp`, `vk_device.cpp`, `host_device.cpp`, `pokopia-coop/CMakeLists.txt`), all changed on `testing` on 9 and 10 November |
| Builds on macOS as it is | Its CI job builds the runtime and the demo on a macOS runner | No: `gfx/extras/ffx_dll.cpp` includes `<windows.h>` and is always compiled; `game-build/src/pkp_stub.cpp` uses `__declspec(dllexport)` without `-fdeclspec` |

Both are arm64 only, keep Vulkan through MoltenVK, use the GPU's own ASTC support, and reserve the guest's fixed address range.

## 2. How each one solves the hard parts

**Guest address space.** The runtime needs exact addresses (`nxrt/memory.h`: `kMainBase 0x7100000000`, `kHeapBase 0x7800000000`),
and on macOS the system allocator maps slabs in `0x70_0000_0000`..`0x80_0000_0000` before `main`.
- P patches a 72 GiB `__GUEST` segment into the executable's Mach-O header after every link (`runtime/cmake/guest_segment.py`,
  P:3554-3679), so the range is taken before any library runs, and refuses to start if the layout is not inside it (P:5670-5694).
- A re-executes the game with `MallocZeroOnFree=0` to keep the allocator out of the range (A:547-559), then reserves a 64 GiB arena
  with `mach_vm_map(VM_FLAGS_FIXED)` and falls back to per-region reservations with a log line if that fails (A:6395-6423).
- Both move parts of the guest layout to fit (alias, stacks, kernel or HLE bases). In P the move is not limited to macOS: the
  Windows layout changes too (P:3750-3775).

**16 KiB host pages, 4 KiB guest pages.** Both keep 4 KiB state and give a host page the most permissive protection of the guest
pages in it (P:11168-11186, A:6306-6341). Both decommit with `mprotect` only: memory is not returned and a recommitted page is not
zeroed (P:11441-11460, A:6974-6988).

**Faults and write watching.** Both use BSD signals on the faulting thread, not Mach exception ports.
- P: a lock-free handler table, a 64 KiB alternate signal stack per thread, and a `sigsetjmp` stand-in for `__try`
  (P:9895-10251). Every fault is reported as a write (P:10052-10058).
- A: reads the access kind from ESR (A:7455-7471), has no alternate stack, builds the crash report inside the handler with
  `std::string` and `fopen`, and takes locks there (A:2188, A:7402-7443). The fault guards around guest copies become plain
  `memcpy` (A:3604-3607, A:4031-4036).

**Threads and waiting.** P emulates Win32 threads, events, timers and `WaitForMultipleObjects` over pthreads; A ports each call
site to pthreads and Mach. Neither has thread affinity. P adds `BigThread` (16 or 32 MiB stacks) for 13 renderer threads because
the 512 KiB default overflowed (P:5266-5399).

**Audio.** P: an AudioUnit sink in its own file with a lock-free ring, device choice by UID and underrun counting
(P:12269-12524). A: an AudioQueue on the default device inside `audio_xaudio2.cpp`, no device choice, underruns always 0
(A:8556-8755).

**Graphics.** Both enable `VK_KHR_portability_enumeration` and `VK_KHR_portability_subset`. Both force the pool shadow copy.
P turns the MoltenVK pipeline cache off by default (P:4795-4807) and adds `third_party/bc7enc` for the prompt atlas; A writes a
mode-6 BC7 encoder into `tools/textures/src/a2b.cpp` (A:10161-10250). DLSS, FSR3 and in P also NIS are off. HDR: P asks
CoreGraphics, A stubs the query.

**Player build and launcher.** Both add a macOS path to `pkp_build.py` (system clang, `libpkp_rt.dylib`, a 128 MiB main stack)
and a native host kind in the launcher. P also has `.app` and `.dmg` scripts (ad-hoc signed) and a memory-pressure guard. A's
`pkp_build.py` passes no `-arch`, so a Python running under Rosetta would build x86-64 objects (A:183-335).

## 3. Problems found by reading

**Port P**
- `runtime/cmake/gfx.cmake` includes `launcher.cmake` on Windows (P:3466). That file is on neither `main` nor `testing`.
- Four new POSIX sources (`audio_coreaudio.cpp`, `audio_devices_posix.cpp`, `movie_posix.cpp`, `temporal_posix.cpp`) carry no
  platform guard and the Windows source glob does not exclude them.
- `diag/crash.cpp` reads `cx->pc`, `sp`, `fp`, `lr` outside any Apple guard (P:3926-3927); the x64 `CONTEXT` has no such fields.
- `pkp_build.py` gets a build lock for every platform, placed before the `try` that turns failures into `@error` lines, and it
  tests the owner with `os.kill(pid, 0)`, which on Windows sends a console control event to that process (P:1207-1223).
- The intermittent boot hang the branch reports has a likely cause in the layer itself: a thread created `CREATE_SUSPENDED`
  waits on a condition variable while its suspend count is above zero (P:8523-8527), and the Apple branch of `ResumeThread`
  lowers the count without signalling it (P:8672-8677; the non-Apple branch signals).
- TCP sends are not protected against `SIGPIPE`, so a Link Play peer that disconnects can end the process.
- The updater looks for `-macos-arm64.zip`; the packaging script writes a `.dmg` (P:2971, P:1456).
- Changes that reach Windows and Linux players: the guest layout move, the crash reporter moved to a helper thread, save work
  folders per instance, `pcopy_land` skipping destinations outside heap and alias (the copy path is on under Proton), and prompt
  icons showing keyboard glyphs until a pad is used (P:5475).

**Port A**
- The two build blockers in the table above, and the missing setup and packaging scripts.
- Signal-handler safety, no alternate signal stack, and a crash handler that can return into the same fault (A:2189-2190).
- `VM_FLAGS_OVERWRITE` mappings that can replace neighbouring pages in the same 64 KiB granule (A:5699-5721).
- Write watch arms a page by `is_mapped`, not by "writable": a page the guest made read-only can come back writable (A:7583).
- A thread's `host_handle` is written after `pthread_create` (A:6045-6054).
- `runtime/CMakeLists.txt` accepts Windows or Apple only, and the generic non-Windows branches would not compile elsewhere
  (for example an unbalanced brace in `heap_alias`, A:6996-7001), so the work does not carry over to a native Linux build.
- Changes that reach Windows and Linux players without a guard: the async pipeline queue order and default thread count
  (A:3507-3553), portability enumeration wherever the loader offers it (A:4443-4456), and `auto_copy` whenever
  `VK_EXT_external_memory_host` is missing (A:3588).

## 4. Fit with the project's rules

- **Scope.** PROJECT.md section 3 defines the project as native Windows x64, also played on Linux through Proton. macOS is a new
  host, so it starts with the repo owner's decision. Issue #199 has the earlier answers; on #479 the owner welcomed the work.
- **Natural home (sections 1, 4.1, 7).** The runtime has no OS layer today: 74 files include `<windows.h>`, there are 20
  `_WIN32` checks and no `__APPLE__` or `__linux__` check. Issues #506 and #507 plan one: `runtime/include/nxrt/os.h` with
  `os_win32.cpp` and `os_posix.cpp` (memory, faults, threads, time, files, sockets, dynamic libraries). Their counts were taken
  on another tree and need taking again here.
  - A is the pattern section 7 warns about: a second job in files named for the first (`audio_xaudio2.cpp` holding CoreAudio)
    and platform branches through 73 runtime sources.
  - P keeps the platform code in one place, which is closer, but it does so by imitating Win32 instead of defining the
    project's own interface, and it still adds platform checks to about 24 existing files.
- **Shared code knows no game.** P hard-codes the game's four module bases and sizes in `runtime/apps/gfx_demo.cpp`
  (P:3371-3372).
- **Owner rule 6.2.** P adds `.github/workflows/macos.yml`. A release artifact for macOS needs a new job in `release.yml`.
  `game-build/CMakeLists.txt` is under CODEOWNERS and both ports change it. Each of these needs the owner's permission.
- **Owner rule 6.5.** Neither port changes `TRANSLATOR_REV`, `kPrepareScheme` or `prepare_key`. A macOS shader cache has to be
  keyed apart from the Windows one, and `compile_flags.ZIG_MINGW` must not change (it is in every player's object cache key):
  macOS gets its own flag table, as both ports do.
- **Line endings.** P rewrites `launcher/src-tauri/src/launch.rs` from CRLF to LF (P:2380-2448).
- **Third parties.** `bc7enc` (MIT or public domain) and a bundled MoltenVK need entries in
  `runtime/THIRD_PARTY_NOTICES.md`. Port A's author asked not to be named; the project is GPL-3.0-or-later and the code would
  carry the usual SPDX header.
- **Where it lands.** PROJECT.md section 9 and CONTRIBUTING.md say pull requests go against `main`; `docs/testing-builds.md`
  says changes land on `testing` first. A Mac build would reach players as a Testing build, so the port's base branch is the
  owner's call.

## 5. What a build for M-series Macs still needs

1. A decision on the base: P, A, or the planned `nxrt/os.h` with the two ports as sources.
2. A runtime that builds for arm64 macOS without changing what Windows and Linux players get.
3. `nxrt/memory.h` aware of the 16 KiB host page, and the guest range reserved before the allocator (P's Mach-O segment is the
   more robust of the two methods: it does not depend on an allocator setting).
4. The player build: a `clang` flag table for arm64, a Mach-O link in `pkp_build.py` with an explicit `-arch arm64`, and stamps
   keyed by platform. This touches the same functions as #527.
5. The launcher: a third host kind. `cfg(not(windows))` means "Linux with Proton" in 11 files today.
6. The shader pipeline: an arm64 macOS build of the translator and a cache separate from the Windows one.
7. Packaging: a signed and notarized `.app`, an update asset name the updater and the packaging agree on, a macOS release job.
8. Open in both ports: in-game videos, a real gamepad, Link Play against a peer, performance (P reports about 25 fps in the
   world on an M3 at 1080p internal, limited by the GPU).

## 6. Recommended order

1. **Owner decision** on scope and base (section 5, item 1), and on a macOS CI job.
2. **Small changes that are right on every platform, one pull request each:** the `-mcx16` flag only for x86-64 targets (P), the
   arm64 spin hint in `recomp/include/nxrecomp/ctx.h`, Windows libraries of `pokopia-coop` linked only on Windows (A), the
   prebuilt `astc2bc7` link made optional.
3. **The OS interface** of #507, moved in family by family (time, files, sockets, threads, memory, faults), Windows behaviour
   unchanged, each file keeping one job. P's `runtime/src/os` is the richest source for the POSIX side; A's Mach memory and
   ESR fault decoding are worth taking into it.
4. **The macOS backends** behind that interface: CoreAudio sink in its own file (P), ImageIO album writer, the Mach-O guest
   segment (P), the launcher's native host kind.
5. **Player build and shader pipeline** for arm64 macOS, after #527.
6. **Packaging and a Testing build**, with the owner's permission for the release job.

Taking P as it stands is the shortest route to something that runs, at the cost of a Win32 imitation the project then owns;
the list in section 3 has to be cleared first either way. Taking A as it stands is not recommended: it does not build, it
spreads through the files that change most often, and its unguarded changes reach Windows players.

## 7. To be shown on a Mac

- The runtime, the demo and the game build and start on arm64 macOS from a clean checkout.
- The `ResumeThread` reading in section 3 against the boot hang.
- Write watch and guard pages under 16 KiB host pages; memory use over a long session with `mprotect`-only decommit.
- Audio device change and loss, a gamepad, videos, Link Play with a Windows peer.
- Windows and Linux builds of the same commit: unchanged output for players, unchanged shader and object caches.
