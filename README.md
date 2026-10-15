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

### Design goals

<table>
  <tr>
    <td width="50%"><strong>Modular by design</strong><br>Independent applications and a shared core, built around different modpacks and ways to play.</td>
    <td width="50%"><strong>Across desktop platforms</strong><br>Targeting Windows, Linux, and macOS with native platform integration.</td>
  </tr>
  <tr>
    <td width="50%"><strong>Native rendering</strong><br>Planned Vulkan on Windows and Linux; native Metal on macOS.</td>
    <td width="50%"><strong>C90 first</strong><br>Portable C foundations, with platform-specific code kept at the edges.</td>
  </tr>
</table>

## About MaaGE

MaaGE is an independent native voxel game platform inspired by Minecraft. It aims to provide a modular foundation for distinct modpacks, content, and gameplay experiences.

The platform is organized into three applications:

- **Launcher** — planned asset management, Microsoft account authentication, and Minecraft ownership verification. Mojang's proprietary assets will not be distributed with the project.
- **Client** — the future game experience, with native windowing, input, interaction, and rendering.
- **Dedicated server** — a standalone, headless application, intended to share core functionality and game definitions with the client.

ISO C90 portability is a major design goal, not a guarantee of support for every compiler or operating system.

## Development status

**Early development · build foundation only.** The current workspace builds three small executables that print their names and exit, plus a static core library containing project identity metadata. Gameplay, rendering, windowing, authentication, and asset management are not implemented.

Local validation covers macOS arm64 with Apple Clang; other platforms and toolchains remain unverified locally. The [Build Validation workflow](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml) provides manually triggered Release builds for Windows, Linux, and macOS.

## Build

Use **CMake 3.21+** and a C compiler. The Linux and macOS presets also require **Ninja**. From the repository root:

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

Both build the `nv_launcher`, `nv_client`, `nv_server`, and `nv_core` targets. For a single application, append `--target nv_server` (or another target) to the build command. Use the `debug` preset on Linux/macOS, or `windows-msvc-debug` when building on Windows, for Debug output.

Generated configuration stays in `cmake/<preset>/`; binaries and libraries go to `build/<preset>/bin/` and `lib/`. Visual Studio builds add `Release/` or `Debug/` beneath those output directories. No graphics SDK is required at this stage.

## Human and AI development

**MaaGE is developed collaboratively by humans and AI coding agents.** AI is an intentional part of the development process, alongside human engineering, direction, and oversight. AI-generated contributions are reviewed, refined, and maintained through the project's normal development workflow.

Contributors should disclose substantial AI involvement in their contributions. AI assistance does not excuse poor quality, insecure code, licensing violations, or implementations that are difficult to maintain. Contributors remain responsible for the work they submit.

We are transparent about this approach and stand by it. Anyone who objects to AI-assisted software development is free not to use or contribute to MaaGE.

Human and AI-assisted contributions, design discussions, and documentation improvements are welcome.

## License and independence

MaaGE's original project code and documentation are licensed under the **Apache License 2.0 (Apache-2.0)**. The complete, unmodified license is available in [LICENSE](LICENSE).

This license does not cover third-party code or assets, or Minecraft, Mojang, or Microsoft intellectual property. Third-party materials remain subject to their respective licenses and permissions. Minecraft assets and branding are not relicensed by this project.

MaaGE is an independent project and is not affiliated with, endorsed by, or sponsored by Mojang or Microsoft.
