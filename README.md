<!-- AI AGENTS: README.md is intended for human readers only. Read AGENTS.md for all AI development instructions, architectural requirements, and repository conventions. -->

<h1 align="center">MaaGE</h1>
<p align="center"><strong>Minecraft as a Game Engine</strong></p>
<p align="center">
  A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and limitless modding.
</p>
<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache--2.0-blue?style=flat-square" alt="License: Apache-2.0"></a>
  <img src="https://img.shields.io/badge/language-C-555555?style=flat-square&amp;logo=c&amp;logoColor=white" alt="Language: C">
  <img src="https://img.shields.io/badge/targets-Windows%20%7C%20Linux%20%7C%20macOS-526b80?style=flat-square" alt="Target platforms: Windows, Linux, macOS">
  <a href="https://github.com/FireController1847/MaaGE/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/FireController1847/MaaGE/build.yml?branch=main&amp;event=workflow_dispatch&amp;style=flat-square&amp;label=build" alt="Build Validation status"></a>
</p>

> **MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.**

## About MaaGE

MaaGE is an independent native voxel platform inspired by Minecraft, designed for custom modpacks and gameplay experiences.

The architecture separates three applications:

- **Launcher** — native first-run asset setup; Microsoft authentication and Minecraft ownership verification remain planned. Mojang's proprietary assets will not be distributed.
- **Client** — independently versioned game executable for Launcher-managed installations and startup. Native windowing, rendering, and gameplay are planned.
- **Server** — independent headless operation, with an optional graphical administration interface planned within the same executable.

The portable core targets ISO C90, with platform-specific integrations kept separate.

## Development Status

**Early development.** The Launcher has a native initial setup interface (AppKit on macOS, Win32 on Windows, GTK 3 on Linux) and a resumable asset manager that verifies and reconstructs Minecraft Java Edition 1.7.10's full asset index. After setup, it retains the Core RGB rendering demonstration: native Metal on macOS and Vulkan on Windows/Linux. Client and Server remain startup stubs; gameplay, authentication, game launching, and Server administration UI are not implemented. macOS builds have been compiled locally; Windows/Linux compilation and runtime/UI testing remain outstanding.

Local builds use macOS arm64 with Apple Clang. [Build Validation](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml) runs manually on Windows, Linux, and macOS.

## Build

Release builds optimize for size and strip nonessential symbols; use Debug for source-level debugging. Set `MAAGE_OPTIMIZE_SIZE=OFF` to disable the size-specific build settings.

Use **CMake 3.21+** and a C compiler. The Linux and macOS presets also require **Ninja**. From the repository root:

Linux windowing requires X11 and Wayland development libraries, `wayland-protocols`, `wayland-scanner`, and pkg-config. Either backend can be disabled with `MAAGE_WINDOW_ENABLE_X11` or `MAAGE_WINDOW_ENABLE_WAYLAND`; both are enabled by default. Windows/Linux rendering requires Vulkan development headers and a loader discoverable by CMake. macOS uses Xcode's AppKit, Metal, and QuartzCore frameworks.

