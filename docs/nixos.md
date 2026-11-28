# NixOS

Recompia ships a `flake.nix` for `x86_64-linux` with both the packaged launcher/runtime and a development shell.

## Running on NixOS

Ensure Vulkan is enabled in your system configuration (`hardware.graphics.enable = true` for Mesa/AMD/Intel or the NVIDIA proprietary driver), then run:

```sh
nix run github:Dittobyte/Recompia
```

Or add the flake to your system or Home Manager configuration:

```nix
inputs.recompia.url = "github:Dittobyte/Recompia";
```

Then install `inputs.recompia.packages.${pkgs.stdenv.hostPlatform.system}.default` (`recompia`).

### How the package works

Because the launcher downloads a standalone Python + Zig toolchain on first **Set up** and compiles the game binary (`pkp`) with a standard Linux dynamic linker path (`/lib64/ld-linux-x86-64.so.2`), the Nix package wraps the Tauri launcher, `libpkp_rt.so`, and the shader translator (`spirv_harness`) in `buildFHSEnv`:

- Read-only runtime, SDK, and shader translator files (`libpkp_rt.so`, `tools/build/`, `tools/harness/`, `tools/shaders/extract_corpus.py`, `assets/prompts/`) are symlinked into `${XDG_DATA_HOME:-$HOME/.local/share}/recompia`.
- Downloaded build tools (`tools/linux/`) and compiled game output stay in writable user directories.
- Updates are managed through Nix (`nix flake update`) rather than the launcher's in-place updater. The optional Save Editor (`pokohex`) is not bundled in the Nix package.

## Developing on NixOS

Enter the development shell from a checkout:

```sh
nix develop
```

The shell includes CMake, Ninja, Clang, lld, Vulkan headers/loader/validation layers, glslang, SDL3 system dependencies, Rust (`cargo`, `clippy`, `rustfmt`, `cargo-tauri`), Node.js 22, WebKitGTK 4.1, Python 3 (`capstone`, `lz4`, `pillow`), .NET 10 SDK, and Zig.

Build the runtime (`nxrt`) and graphics demo:

```sh
cmake -S runtime -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DNXRT_BUILD_GAMES=OFF
cmake --build build/linux -j
```

Build and check the launcher:

```sh
cd launcher
npm ci
npm run build
cd src-tauri
cargo test
```
