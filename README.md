<!-- AI AGENTS: DO NOT MODIFY README.md unless explicitly instructed by the user. This file is intended for human readers only. Read AGENTS.md for all AI development instructions, architectural requirements, and repository conventions. -->

<h1 align="center">MaaGE</h1>
<p align="center"><strong>Minecraft as a Game Engine</strong></p>
<p align="center">
  <em>A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and modding.</em>
</p>
<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-Apache--2.0-blue?style=flat-square" alt="License: Apache-2.0"></a>
  <img src="https://img.shields.io/badge/Language-ISO%20C90-00599C?style=flat-square&amp;logo=c&amp;logoColor=white" alt="Language: ISO C90">
  <img src="https://img.shields.io/badge/Platforms-Windows%20%7C%20Linux%20%7C%20macOS-526b80?style=flat-square" alt="Platforms: Windows, Linux, macOS">
  <a href="https://github.com/FireController1847/MaaGE/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/FireController1847/MaaGE/build.yml?branch=main&amp;event=workflow_dispatch&amp;style=flat-square&amp;label=Latest%20Build" alt="Latest Build"></a>
  <img src="https://img.shields.io/badge/Status-Early%20Development-orange?style=flat-square" alt="Status: Early Development">
</p>

> **MaaGE is NOT an official Minecraft project and is NOT approved by or associated with Mojang.**

## About MaaGE

MaaGE is an open-source, native game engine and platform inspired by **Minecraft: Java Edition**, built from the ground up in C.

MaaGE reimagines Minecraft as a modular, high-performance foundation for custom gameplay experiences, modpacks, and extensible game systems. Its independent architecture emphasizes flexibility, performance, and the freedom to expand beyond the original game's technical limitations.

**Minecraft: Java Edition 1.7.10** serves as the initial reference for gameplay mechanics, content, and visual fidelity, while MaaGE's architecture supports continued innovation and expansion.

MaaGE is an independent implementation, not a Forge replacement or compatibility layer for existing Java mods.

## Architecture

MaaGE consists of three independently developed applications:

- **Launcher** — A native application for account verification, Minecraft asset preparation, installation management, and launching game instances.
- **Client** — A standalone game executable responsible for rendering, input, audio, and gameplay presentation.
- **Server** — An independent executable designed for authoritative game simulation, integrated single-player sessions, and dedicated headless hosting.

The platform emphasizes portable **ISO C90** code, reusable libraries, explicit subsystem boundaries, and replaceable platform backends.

Target platforms include **Windows, Linux, and macOS**, with native Metal rendering on macOS and Vulkan, OpenGL, and software rendering backends at varying stages of development.

## Development Status

**MaaGE is in early development and is not yet ready for general gameplay.**

The project contains foundations for native launching, asset management, voxel world generation, rendering, player movement, and persistent world storage.

Many systems are still incomplete or undergoing architectural development. Platform support and runtime validation also vary.

See the [Development Roadmap](docs/development-roadmap.md) for implemented milestones, known limitations, and planned functionality.

## Building From Source

MaaGE uses **CMake 3.21+** and a supported C compiler. Build requirements vary depending on the operating system and selected components.

### Platform Prerequisites

**macOS**

- Xcode Command Line Tools, including Apple Clang.
- Ninja for the default CMake presets.
- Apple SDK frameworks for AppKit, Metal, and QuartzCore.
- Homebrew or another package manager for third-party libraries.

**Windows**

- Visual Studio 2022 with the MSVC C toolchain.
- CMake 3.21 or later.
- Vulkan SDK headers and loader for Vulkan-enabled builds.
- vcpkg or equivalent dependency management for third-party libraries.

**Linux**

- GCC or Clang and Ninja.
- pkg-config.
- X11 and Wayland development libraries.
- `wayland-protocols` and `wayland-scanner`.
- Vulkan development headers and loader for Vulkan-enabled builds.

### Module Dependencies

Different MaaGE components require different libraries. Only dependencies associated with enabled build targets are required.

| Module | Dependencies |
|---|---|
| **Core** | zlib 1.2.3+ |
| **Launcher** | libcurl 7.85+, json-c 0.15+, OpenSSL Crypto 1.1.0+, libzip 1.7+ |
| **Client** | libpng 1.6+, platform-specific rendering and windowing dependencies |
| **Server** | Core dependencies; no graphics or windowing dependencies for headless builds |

The Launcher additionally requires **GTK 3.22+ on Linux**.

Third-party libraries are installed separately and remain subject to their respective licensing terms. See [NOTICE.md](NOTICE.md) for project acknowledgments and license references.

### Building on macOS and Linux

From the repository root:

```powershell
cmake --preset release
cmake --build --preset release
```

For a Debug build:

```powershell
cmake --preset debug
cmake --build --preset debug
```

### Building on Windows

Using the Visual Studio 2022 toolchain:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

For a Debug build:

```powershell
cmake --build --preset windows-msvc-debug
```

When using vcpkg, configure CMake with the appropriate `CMAKE_TOOLCHAIN_FILE` path.

### Headless Server Builds

MaaGE supports building its Server and portable libraries independently of the Launcher and graphical systems.

```powershell
cmake --preset release -DMAAGE_BUILD_LAUNCHER=OFF -DMAAGE_BUILD_CLIENT=OFF -DMAAGE_BUILD_WINDOW=OFF -DMAAGE_BUILD_RENDER=OFF -DMAAGE_BUILD_WORLD=ON
cmake --build --preset release
```

Headless builds do not require a graphical environment or graphics SDK.

CMake configuration files are generated beneath `cmake/`, while compiled artifacts are written beneath `build/`.

## Minecraft Assets and Ownership

MaaGE does **not** distribute Minecraft's proprietary game assets or original Java client.

The Launcher provides Microsoft account authentication and verifies access to Minecraft Java Edition before preparing the resources required by MaaGE.

Supported resources are obtained from Minecraft's official distribution infrastructure and stored locally.

A legitimate Minecraft Java Edition account is required for the supported asset preparation workflow. All third-party content remains subject to its original licensing and usage restrictions.

## AI Assisted Development

MaaGE is developed collaboratively by **human contributors and AI coding agents**, with human direction, engineering oversight, and review.

AI-assisted development is an intentional part of this project. Contributions are held to the same standards of correctness, maintainability, security, attribution, and licensing regardless of how they were produced.

Both human-written and AI-assisted contributions are welcome. Contributors remain responsible for the quality and legality of their work.

## License and Independence

MaaGE's original source code and documentation are licensed under the **Apache License 2.0**. See [LICENSE](LICENSE) for the complete license terms.

Third-party technologies and their developers are acknowledged in [NOTICE.md](NOTICE.md).

This license does not extend to third-party software, Minecraft assets, trademarks, or other intellectual property owned by Mojang, Microsoft, or their respective rights holders.

MaaGE is an independent project and is not affiliated with, endorsed by, approved by, or sponsored by Mojang Studios or Microsoft.
