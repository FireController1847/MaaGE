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

- **Launcher** — native Microsoft sign-in and Minecraft Java access verification followed by first-run asset setup. Mojang's proprietary assets will not be distributed.
- **Client** — independently versioned game executable for Launcher-managed installations and startup. Native windowing, rendering, and gameplay are planned.
- **Server** — independent headless operation, with an optional graphical administration interface planned within the same executable.

The portable core targets ISO C90, with platform-specific integrations kept separate.

## Development Status

**Early development.** The Launcher has native Verify Minecraft Access and Preparing MaaGE interfaces (AppKit on macOS, Win32 on Windows, GTK 3 on Linux), a Microsoft OAuth/PKCE account service, and a resumable asset manager that verifies and reconstructs Minecraft Java Edition 1.7.10's full asset index and client-JAR game resources. After setup, a native sidebar opens Play, News, Installations, and Settings. Play remains disabled because Client startup is not implemented; News has an honest empty state. Client and Server remain startup stubs; gameplay, game launching, and Server administration UI are not implemented. macOS builds have been compiled locally; full Windows/Linux builds and runtime/UI testing remain outstanding.

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

The workflow is **Verify Minecraft Access → Microsoft sign-in → Java access verification → Preparing MaaGE → download and reconstruct resources → main Launcher**. The existing native dark setup offers system-browser sign-in, device-code fallback (verification URL, code, copy/open buttons, expiry countdown), progress, cancellation and retry. Continue requires a current verified Java account. No renderer initializes for setup or ordinary Launcher UI. `--renderer-demo` still opens the RGB demonstration after setup.

The main Launcher defaults to Play, with a compact account selector and synchronized MaaGE configuration selector. Installations supports naming, duplication, removal, and choosing a default; these are configurations, not Minecraft instances or versions. Preferences and selections live in `config/launcher.json`, separately from account credentials. Settings shows the resource location and readiness, opens the established account and repair workflows, and includes About information. Appearance follows the system by default, with Light/Dark overrides in AppKit and GTK; Win32 uses native system colors. News links to the project while no feed is configured. Play explains the unavailable Client startup rather than simulating a launch.

MaaGE's approved public Microsoft Entra application is `5e2b53b6-d00b-4de9-8f4d-a1897aaeca43`. It uses the consumer authority and requests `XboxLive.signin offline_access`. Browser sign-in follows [Microsoft's authorization code flow](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow) with S256 PKCE, cryptographically random state, exact `maage://auth` redirect matching, a five-minute deadline, and one-time callback consumption. There is no client secret, embedded browser, password collection, application-ID substitution, or third-party token import. [Device authorization](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-device-code) is available explicitly and is used when MaaGE cannot open the browser or create a callback receiver; polling respects pending/slow-down responses and backs off on transient failures.

The Microsoft access token is exchanged with Xbox Live (identity-platform `d=` RPS ticket), then XSTS (`RETAIL`, Minecraft Services relying party), then Minecraft Services. Separate transient credentials are wiped after each exchange. Errors identify the failing endpoint and HTTP status with known, sanitized service errors; a 403 is not automatically diagnosed as an allowlist problem. Successful Microsoft or Xbox login alone never grants resource access.

