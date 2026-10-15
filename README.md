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

- **Launcher** — planned asset management, Microsoft authentication, and Minecraft ownership verification. Mojang's proprietary assets will not be distributed.
- **Client** — planned native windowing, input, rendering, and player interaction.
- **Server** — independent headless operation, with an optional graphical administration frontend planned around the same server functionality.

The portable core targets ISO C90, with platform-specific integrations kept separate.

## Development Status

**Early development.** The macOS Launcher has a native UI preview with Play, News, Profiles, Accounts, Settings, and About pages using local example data. UI presentation currently supports Metal and Software; Windows/Linux UI adapters and Vulkan/OpenGL UI presentation remain unimplemented. Client is a graphical startup stub; Server has shared startup code with headless and optional GUI executables. Gameplay, administration UI, authentication, and asset management remain unimplemented.

The Launcher has been compiled locally with Apple Clang on macOS arm64 and cross-compiled with MinGW-w64 for Windows. Runtime and visual testing remain manual; Linux has not been compiled locally. [Build Validation](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml) runs manually on Windows, Linux, and macOS.

## Build

Use **CMake 3.21+** and a C compiler. The Linux and macOS presets also require **Ninja**. From the repository root:

Linux windowing requires X11 and Wayland development libraries, `wayland-protocols`, `wayland-scanner`, and pkg-config. Either backend can be disabled with `MAAGE_WINDOW_ENABLE_X11` or `MAAGE_WINDOW_ENABLE_WAYLAND`; both are enabled by default. Windows/Linux rendering requires Vulkan development headers and a loader discoverable by CMake. macOS uses Apple's AppKit, Metal, QuartzCore, CoreGraphics, and CoreText frameworks.

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

Generated files go in `cmake/<preset>/`; compiled output goes in `build/<preset>/bin/` and `lib/`. macOS produces `MaaGE Launcher.app` and `MaaGE Client.app`; Server remains a command-line executable. Visual Studio adds `Release/` or `Debug/` subdirectories. `MAAGE_BUILD_SERVER_GUI=ON` adds the optional Server GUI startup target, using the Server's version. For Server-only builds, also disable Client with `-DMAAGE_BUILD_CLIENT=OFF`.

Install with `cmake --install cmake/release --prefix <destination>` (add `--config Release` for multi-config builds). Linux desktop entries use the installed executable paths and do not require a terminal. Headless Server builds require no windowing or graphics SDK.

GUI diagnostic logs are stored in `~/Library/Logs/MaaGE/` on macOS, `%LOCALAPPDATA%\MaaGE\` on Windows, and `$XDG_STATE_HOME/maage/` or `~/.local/state/maage/` on Linux.

## AI Collaborative Development

**MaaGE is developed collaboratively by humans and AI coding agents.** AI is intentionally used throughout development, with human engineering and oversight. AI-generated contributions are reviewed, refined, and maintained through the normal development workflow.

Contributors should disclose substantial AI involvement and remain responsible for their work. AI assistance does not excuse poor quality, insecure code, licensing violations, or unmaintainable implementations.

We stand by this approach. Anyone who objects to AI-assisted development is free not to use or contribute to MaaGE.

Human and AI-assisted contributions are welcome.

## License and Independence

MaaGE's original project code and documentation are licensed under the **Apache License 2.0 (Apache-2.0)**. The complete, unmodified license is available in [LICENSE](LICENSE).

This license does not cover third-party code or assets, or Minecraft, Mojang, or Microsoft intellectual property. Third-party materials remain subject to their respective licenses and permissions. Minecraft assets and branding are not relicensed by this project.

MaaGE is an independent project and is not affiliated with, endorsed by, or sponsored by Mojang or Microsoft.