Launcher setup also requires **libcurl 7.85+** with HTTPS/TLS support, **json-c 0.15+**, and **OpenSSL Crypto 1.1.0+**. Linux additionally needs **GTK 3.22+**. These dependencies are excluded from headless workspaces. On macOS, `brew install json-c openssl@3` supplies the additional libraries; the Apple SDK supplies libcurl. On Debian/Ubuntu, install `libgtk-3-dev libcurl4-openssl-dev libjson-c-dev libssl-dev ca-certificates`. On Windows, install `curl:x64-windows json-c:x64-windows openssl:x64-windows` with vcpkg and pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` when configuring. Keep dependency libraries available at runtime; standalone dependency bundling is not yet implemented.

**Linux / macOS** — GCC or Clang on Linux; Apple Clang on macOS:

```sh
cmake --preset release
cmake --build --preset release
```

**Windows** — with the Visual Studio 2022 C toolchain installed:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

Both build `maage_launcher`, `maage_client`, `maage_server`, `maage_core`, `maage_window`, and `maage_render`. Add `--target maage_server` to build only the server and its dependencies. For Debug builds, use `debug` on Linux/macOS or build with `windows-msvc-debug` on Windows. For a headless workspace, configure with `-DMAAGE_BUILD_LAUNCHER=OFF -DMAAGE_BUILD_RENDER=OFF -DMAAGE_BUILD_WINDOW=OFF`.

Generated files go in `cmake/<preset>/`; compiled output goes in `build/<preset>/bin/` and `lib/`. macOS produces `MaaGE.app`, a plain `client` executable, and the command-line `server`. Visual Studio adds `Release/` or `Debug/` subdirectories. For Server-only builds, also disable Client with `-DMAAGE_BUILD_CLIENT=OFF`.

Install with `cmake --install cmake/release --prefix <destination>` (add `--config Release` for multi-config builds). The Linux Launcher desktop entry uses the installed executable path and does not require a terminal. Headless Server builds require no windowing or graphics SDK.

Launcher data lives in `%APPDATA%/MaaGE/` on Windows, `~/Library/Application Support/MaaGE/` on macOS, and `$XDG_DATA_HOME/maage/` or `~/.local/share/maage/` on Linux. Launcher logs use its `logs/` subdirectory; old `.maage` logs are retained in place. Client logs currently remain in `logs/` beside its executable. Each launch starts a fresh latest file; the previous log is archived as, for example, `launcher.2026-10-15_19-30-00.log` (local rotation time, with a numeric suffix only if the name already exists). Archives are retained. Logging configuration and command-line options can override the defaults.

## Initial Setup

Setup retrieves the official manifest, Minecraft 1.7.10 metadata, its referenced asset index, and every unique content-addressed object in that index. Minecraft 1.7.10 is the fixed resource baseline; MaaGE application versions remain independent. No Minecraft JAR, Java runtime, native library, authentication, or game installation is required or downloaded. The index does not include every resource historically embedded in the client JAR.

Minecraft files live beneath the application's `minecraft/` directory:

- `assets/indexes/1.7.10.json` preserves the complete logical-name-to-hash mapping.
- `assets/objects/<prefix>/<hash>` holds the verified, deduplicated object cache.
- `assets/reconstructed/<logical-name>` exposes every indexed resource under its original name and directory hierarchy.
- `metadata/1.7.10.json` stores the version metadata; the manifest and integrity receipt also live in `metadata/`.

Existing files from the previous `assets/` and `versions/1.7.10/metadata.json` layout are verified and migrated as needed, preserving valid downloads across interrupted migration. After successful setup, recognized legacy files and their temporary files are removed; unrecognized contents are retained, and only empty legacy directories are removed. MaaGE's `config/` and `logs/` remain outside `minecraft/`.

Downloads use verified HTTPS, four concurrent requests, timeouts, and limited retries. Reconstruction stages hard links where available and verified copies otherwise, then installs them atomically. Unsafe logical paths and symlink/reparse directories are rejected. Cancellation preserves verified resources, and Retry resumes the same operation. Continue remains disabled until every unique object and every logical file passes verification. The setup interface reports unique-object/download counts, transferred bytes, and reconstructed logical-file counts separately.

Subsequent launches check cached metadata and both resource forms using sizes and file fingerprints; unchanged installations need no network requests or full rehash. Changed files and incomplete installations receive hash checks. `--repair-assets` uses the same implementation to fully verify and repair the cache and reconstructed resources. Missing derived files are restored from valid cached objects without network downloads. Hard links share already verified content; independent copies receive their own integrity checks.

Resource consumers can link `MaaGE::MinecraftAssets` and call `maage_minecraft_asset_path` with a logical name and caller-owned output buffer. Set `MAAGE_BUILD_MINECRAFT_ASSETS=ON` to build this C90 resource-path library independently of the Launcher UI, downloader, and renderer. It resolves paths without certifying installation integrity; use resources after setup/repair succeeds. Runtime migration, downloading, reconstruction, and UI behavior await programmer testing.

## AI Collaborative Development

**MaaGE is developed collaboratively by humans and AI coding agents.** AI is intentionally used throughout development, with human engineering and oversight. AI-generated contributions are reviewed, refined, and maintained through the normal development workflow.

Contributors should disclose substantial AI involvement and remain responsible for their work. AI assistance does not excuse poor quality, insecure code, licensing violations, or unmaintainable implementations.

We stand by this approach. Anyone who objects to AI-assisted development is free not to use or contribute to MaaGE.

Human and AI-assisted contributions are welcome.

## License and Independence

MaaGE's original project code and documentation are licensed under the **Apache License 2.0 (Apache-2.0)**. The complete, unmodified license is available in [LICENSE](LICENSE).

This license does not cover third-party code or assets, or Minecraft, Mojang, or Microsoft intellectual property. Third-party materials remain subject to their respective licenses and permissions. Minecraft assets and branding are not relicensed by this project.

MaaGE is an independent project and is not affiliated with, endorsed by, or sponsored by Mojang or Microsoft.
