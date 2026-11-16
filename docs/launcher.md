# Launcher and Resources

## Native Shell and Startup

AppKit (macOS), Win32 (Windows) and GTK 3 (Linux) provide one native window: Play, Installations, Accounts, Settings, About. AppKit/Win32 have full-window branded Play with split quick-selection/management controls (Installation, Version, Account, placeholder Playspace) and configuration. AppKit uses the HTML prototype’s viewport-relative scaling, centered cover artwork, fixed-size control proportions and grouped two-row wrap; it displays readiness when Play is blocked. Win32 retains its existing layout and readiness. Management preserves active selections. First startup uses a splash and sign-in/resource onboarding in the same window. GTK retains native header/controls. Installations support create/edit/duplicate/activate/default/folder access and Client build management; local package import is development-only.

Minimal service handles/model precede window presentation. After show/map, a worker initializes networking/account service and loads preferences, manifests and verified Client catalog. UI receives copied results; package inspection/resources never run in the polling loop. Initial appearance follows system, then preferences.

Account worker publishes cached public profiles, restores encrypted sessions and renews the selected profile only when needed. Valid leases avoid network exchange; inactive profiles do not block startup. Transient refresh failure may retain an unexpired lease; failed entitlement cannot grant access. Cancellation disables automatic renewal until explicit retry. Accounts supports browser/device sign-in, profile selection, refresh, sign-out/removal, device-code actions and cancellation. Resources require verified Java access; loss stops work without deleting verified files. See [AGENTS.md](../AGENTS.md) for authentication/security.

## Resource Readiness

`minecraft/metadata/assets-state.json` is an atomic checksummed verification receipt binding metadata, JAR, complete resource inventory and object/logical-path fingerprints. It is a derived local cache, not credentials or protection against a malicious local user. Incomplete/corrupt/unsupported receipts confer no readiness.

Normal preparation hashes metadata/index and compares verified size, file identity and write/change fingerprints for all objects/reconstructed files; parent paths must remain ordinary directories. Unchanged installations need no object hashing, JAR inspection, reconstruction or receipt rewrite. Changed files fall back to SHA-1 and verified acquisition/reconstruction; unchanged JAR/inventory can be reused. Hash fingerprints are checked before/after reading. Archive checks enforce paths/types, bounded expansion, CRC/size, derived hashes and collision preflight.

Settings controls:
- Retry repeats the requested operation when authorized.
- Verify Resources hashes indexed objects, JAR and independent copies; reports defects without downloads/reconstruction.
- Repair Resources fully verifies and restores missing/damaged resources with staged atomic writes.

Publish receipts only after final checks; cancellation preserves verified files and incomplete checkpoints. Unsafe paths/conflicts/corrupt authority are not silently reset.

## Launch and Ownership

Play requires a valid catalog, exact selected package, verified active account, ready resources and no conflicting local Client. Capture account/instance UUID/build ID, recheck resources asynchronously, cancel on changed selection/lost access. Worker verifies exact package, assignment and account again; launch uses structured CLI, skin reference, cwd and combined instance log. Maintenance cannot overlap launch/running Client.

Client lifetime survives Launcher closure. Close cancels and joins workers before releasing borrowed services; failed joins retain resources. Existing filesystem calls finish on their worker. Native controls consume copied snapshots; workers never update them.

AppKit/Win32 default to 1000×670, minimum 700×500 logical content units; below 900 wide, selection cards reflow into 2×2. AppKit hosts only active screens/pages with explicit host sizing, so hidden constraints cannot impose minima. Win32 preserves native DPI, keyboard focus, selection and high contrast; buffered DIB painting and owner-drawn controls support live resize. Cached shadows avoid per-pixel regeneration. Onboarding progress is time-based; page fades use noninteractive outgoing frames and respect reduced motion/high contrast. GTK retains native conventions.

`--manage-accounts` opens Accounts; `--repair-assets` queues Repair in Settings; only explicit `--renderer-demo` starts graphics, independently of account/resources.

## Diagnostics and Validation

[Diagnostics](diagnostics.md) documents DEBUG startup phase timings. Source inspection does not establish startup speed, compilation, authentication or visual correctness; platform/runtime verification remains programmer-owned.
