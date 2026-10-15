<!-- AI AGENTS: README.md is intended for human readers only. Read AGENTS.md for all AI development instructions, architectural requirements, and repository conventions. -->

# MaaGE

An independent, extensible, Minecraft-inspired native voxel game platform, designed to support different modpacks and gameplay experiences.

MaaGE aims to provide a modular foundation for building and playing voxel worlds, with room for distinct rules, content, and ways to play.

## Current status

**Early planning and workspace initialization.** The repository provides a C90-first CMake workspace with launcher, client, and server executables that identify themselves and exit, plus a minimal shared core containing project identity metadata. Engine functionality and the capabilities below remain future plans.

## Planned architecture

The platform will be written primarily in C, with **ISO C90 portability as a major design goal**. Platform-specific integrations will sit behind a modular core; C90 is a portability target, not a guarantee of support for every compiler or operating system.

- **Separate applications:** a launcher, a game client, and a dedicated server.
- **Modular core:** shared foundations intended to support different modpacks and gameplay experiences.
- **Native windowing:** integration with each desktop platform's native windowing APIs.
- **Graphics:** Vulkan on Windows and Linux, with native Metal on macOS.

The planned launcher will manage assets and provide Microsoft account authentication with Minecraft ownership verification. Mojang's proprietary assets will not be distributed with the project.

## Human and AI development

**MaaGE is developed collaboratively by humans and AI coding agents.** AI is an intentional part of the development process, alongside human engineering, direction, and oversight. AI-generated contributions are reviewed, refined, and maintained through the project's normal development workflow.

Contributors should disclose substantial AI involvement in their contributions. AI assistance does not excuse poor quality, insecure code, licensing violations, or implementations that are difficult to maintain. Contributors remain responsible for the work they submit.

We are transparent about this approach and stand by it. Anyone who objects to AI-assisted software development is free not to use or contribute to MaaGE.

## Contributing

Human and AI-assisted contributions are welcome, including design discussions, documentation improvements, and implementation proposals. At this stage, contributions should reflect the project's planning status and distinguish proposals from working functionality.

See [AGENTS.md](AGENTS.md) for AI development instructions, architectural requirements, and repository conventions.

## Build the workspace

Use CMake 3.21 or newer, Ninja, and a C compiler. The minimum supports version 3 presets, including host conditions and configure/build presets, without requiring CMake 4.x. From the repository root:

```sh
cmake --preset debug
cmake --build --preset debug
./build/debug/bin/launcher
```

Use `release` for an optimized build. The `debug` and `release` presets use the environment's default C compiler; select GCC or Clang with `CC` on the first configure, or use a Windows developer shell for MSVC/Clang. The Windows-only `windows-msvc` preset requires Visual Studio 2022's C toolchain; build with `windows-msvc-debug` or `windows-msvc-release`. The `multi` Ninja Multi-Config preset similarly provides `multi-debug` and `multi-release` builds.

Targets are `nv_launcher`, `nv_client`, `nv_server`, and static library `nv_core`. Disable applications independently with `-DNV_BUILD_LAUNCHER=OFF`, `-DNV_BUILD_CLIENT=OFF`, or `-DNV_BUILD_SERVER=OFF` when configuring. `-DNV_BUILD_TESTS=ON` registers smoke tests for enabled applications; run `ctest --test-dir cmake/debug --output-on-failure` after building.

Generated headers and intermediates live in `cmake/<preset>/`; compiled output lives in `build/<preset>/bin/` and `lib/`. Multi-config builds append a configuration directory such as `bin/Debug/`. Manual builds must also configure beneath `cmake/<name>/`; cross-compilers should supply a toolchain file with target information. Target vendor defaults to `apple` for macOS and `unknown` otherwise, and can be supplied through `NV_TARGET_VENDOR`.

Validated on macOS arm64 with Apple Clang. Windows, Linux, other architectures, and Open Watcom remain untested. Open Watcom experimentation requires a suitable local toolchain/generator configuration; no working preset is claimed. No windowing or rendering SDK is required or linked.

## License and independence

MaaGE's original project code and documentation are licensed under the **Apache License 2.0 (Apache-2.0)**. The complete, unmodified license is available in [LICENSE](LICENSE).

This license does not cover third-party code or assets, or Minecraft, Mojang, or Microsoft intellectual property. Third-party materials remain subject to their respective licenses and permissions. Minecraft assets and branding are not relicensed by this project.

MaaGE is an independent project and is not affiliated with, endorsed by, or sponsored by Mojang or Microsoft.
