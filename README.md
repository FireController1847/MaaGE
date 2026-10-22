<!-- AI AGENTS: DO NOT MODIFY README.md unless explicitly instructed by the user. This file is intended for human readers only. Read AGENTS.md for all AI development instructions, architectural requirements, and repository conventions. -->

# MaaGE

**Minecraft as a Game Engine**

*A modular, cross-platform reimagining of Minecraft in C, built for performance, extensibility, and limitless modding.*

[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)
![Language: ISO C90](https://img.shields.io/badge/Language-ISO%20C90-555555.svg)
![Platforms: Windows, Linux, macOS](https://img.shields.io/badge/Platforms-Windows%20%7C%20Linux%20%7C%20macOS-526b80.svg)
[![Build Validation](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml/badge.svg)](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml)
![Status: Early Development](https://img.shields.io/badge/Status-Early%20Development-orange.svg)

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

MaaGE uses **CMake 3.21+** and requires a supported C compiler.

Dependencies vary by platform and selected build targets:

- **Core:** zlib.
- **Launcher:** libcurl, json-c, OpenSSL, and libzip; GTK 3 on Linux.
- **Client:** libpng and the corresponding native graphics and windowing development libraries.
- **macOS:** Xcode Command Line Tools and the Apple SDK.
- **Windows:** Visual Studio 2022 and the relevant dependencies, optionally managed through vcpkg.
- **Linux:** GCC or Clang, Ninja, and applicable X11, Wayland, and Vulkan development packages.

### macOS and Linux

```powershell
cmake --preset release
cmake --build --preset release
```

For a Debug build, replace `release` with `debug`.

### Windows

Using the Visual Studio 2022 toolchain:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
```

The Windows configuration requires the appropriate dependency libraries and may require a CMake toolchain file for vcpkg.

### Headless Builds

MaaGE supports building its portable libraries and Server without Launcher or graphical dependencies.

```powershell
cmake --preset release -DMAAGE_BUILD_LAUNCHER=OFF -DMAAGE_BUILD_CLIENT=OFF -DMAAGE_BUILD_WINDOW=OFF -DMAAGE_BUILD_RENDER=OFF -DMAAGE_BUILD_WORLD=ON
cmake --build --preset release
```

Build artifacts are generated beneath `build/`, with CMake configuration files beneath `cmake/`.

## Minecraft Assets and Ownership

MaaGE does **not** distribute Minecraft's proprietary game assets or original Java client.

The Launcher provides Microsoft account authentication and verifies access to Minecraft Java Edition before preparing the resources required by MaaGE.

Supported resources are obtained from Minecraft's official distribution infrastructure and stored locally.

A legitimate Minecraft Java Edition account is required for the supported asset preparation workflow. All third-party content remains subject to its original licensing and usage restrictions.

## Development and Contributions

MaaGE is developed collaboratively by **human contributors and AI coding agents**, with human direction, engineering oversight, and review.

AI-assisted development is an intentional part of this project. Contributions are held to the same standards of correctness, maintainability, security, attribution, and licensing regardless of how they were produced.

Both human-written and AI-assisted contributions are welcome. Contributors remain responsible for the quality and legality of their work.

## License and Independence

MaaGE's original source code and documentation are licensed under the **Apache License 2.0**. See [LICENSE](LICENSE) for the complete license terms.

This license does not extend to third-party software, Minecraft assets, trademarks, or other intellectual property owned by Mojang, Microsoft, or their respective rights holders.

**MaaGE is an independent project and is not affiliated with, endorsed by, approved by, or sponsored by Mojang Studios or Microsoft.**
