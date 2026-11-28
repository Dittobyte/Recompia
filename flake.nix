{
  description = "Recompia — native Linux launcher and runtime";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs { inherit system; };
      inherit (pkgs) lib;

      version =
        let
          conf = builtins.fromJSON (builtins.readFile ./launcher/src-tauri/tauri.conf.json);
        in
        conf.version;

      # Pinned Ryubing checkout for the offline Maxwell -> SPIR-V shader translator (.github/workflows/release.yml).
      ryubingSrc = pkgs.fetchzip {
        url = "https://git.ryujinx.app/projects/Ryubing/archive/a82350bb774f70fcbd41c9987bf67a3775409963.tar.gz";
        hash = "sha256-PrfT+5BKYIe4+3xRkQtIBloF4r20jXdepBJlyNqJS9I=";
      };

      # Pinned third-party sources from runtime/cmake/gfx_deps.cmake for offline Nix builds.
      gfxDeps = {
        sdl3 = pkgs.fetchurl {
          url = "https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz";
          sha256 = "9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3";
        };
        vulkan_headers = pkgs.fetchurl {
          url = "https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.zip";
          sha256 = "f4be95220ff0ee0b1c620301ff4fe9ae48592d71f4c30f2d5c571351a153ae81";
        };
        volk = pkgs.fetchurl {
          url = "https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.zip";
          sha256 = "889b90fdcbe9cee7b118339d640c6895b7be5c920d309c44b744692063275b3d";
        };
        vma = pkgs.fetchurl {
          url = "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.zip";
          sha256 = "ce9a61ea5e72284ac9ccc152c214eef381ab57bfd7383f20232e5a652e3afda6";
        };
        imgui = pkgs.fetchurl {
          url = "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.zip";
          sha256 = "e1c46d676c2bcb7ced847ba27f50553e33a19db97b3cadaec7f8be64449139f8";
        };
        zstd = pkgs.fetchurl {
          url = "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz";
          sha256 = "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3";
        };
      };

      recompia-frontend = pkgs.buildNpmPackage {
        pname = "recompia-frontend";
        inherit version;
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./launcher/package.json
            ./launcher/package-lock.json
            ./launcher/index.html
            ./launcher/vite.config.js
            ./launcher/src
            ./launcher/scripts
            ./runtime/docs
          ];
        };
        sourceRoot = "source/launcher";
        npmDepsHash = "sha256-QuFiy6pzrtYYYaGFfYPXmGB3pGaQGznoorOj+kFiAdU=";
        installPhase = ''
          runHook preInstall
          cp -r dist "$out"
          runHook postInstall
        '';
      };

      recompia-launcher = pkgs.rustPlatform.buildRustPackage {
        pname = "recompia-launcher";
        inherit version;
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./launcher/src-tauri
            ./launcher/relay
            ./launcher/cloud-gateway
            ./launcher/examples/theme-example
            ./runtime/third_party/modtopia
          ];
        };
        cargoRoot = "launcher/src-tauri";
        buildAndTestSubdir = "launcher/src-tauri";
        cargoLock.lockFile = ./launcher/src-tauri/Cargo.lock;
        cargoBuildFlags = [ "--features" "tauri/custom-protocol" ];

        nativeBuildInputs = with pkgs; [
          pkg-config
        ];

        buildInputs = with pkgs; [
          glib
          gtk3
          librsvg
          libsoup_3
          openssl
          udev
          webkitgtk_4_1
        ];

        preBuild = ''
          mkdir -p launcher/dist
          cp -r ${recompia-frontend}/* launcher/dist/
        '';

        meta = with lib; {
          description = "Recompia Tauri launcher";
          license = licenses.gpl3Plus;
          platforms = [ "x86_64-linux" ];
        };
      };

      pythonWithPillow = pkgs.python3.withPackages (ps: [ ps.pillow ]);

      recompia-runtime = pkgs.stdenv.mkDerivation {
        pname = "recompia-runtime";
        inherit version;
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./game-build
            ./recomp
            ./runtime
            ./tools/kbm_icons.py
            ./tools/shaders
            ./tools/textures
            ./tools/tiling
          ];
        };

        nativeBuildInputs = with pkgs; [
          cmake
          glslang
          ninja
          pkg-config
          pythonWithPillow
          unzip
        ];

        buildInputs = with pkgs; [
          alsa-lib
          dbus
          fribidi
          libdrm
          libgbm
          libGL
          libpulseaudio
          libthai
          libvpx
          libxkbcommon
          pipewire
          udev
          vulkan-headers
          vulkan-loader
          wayland
          wayland-protocols
          libx11
          libxcb
          libxcursor
          libxext
          libxfixes
          libxi
          libxrandr
          libxscrnsaver
          libxtst
        ];

        dontUseCmakeConfigure = true;
        dontUseCmakeBuild = true;
        dontUseCmakeInstall = true;

        configurePhase = ''
          runHook preConfigure

          mkdir -p "$TMPDIR/gfx-deps"
          unpack_dep() {
            local name="$1"
            local archive="$2"
            local stage="$TMPDIR/gfx-deps-stage-$name"
            mkdir -p "$stage"
            case "$archive" in
              *.tar.gz) tar -xzf "$archive" -C "$stage" ;;
              *.zip) unzip -q "$archive" -d "$stage" ;;
            esac
            local top
            top=$(find "$stage" -mindepth 1 -maxdepth 1 -type d | head -n1)
            mv "$top" "$TMPDIR/gfx-deps/$name"
          }

          unpack_dep sdl3 "${gfxDeps.sdl3}"
          unpack_dep vulkan_headers "${gfxDeps.vulkan_headers}"
          unpack_dep volk "${gfxDeps.volk}"
          unpack_dep vma "${gfxDeps.vma}"
          unpack_dep imgui "${gfxDeps.imgui}"
          unpack_dep zstd "${gfxDeps.zstd}"

          cmake -S game-build -B build/rtdll -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            -DPython3_EXECUTABLE="${pythonWithPillow}/bin/python3" \
            -DFETCHCONTENT_SOURCE_DIR_SDL3="$TMPDIR/gfx-deps/sdl3" \
            -DFETCHCONTENT_SOURCE_DIR_VULKAN_HEADERS="$TMPDIR/gfx-deps/vulkan_headers" \
            -DFETCHCONTENT_SOURCE_DIR_VOLK="$TMPDIR/gfx-deps/volk" \
            -DFETCHCONTENT_SOURCE_DIR_VMA="$TMPDIR/gfx-deps/vma" \
            -DFETCHCONTENT_SOURCE_DIR_IMGUI="$TMPDIR/gfx-deps/imgui" \
            -DFETCHCONTENT_SOURCE_DIR_ZSTD="$TMPDIR/gfx-deps/zstd" \
            -DPKP_RT_DLL=ON \
            -DPKP_RELEASE=ON \
            -DNXRT_UPSCALER_FSR3=ON \
            -DPKP_VERSION="${version}"

          runHook postConfigure
        '';

        buildPhase = ''
          runHook preBuild
          cmake --build build/rtdll --target pkp_rt -j "$NIX_BUILD_CORES"
          python3 tools/kbm_icons.py --out kbm_icons
          runHook postBuild
        '';

        installPhase = ''
          runHook preInstall

          local share="$out/share/recompia"
          mkdir -p "$share/tools/build/sdk/kbm_icons" "$share/tools/build/sdk/fonts" \
                   "$share/tools/build/pgo" "$share/tools/shaders" "$share/assets"

          install -m 755 build/rtdll/libpkp_rt.so "$share/libpkp_rt.so"
          install -m 755 build/rtdll/libpkp_rt.so "$share/tools/build/sdk/libpkp_rt.so"

          cp game-build/release_b/pkp_build.py game-build/release_b/dump_hashes.json \
             game-build/release_b/fetch_dlss.py "$share/tools/build/"
          cp game-build/release_b/pgo/pkp_pgo.py game-build/release_b/pgo/profile_writer.cpp \
             game-build/release_b/pgo/session.nxs "$share/tools/build/pgo/"

          cp -R recomp "$share/tools/build/recomp"
          find "$share/tools/build/recomp" -name __pycache__ -type d -exec rm -rf {} + 2>/dev/null || true
          (cd "$share/tools/build/recomp" && find . -type f ! -name '*.pyc' | sed 's|^\./||' | LC_ALL=C sort) > "$share/tools/build/recomp.files"

          cp game-build/src/pkp_stub.cpp game-build/src/pkp_rt_api.h "$share/tools/build/sdk/"
          cp kbm_icons/*.png "$share/tools/build/sdk/kbm_icons/"
          cp runtime/assets/fonts/DejaVuSans.ttf "$share/tools/build/sdk/fonts/"
          cp -R runtime/assets/prompts "$share/assets/prompts"
          cp tools/shaders/ampere/extract_corpus.py "$share/tools/shaders/"

          runHook postInstall
        '';

        meta = with lib; {
          description = "Recompia native Linux runtime and build kit";
          license = licenses.gpl3Plus;
          platforms = [ "x86_64-linux" ];
        };
      };

      recompia-harness = pkgs.buildDotnetModule {
        pname = "recompia-harness";
        inherit version;
        src = lib.fileset.toSource {
          root = ./.;
          fileset = lib.fileset.unions [
            ./tools/shaders
          ];
        };
        projectFile = "tools/shaders/harness/SpirvHarness.csproj";
        dotnet-sdk = pkgs.dotnet-sdk_10;
        dotnet-runtime = pkgs.dotnet-runtime_10;
        selfContainedBuild = true;
        nugetDeps = ./nix/harness-deps.json;
        nativeBuildInputs = [ pkgs.gnupatch ];
        preConfigure = ''
          bash tools/shaders/scripts/setup_ryujinx.sh "${ryubingSrc}"
          chmod -R u+w tools/shaders/third_party/ryubing
          rm -f tools/shaders/third_party/ryubing/nuget.config
        '';

        meta = with lib; {
          description = "Recompia offline Maxwell-to-SPIR-V shader translator";
          license = licenses.gpl3Plus;
          platforms = [ "x86_64-linux" ];
        };
      };

      desktopItem = pkgs.makeDesktopItem {
        name = "recompia";
        desktopName = "Recompia";
        comment = "Native PC port launcher";
        exec = "recompia";
        icon = "recompia";
        categories = [ "Game" ];
      };

      recompia = pkgs.buildFHSEnv {
        name = "recompia";

        targetPkgs = pkgs: with pkgs; [
          alsa-lib
          atk
          cairo
          curl
          dbus
          freetype
          gdk-pixbuf
          glib
          glib-networking
          gsettings-desktop-schemas
          gtk3
          harfbuzz
          libdrm
          libGL
          libpulseaudio
          librsvg
          libsoup_3
          libxkbcommon
          openssl
          pango
          pipewire
          stdenv.cc.cc.lib
          udev
          vulkan-loader
          wayland
          webkitgtk_4_1
          xdg-utils
          libx11
          libxcb
          libxcursor
          libxext
          libxfixes
          libxi
          libxrandr
          libxscrnsaver
          libxtst
          zlib
        ];

        runScript = pkgs.writeShellScript "recompia-run" ''
          set -eu
          data_dir="''${XDG_DATA_HOME:-$HOME/.local/share}/recompia"
          rt_share="${recompia-runtime}/share/recompia"

          mkdir -p "$data_dir/tools/build" "$data_dir/tools/shaders" "$data_dir/assets"
          ln -sfn "$rt_share/libpkp_rt.so" "$data_dir/libpkp_rt.so"
          ln -sfn "$rt_share/assets/prompts" "$data_dir/assets/prompts"
          ln -sfn "$rt_share/tools/shaders/extract_corpus.py" "$data_dir/tools/shaders/extract_corpus.py"

          if [ -d "$data_dir/tools/harness" ] && [ ! -L "$data_dir/tools/harness" ]; then
            rm -rf "$data_dir/tools/harness"
          fi
          ln -sfn "${recompia-harness}/lib/recompia-harness" "$data_dir/tools/harness"

          for f in pkp_build.py dump_hashes.json fetch_dlss.py pgo recomp recomp.files sdk; do
            ln -sfn "$rt_share/tools/build/$f" "$data_dir/tools/build/$f"
          done

          export GIO_MODULE_DIR="${pkgs.glib-networking}/lib/gio/modules"
          export XDG_DATA_DIRS="${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
          export APPIMAGE="$data_dir/recompia"
          exec "${recompia-launcher}/bin/recompia" "$@"
        '';

        extraInstallCommands = ''
          mkdir -p "$out/share/applications" "$out/share/icons/hicolor/128x128/apps"
          cp "${desktopItem}/share/applications/recompia.desktop" "$out/share/applications/"
          cp "${./launcher/src-tauri/icons/128x128.png}" "$out/share/icons/hicolor/128x128/apps/recompia.png"
        '';

        meta = with lib; {
          description = "Recompia launcher and runtime wrapped for NixOS";
          license = licenses.gpl3Plus;
          mainProgram = "recompia";
          platforms = [ "x86_64-linux" ];
        };
      };

      devLibs = with pkgs; [
        alsa-lib
        curl
        dbus
        fribidi
        glib
        gtk3
        libdrm
        libgbm
        libGL
        libpulseaudio
        librsvg
        libsoup_3
        libthai
        libvpx
        libxkbcommon
        openssl
        pipewire
        udev
        vulkan-loader
        wayland
        webkitgtk_4_1
        libx11
        libxcursor
        libxext
        libxfixes
        libxi
        libxrandr
        libxscrnsaver
        libxtst
        zlib
      ];
    in
    {
      packages.${system} = {
        default = recompia;
        inherit recompia recompia-launcher recompia-runtime recompia-frontend recompia-harness;
      };

      apps.${system}.default = {
        type = "app";
        program = "${recompia}/bin/recompia";
        meta = recompia.meta;
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = with pkgs; [
          cargo
          cargo-tauri
          clang
          clippy
          cmake
          dotnet-sdk_10
          gdb
          glslang
          lld
          ninja
          nodejs_22
          pkg-config
          (python3.withPackages (ps: [ ps.capstone ps.lz4 ps.pillow ]))
          rustc
          rustfmt
          uv
          vulkan-headers
          vulkan-validation-layers
          wayland-protocols
          zig
        ] ++ devLibs;

        shellHook = ''
          export LD_LIBRARY_PATH="${lib.makeLibraryPath devLibs}''${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
          export GIO_MODULE_DIR="${pkgs.glib-networking}/lib/gio/modules"
          export XDG_DATA_DIRS="${pkgs.gsettings-desktop-schemas}/share/gsettings-schemas/${pkgs.gsettings-desktop-schemas.name}:${pkgs.gtk3}/share/gsettings-schemas/${pkgs.gtk3.name}''${XDG_DATA_DIRS:+:$XDG_DATA_DIRS}"
        '';
      };
    };
}
