<!-- AI AGENTS: README.md is intended for human readers only. Read AGENTS.md for all AI development instructions, architectural requirements, and repository conventions. -->

# MaaGE

An independent, extensible, Minecraft-inspired native voxel game platform, designed to support different modpacks and gameplay experiences.

MaaGE aims to provide a modular foundation for building and playing voxel worlds, with room for distinct rules, content, and ways to play.

## Current status

**Early planning and repository initialization.** The repository currently contains licensing, this overview, and ignore rules. No engine, launcher, client, dedicated server, or build system has been implemented. The architecture and capabilities below describe future plans.

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

See [AGENTS.md](AGENTS.md) for AI development instructions, architectural requirements, and repository conventions. That file is planned but has not yet been added.

## License and independence

MaaGE's original project code and documentation are licensed under the **Apache License 2.0 (Apache-2.0)**. The complete, unmodified license is available in [LICENSE](LICENSE).

This license does not cover third-party code or assets, or Minecraft, Mojang, or Microsoft intellectual property. Third-party materials remain subject to their respective licenses and permissions. Minecraft assets and branding are not relicensed by this project.

MaaGE is an independent project and is not affiliated with, endorsed by, or sponsored by Mojang or Microsoft.
