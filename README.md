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

- **Launcher** — native existing-account access verification followed by first-run asset setup; new Microsoft sign-in remains deferred. Mojang's proprietary assets will not be distributed.
- **Client** — independently versioned game executable for Launcher-managed installations and startup. Native windowing, rendering, and gameplay are planned.
- **Server** — independent headless operation, with an optional graphical administration interface planned within the same executable.

The portable core targets ISO C90, with platform-specific integrations kept separate.

## Development Status

**Early development.** The Launcher has native Verify Minecraft Access and Preparing MaaGE interfaces (AppKit on macOS, Win32 on Windows, GTK 3 on Linux), an authenticated existing-session verification service, and a resumable asset manager that verifies and reconstructs Minecraft Java Edition 1.7.10's full asset index and client-JAR game resources. After setup, it retains the Core RGB rendering demonstration: native Metal on macOS and Vulkan on Windows/Linux. Client and Server remain startup stubs; gameplay, new Microsoft sign-in, game launching, and Server administration UI are not implemented. macOS builds have been compiled locally; Windows/Linux compilation and runtime/UI testing remain outstanding.

Local builds use macOS arm64 with Apple Clang. [Build Validation](https://github.com/FireController1847/MaaGE/actions/workflows/build.yml) runs manually on Windows, Linux, and macOS.

## Build

Release builds optimize for size and strip nonessential symbols; use Debug for source-level debugging. Set `MAAGE_OPTIMIZE_SIZE=OFF` to disable the size-specific build settings.

Use **CMake 3.21+** and a C compiler. The Linux and macOS presets also require **Ninja**. From the repository root:

Linux windowing requires X11 and Wayland development libraries, `wayland-protocols`, `wayland-scanner`, and pkg-config. Either backend can be disabled with `MAAGE_WINDOW_ENABLE_X11` or `MAAGE_WINDOW_ENABLE_WAYLAND`; both are enabled by default. Windows/Linux rendering requires Vulkan development headers and a loader discoverable by CMake. macOS uses Xcode's AppKit, Metal, and QuartzCore frameworks.

Launcher setup also requires **libcurl 7.85+** with HTTPS/TLS support, **json-c 0.15+**, **OpenSSL Crypto 1.1.0+**, and **libzip 1.7+**. Linux additionally needs **GTK 3.22+**. These dependencies are excluded from headless workspaces. On macOS, `brew install json-c openssl@3 libzip` supplies the additional libraries; the Apple SDK supplies libcurl. On Debian/Ubuntu, install `libgtk-3-dev libcurl4-openssl-dev libjson-c-dev libssl-dev libzip-dev ca-certificates`. On Windows, install `curl:x64-windows json-c:x64-windows openssl:x64-windows libzip[core]:x64-windows` with vcpkg and pass `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` when configuring. Keep dependency libraries available at runtime; standalone dependency bundling is not yet implemented.

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

The workflow is **Verify Minecraft Access → Preparing MaaGE → download and reconstruct resources → Ready to Play**, followed by the existing RGB rendering demonstration. Game launching is still deferred. Native controls support selecting multiple real accounts, rescan, manual configuration-folder selection, explicit Import Account consent and Verify Saved, retry, disconnect, and cancel. The disabled **Verify Through Minecraft — Coming Soon** option has no multiplayer implementation. No renderer initializes for these setup screens.

| Launcher / Format | Discovery | Authenticated Verification |
| --- | --- | --- |
| PrismLauncher / MultiMC `accounts.json` V3 | Public `profile.id` / `profile.name`; UUID deduplication preserves launcher sources | MSA accounts with an available, usable Minecraft Services `ygg.token` |
| Official Launcher `launcher_accounts.json` | Public `accounts.*.minecraftProfile` identities | Discovery only |
| CurseForge with an embedded official launcher | Official account records under the Minecraft `Install` directory | Discovery only |
| FTB App / standalone CurseForge private stores | Known folders detected; official-format records read where present | Private, protected, and opaque account formats unsupported |

Known Windows, macOS, Linux, and Prism/MultiMC Flatpak paths are scanned; portable/custom installations can be selected manually. Folder detection alone does not create a player account or prove access. Installation profiles, offline identities, and cached ownership flags cannot pass verification. Unsupported formats and missing sessions remain unverified. The supported token layout follows the upstream [PrismLauncher account format](https://github.com/PrismLauncher/PrismLauncher/blob/develop/launcher/minecraft/auth/AccountData.cpp) and [MultiMC account format](https://github.com/MultiMC/Launcher/blob/develop/launcher/minecraft/auth/AccountData.cpp); launcher changes may require an adapter update.

After explicit Import Account consent, a worker selectively reads only the selected compatible account's Minecraft Services token and creates a MaaGE-owned local account. It checks `GET https://api.minecraftservices.com/minecraft/profile`, validates the username and matching UUID, then checks `GET https://api.minecraftservices.com/entitlements/mcstore` for a valid Java `game_minecraft` entitlement record. HTTPS requests have bounded responses, timeouts, certificate verification, and no redirects. HTTP 200 alone does not grant access. Rejected/application-bound or expired sessions require refreshing the original launcher and re-importing; MaaGE cannot renew Minecraft sessions yet. This workflow does not use a MaaGE Microsoft Client ID or perform Microsoft OAuth.

MaaGE stores imported sessions locally without Keychain, Credential Manager, DPAPI, or another system credential store. `<MaaGE Application Data>/config/accounts/<uuid>.account` contains an AES-256-GCM-encrypted versioned account record; `config/accounts/key.bin` is its randomly generated local key. Both files receive owner-only access (0600 on POSIX; a protected current-user DACL on Windows). Writes use a private temporary file and atomic replacement under a store lock. The UUID is authenticated with the encrypted payload, preventing account-file swapping. This protects against plaintext disclosure and other users; a process running as the same user can read both files. Backups must protect the key and account files together.

The account payload records a stable Minecraft UUID/name, `minecraft-session-import` credential provider, Minecraft access token and expiry, and a verified access lease. The public `config/minecraft-profiles.json` registry remains nonsecret and identifies local profiles and launcher sources. The provider/version boundary allows a future Microsoft OAuth flow to replace credentials for the same identity; Microsoft passwords, OAuth access/refresh tokens, source authentication databases, and protected credential stores are not imported. No Microsoft sign-in or token-refresh flow is implemented. In-memory token/JSON copies are wiped after use; credentials and authentication responses never enter logs.

Startup restores authenticated verification leases for up to one hour, bounded by the imported session's expiry. Plain public-registry flags cannot unlock access. When a lease needs renewal and its token remains current, MaaGE verifies its own saved token in the background, independently of the original launcher. At least one usable local account automatically advances past initial verification. **Verify Saved** checks only MaaGE's stored session; **Re-import** explicitly obtains a replacement from the selected source after consent; **Disconnect** deletes the local account credential. Start with `--manage-accounts` to open these controls even when an account is already ready. Sessions with unknown expiry are conservatively limited to one hour. Once the imported Minecraft token expires, re-import is required until Microsoft authentication/refresh is implemented; Microsoft refresh tokens are [bound to the issuing client](https://learn.microsoft.com/en-us/entra/identity-platform/refresh-tokens), so they are not treated as transferable Minecraft credentials.


At least one currently verified profile is required by both the controller and asset-manager service, including repair and retry. Cancellation, disconnect, and lease expiry revoke the active grant; active work stops through its cancellation checks, preserving completed resources. If access expires during setup, **Verify Access** restores/checks local accounts before requesting user action. Authentication, discovery, and native UI behavior have not been runtime tested.

Setup retrieves the official manifest, Minecraft 1.7.10 metadata, its referenced asset index, every unique content-addressed object in that index, and `downloads.client` from the same official 1.7.10 metadata. The client JAR is downloaded with the metadata-provided URL, SHA-1, and size, using the existing HTTPS/retry/cancellation/atomic-download infrastructure. Minecraft 1.7.10 is the fixed resource baseline; MaaGE application versions remain independent. A verified Minecraft Java account is required first; the official client JAR is downloaded as a resource archive. No Java runtime, server JAR, native game library, or existing Minecraft installation is required. The client archive supplies additional resources absent from the index, including its actual texture and shader files.

Minecraft files live beneath the application's `minecraft/` directory:

- `assets/indexes/1.7.10.json` preserves the complete logical-name-to-hash mapping.
- `assets/objects/<prefix>/<hash>` holds the verified, deduplicated object cache.
- `assets/reconst/<logical-name>` exposes every indexed resource under its original name and directory hierarchy.
- `cache/client-1.7.10.jar` — verified client resource source archive.
- `metadata/1.7.10.json` stores the version metadata; the manifest and integrity receipt also live in `metadata/`.

The previous `assets/reconstructed/` folder is renamed to `assets/reconst/` when the new folder does not exist. If both exist, the old folder is preserved and setup prepares `reconst/` from verified caches.

The source JAR is retained at `minecraft/cache/client-1.7.10.jar`. A private archive adapter uses [libzip](https://libzip.org/documentation/) to inventory entries and stream game resources. Selected files are `assets/<namespace>/...`, `pack.png`, and `pack.mcmeta`, when actually present; no model/blockstate directories are fabricated. Java `.class`/`.java` files, embedded archives, native executables/libraries, scripts, signatures, and unrelated root package files are excluded. The JAR is never executed.

JAR resources omit only the leading `assets/`: `assets/minecraft/textures/...` becomes `minecraft/assets/reconst/minecraft/textures/...`, merging directly with indexed resources while retaining the `minecraft` namespace. Other namespaces and root pack resources are preserved. Existing verified files in either the old nested or flattened layout are reused; after successful reconstruction, verified obsolete originals and empty directories are removed while indexed and unknown files are preserved. Every archive path/type is checked, including excluded entries. Traversal, absolute paths, links/reparse entries, unsafe names, selected case aliases, and file/directory conflicts fail closed. Expansion and entry counts are bounded. Source streams pass CRC/size checks and produce SHA-1 hashes; staged output and installed files are checked against those hashes. Identical JAR resource contents can reuse hard links. Conflicting index/JAR paths are rejected before reconstruction; identical spelling/content can share one file.

The existing `assets-state.json` receipt records indexed mappings in `reconstructed`, JAR logical mappings with original archive paths and SHA-1/size/fingerprint in `client_resources`, and each source's hash. Completion requires the verified index/object cache, verified client archive, and valid reconstructed files from both sources. Missing/corrupt files rebuild from those local sources without resource downloads; only an invalid/missing JAR is reacquired when indexed objects remain valid. The existing account-verification prerequisite still applies. Every setup checks the JAR's hash and hashes its selected logical resources; full repair also hashes all indexed objects/copies.

The Preparing MaaGE checklist now includes client resources. It reports JAR bytes transferred and known archive size (retries can add traffic), actual applicable archive-entry counts, reconstruction progress, and final verification. Work stays on the setup worker; cancellation/retry and disabled Continue remain in force until all required resources are ready. Progress stays indeterminate until archive work totals are known.

Existing files from the previous `assets/` and `versions/1.7.10/metadata.json` layout are verified and migrated as needed, preserving valid downloads across interrupted migration. After successful setup, recognized legacy files and their temporary files are removed; unrecognized contents are retained, and only empty legacy directories are removed. MaaGE's `config/` and `logs/` remain outside `minecraft/`.

Downloads use verified HTTPS, four concurrent requests, timeouts, and limited retries. Reconstruction stages hard links where available and verified copies otherwise, then installs them atomically. Unsafe logical paths and symlink/reparse directories are rejected. Cancellation preserves verified resources, and Retry resumes the same operation. Continue remains disabled until both indexed and client-JAR resources pass verification. The setup interface reports unique-object/download counts, transferred bytes, and reconstructed logical-file counts separately. INFO logs record individual downloads (logical name, URL, destination, attempts/retries), verified installation, migration, and reconstruction source/destination paths with hard-link/copy results. Use `--log-level=debug` to also log indexed mappings, per-file verification, and reuse of unchanged cached resources. These diagnostics use the downloaded index rather than an embedded asset list.

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
