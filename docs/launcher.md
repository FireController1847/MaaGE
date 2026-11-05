# Launcher and Resources

## Native Shell and Startup

The Launcher uses AppKit on macOS, Win32 on Windows and GTK 3 on Linux. One window provides Play, Installations, Accounts, Settings and About. Its header retains the active installation and Minecraft account across pages. Play exposes an installation selector, Client version, launch requirements and the primary action. Installations retains creation, editing, duplication, activation, default selection, folder access and Client build management. Local executable/manifest import remains a development feature.

Before presentation the Launcher allocates service handles and an empty presentation model. Core logging initializes earlier, with a separate phase timing. After the native show/map boundary, a startup worker initializes networking, starts the account service and loads Launcher preferences, instance manifests and the verified Client catalog. The UI receives that catalog only after worker completion. Package inspection and resource preparation do not run from the UI polling loop. Appearance preferences apply when the catalog arrives; the first presentation uses the system appearance.

The access worker publishes cached public profile metadata, then restores encrypted sessions locally. Valid bounded verification leases remain usable without a network request. Once the active profile is selected, the existing worker renews that profile when needed. Inactive profiles do not hold startup behind sequential network exchanges. A transient refresh failure can retain an existing verified lease until its original deadline; failed entitlement verification cannot grant access. Cancellation suppresses automatic renewal until an explicit account operation retries it.

Accounts contains profile selection, browser or device-code sign-in, refresh/retry, sign-out, removal, device-code copy/open actions and cancellation. Cached profile names never authorize access. Resource preparation starts only with current verified Java Edition access. It can run while the user navigates or manages accounts; losing authorization stops it cooperatively without deleting verified resources.

## Resource Readiness

`minecraft/metadata/assets-state.json` is a derived, versioned verification receipt, atomically published under the resource writer lock. Its checksum covers the metadata identities, indexed-object and logical-path fingerprints, client JAR identity, complete client resource inventory and completion marker. An incomplete, damaged or unsupported receipt cannot establish readiness. Receipts are local caches, not credentials or protection against a malicious local user.

Normal preparation hashes the small version metadata and asset index, validates their expected identities, then checks every recorded object and reconstructed file against its verified size and filesystem fingerprint. Fingerprints include file identity and write/change metadata. Parent paths must remain ordinary directories. An unchanged completed installation needs no object-content hashing, JAR opening, ZIP inventory, reconstruction or receipt rewrite.

A changed file or identity falls back to SHA-1 verification and the existing acquisition/reconstruction path. An unchanged JAR and verified client inventory can still be reused while indexed resources need work. Changed client resources require verified archive inspection. Source streams retain ZIP path/type/expansion checks, CRC/size validation, derived SHA-1 and collision preflight. Content checks capture fingerprints before and after hashing so concurrent changes cannot be recorded as verified.

Settings provides three explicit controls in addition to cancellation:

- Retry repeats the requested preparation, verification or repair mode, waiting for authorization when necessary.
- Verify Resources hashes indexed objects, the client JAR and independent reconstructed resources. It does not download or reconstruct missing files; defects direct the user to Repair.
- Repair Resources performs full verification and restores missing or damaged resources through staged, verified atomic writes.

Completed receipts publish only after final checks. Cancellation and interrupted operations retain usable verified files and incomplete checkpoints. Unsafe paths, linked directories, conflicting resources and corrupt authoritative configuration are never silently reset.

## Launch and Ownership

Play requires a loaded valid Client catalog, the exact selected available package, a verified active account, completed resource preparation and no conflicting local Client operation. A Play request captures the account, instance UUID and build ID, then rechecks resources asynchronously. A changed selection or lost authorization cancels that request. The Client worker reloads/verifies the exact managed package and instance assignment, validates current account identity again and preserves structured CLI arguments, skin references, working directory and combined instance log capture.

Maintenance does not overlap a local launch or running Client. The Client remains an independent process whose lifetime survives Launcher closure. Closing hides/destroys native controls, cancels access/resource operations and joins owned workers before releasing their borrowed services. A failed join retains borrowed resources. Existing catalog/package filesystem calls finish on their worker before teardown; they do not have mid-file cancellation.

Workers never update native controls. The UI polls copied account/resource snapshots and takes completed catalog/Client outputs. Navigation remains available while these operations run. Windows keeps native DPI, keyboard selection, persistent installation-row highlighting and high-contrast system colors, with explicit System/Light/Dark appearance. AppKit and GTK retain their native control and appearance conventions.

`--manage-accounts` opens Accounts; `--repair-assets` opens Settings with Repair queued. Only explicit `--renderer-demo` starts the independent graphics diagnostic, which consumes no account or Minecraft resources.

## Diagnostics and Validation

DEBUG phase timings use `maage.launcher.startup`; see [diagnostics](diagnostics.md). They measure work actually performed, including first Play availability when all requirements are satisfied. Source implementation and static inspection do not establish startup speed, compilation, authentication behavior or visual quality. Platform builds and runtime/visual verification remain programmer-owned.