Verification requires an authenticated Minecraft Java profile with a valid username/UUID and an explicit `game_minecraft` entitlement. Both `entitlements/mcstore` (signed items) and `entitlements/license?requestId=...` (signed aggregate with item name/source, including non-store access) are consulted. The license schema is described by the maintained [PrismarineJS interface](https://github.com/PrismarineJS/prismarine-auth/blob/master/index.d.ts); the endpoint is also used by [Modrinth](https://github.com/modrinth/code/blob/main/packages/app-lib/src/state/minecraft_auth.rs). Empty lists, unrelated/Bedrock entitlements, failed profiles, and public cached flags cannot pass. Current Java access is verified; opaque license sources do not establish a permanent-purchase guarantee or conclusively classify subscription versus purchase. API schemas and app-specific acceptance still require programmer runtime validation.

MaaGE keeps one UUID-keyed registry at `config/minecraft-profiles.json`. Add / Manage Microsoft Accounts reuses the same native account controls from the Play selector and Settings: add, select, refresh/reverify, sign out, and remove. Sign-out deletes MaaGE credentials while retaining profile metadata; removal also removes the profile. Previously imported profiles retain their identity but require Microsoft sign-in; no external launcher credential files are read. Minecraft resources remain installed when access expires or an account is removed.

By explicit project policy, **MaaGE does not use native credential storage**. On macOS, Windows and Linux, `config/accounts/<uuid>.account` contains AES-256-GCM-encrypted Microsoft refresh/Minecraft session credentials, stable identity, provider version and bounded verification lease. `config/accounts/key.bin` is the OS-random local encryption key. Files have owner-only permissions (0600 on POSIX; protected current-user DACL on Windows), UUID-authenticated ciphertext, atomic replacement and a store lock. A process running as the same user can access both key and ciphertext; protect them together in backups. No refresh token is stored in ordinary plaintext configuration. Transient credentials, HTTP payloads, and raw responses do not enter logs or dialogs.

Startup restores authenticated leases for up to one hour, capped by Minecraft token expiry. Expiring sessions refresh through MaaGE's own client application; rotated refresh credentials are saved before Xbox/Minecraft exchanges. Unexpired leases avoid interactive login, and refreshed credentials receive a new Java access check. Revoked authorization requires sign-in again. Use `--manage-accounts` to retain the account screen even when startup can continue automatically. The controller and asset-manager boundary enforce a current verified lease for acquisition, repair, retry and active work; cancellation/sign-out/expiry stop unauthorized work while preserving resources.

Browser callback installation:

- **macOS:** the Launcher bundle advertises `maage` in `CFBundleURLTypes`; a lifecycle Apple-event handler routes activations into the pending worker independently of the currently displayed window. Install/open the app bundle so LaunchServices discovers it. Code signing and distribution provisioning remain deployment responsibilities.
- **Windows:** a normal Launcher start registers this executable as the per-user `HKCU\Software\Classes\maage` handler with a quoted absolute path. Protocol activation forwards its URI to the pending Launcher and exits; no administrator access is needed.
- **Linux:** installation generates `org.maage.launcher.desktop` with `%u` and `MimeType=x-scheme-handler/maage;`. Install it into the desktop environment's applications path, refresh its desktop database where required, and select it with `xdg-mime default org.maage.launcher.desktop x-scheme-handler/maage`. Browser launch uses the desktop default URI handler through GIO. Device-code sign-in remains available when scheme activation is unavailable.

OS activations forward locally to the live operation; the receiver binds only IPv4 loopback, uses the random OAuth state for routing/validation, and closes before exchanging the code. This local forwarding socket does **not** change the registered OAuth redirect. Duplicate, stale, unsolicited and state-mismatched callbacks cannot authorize an account. If the original pending process has exited, its PKCE verifier is gone and the user must start a new sign-in.

**Custom-URI acceptance has not been confirmed against this application.** No live authentication requests are made during development. Entra must register `maage://auth` as a public-client Mobile and desktop redirect, not a Web/SPA redirect; allowlisting Minecraft Services does not validate that redirect. If Microsoft rejects the custom URI, use device-code sign-in. The standards-supported alternative is a system-browser loopback redirect: additionally register `http://localhost` under Mobile and desktop applications as described in [Microsoft's redirect guidance](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url), then explicitly implement/select loopback mode with an ephemeral port and PKCE. This build offers device code and documents that loopback alternative; it never silently substitutes an unregistered redirect.

Only compilation is performed during development. Browser activation, authentication, refresh/revocation, native UI behavior and non-macOS builds remain for programmer validation.

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
