# Launcher Authentication

MaaGE uses the system browser with authorization-code OAuth and PKCE (S256), then exchanges Microsoft credentials through Xbox Live, XSTS, and Minecraft Services. Minecraft Java ownership and the player profile are checked separately from Microsoft sign-in. A Microsoft account without verified Java ownership is never launch-ready.

## Microsoft Application Registration

Before real sign-in, provide **MaaGE's own application registration**. No client ID is supplied, and no other launcher's registration may be substituted.

1. Register a native/public client that supports personal Microsoft accounts. Follow Microsoft's [authorization-code and PKCE documentation](https://learn.microsoft.com/en-us/entra/identity-platform/v2-oauth2-auth-code-flow). Do not create or supply a client secret.
2. Register the exact redirect `http://127.0.0.1:53682/callback` as a public-client redirect. Microsoft's [redirect URI guidance](https://learn.microsoft.com/en-us/entra/identity-platform/reply-url) requires the application manifest for HTTP `127.0.0.1` redirects. A registered `http://localhost:53682/callback` can alternatively be selected; MaaGE's listener binds IPv4 loopback only. Ports 1024–65535 and the `/callback` path are supported. The chosen port must be free.
3. Ensure the registration is authorized for `XboxLive.SignIn` and `XboxLive.offline_access`, and obtain any required Xbox/Minecraft Services permission for this application. See Microsoft's [Xbox authentication documentation](https://learn.microsoft.com/en-us/gaming/gdk/docs/services/fundamentals/s2s-auth-calls/service-authentication/live-website-authentication) and [Minecraft application registration information](https://aka.ms/AppRegInfo). Creating an Entra registration alone does not establish Minecraft Services approval.
4. Configure the application ID at build time:

   ```sh
   cmake --preset release -DMAAGE_MICROSOFT_CLIENT_ID=<your-application-id>
   cmake --build --preset release --target maage_launcher
   ```

   Alternatively, set `MAAGE_MICROSOFT_CLIENT_ID` in the Launcher's process environment. `MAAGE_MICROSOFT_REDIRECT_URI` selects a different registered loopback redirect. Finder-launched applications do not ordinarily inherit shell environment settings; a build-time ID is convenient for native application bundles. The application ID is public configuration, not a credential.

Missing registration produces a configuration message and disables account operations. Authorization codes expire, the listener times out after five minutes, and Cancel closes the current attempt. Signing in again to a saved account must resolve to that same Xbox identity; use Add Microsoft Account for a different identity.

## Build Dependencies

Launcher authentication requires **libcurl 7.85+** (with HTTPS support), **json-c 0.15+**, and native threads. Protocol/session code and public interfaces are ISO C90; dependency and OS adapters privately use C99.

- **macOS:** `brew install json-c`; libcurl and Security/CoreServices/ImageIO come from the Apple SDK. Credentials use Keychain.
- **Linux:** install `libcurl4-openssl-dev libjson-c-dev libsecret-1-dev xdg-utils` alongside the existing graphics/windowing dependencies. A running, unlocked Secret Service is needed for persistent credentials; `xdg-open` opens the browser.
- **Windows:** install `curl[core,schannel]:x64-windows` and `json-c:x64-windows` using vcpkg, then add `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows` when configuring the existing Windows preset. Windows supplies BCrypt, Credential Manager, Winsock, and ShellExecute. Distribute the dependency DLLs alongside the executable when using a dynamic triplet.

The manual CI workflow provisions these libraries; Windows pins its vcpkg tool/ports revision. Client, Core, and headless Server do not acquire authentication dependencies when Launcher is disabled.

## Accounts and Credentials

The header menu supports adding, selecting, refreshing, signing out, and removing up to 32 accounts. Identity uses the authenticated Xbox user hash, independently of player names. Saved accounts are displayed from metadata, but the selected account is refreshed and revalidated before becoming ready. Other accounts refresh when selected. Selected sessions refresh shortly before expiry; temporary network failure preserves an existing verified session only until its original expiration.

Only refresh tokens are persisted, using Keychain on macOS, Credential Manager on Windows, or Secret Service on Linux. Access tokens remain in memory. Rotated refresh tokens replace the saved credential before downstream validation. If secure storage fails while saving, the menu requires **Continue for This Session** or **Cancel**; no plaintext fallback exists. Session-only accounts need interactive sign-in after restart.

Non-sensitive identity metadata is written atomically to:

- macOS: `~/Library/Application Support/MaaGE/accounts.json`
- Windows: `%LOCALAPPDATA%\MaaGE\accounts.json`
- Linux: `$XDG_DATA_HOME/maage/accounts.json`, falling back to `~/.local/share/maage/accounts.json`

Sign Out removes local credentials but retains display metadata. Remove Account also deletes that metadata. Both clear credentials from memory and report failure if the stored credential cannot be deleted; the retained account is marked not to restore credentials. Unlock the native store and retry Remove Account to delete a remaining vault entry. Neither action signs out of Microsoft in the browser or revokes consent remotely. Registrations have separate credential keys; changing client ID requires signing in again.

Widgets receive identity, ownership, readiness, and progress only. Future launch code can request a copied, unexpired verified session with `maage_auth_session_copy`, and must erase it with `maage_auth_session_clear`. No game launch is implemented. Profile name and UUID are displayed. macOS can fetch the active skin from the fixed HTTPS Minecraft texture host and display its face using ImageIO; missing/invalid images are optional and do not invalidate authentication. Other platforms retain the default avatar until their native UI/image adapters exist. Native store authorization dialogs may require user interaction; cancellation cannot dismiss every OS-owned dialog.

## Protocol Sources and Limitations

The implementation was written independently after reviewing [ProtoLauncher](https://github.com/FireController1847/ProtoLauncher) first, [PrismLauncher](https://github.com/PrismLauncher/PrismLauncher) second, and [MultiMC](https://github.com/MultiMC/Launcher) for remaining session details. No source was transplanted from these projects.

Microsoft OAuth/PKCE and Xbox request contracts were checked against the Microsoft documentation linked above. Current Minecraft defaults (`/launcher/login`, `/entitlements/license`, `/minecraft/profile`) were cross-checked against Prism and MultiMC; a complete public official Minecraft Services specification was not available to verify independently. Entitlement responses must include both `game_minecraft` and `product_minecraft`, followed by a valid Java profile; subscription-only entitlement variants are not inferred as ownership.

Endpoints are centralized in `src/launcher/auth/CMakeLists.txt` as `MAAGE_AUTH_*_URL` cache settings. Overrides must remain HTTPS and are trusted build configuration: token exchanges disclose credentials to those endpoints. Requests verify certificates, do not follow redirects, bound response size, and time out. Tokens, authorization codes, headers, and credential-bearing responses are never logged.

This implementation has only been compiled. No authentication requests or end-to-end sign-in have been run. Application registration, service approval, and programmer testing remain necessary. The existing native Launcher UI currently runs only on macOS; Windows/Linux UI adapters remain separate unfinished work.
