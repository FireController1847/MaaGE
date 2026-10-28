# Authenticated Local-Player Skins

Launcher `1.0.0-alpha.40` and Client `0.1.0-alpha.46` share one nonsecret
appearance contract. Server behavior, world storage and session protocol are
unchanged. Runtime behavior remains programmer-owned and unverified.

## Profile and Acquisition

The existing authenticated `GET https://api.minecraftservices.com/minecraft/profile`
supplies `id`, `name`, and the `skins` array. Only an `ACTIVE` entry supplies
`url` and `variant` (`CLASSIC` or `SLIM`). UUID/name must match the verified Java
profile; ambiguous active entries, invalid types/models and unsafe URLs fail as
optional resource problems. An empty active-skin selection chooses the original
default appearance. The response shape was cross-checked against the maintained
[Prism Launcher profile request](https://github.com/PrismLauncher/PrismLauncher/blob/develop/launcher/minecraft/auth/steps/MinecraftProfileStep.cpp)
and [profile parser](https://github.com/PrismLauncher/PrismLauncher/blob/develop/launcher/minecraft/auth/Parsers.cpp).
This is implementation evidence, not a new official Mojang API guarantee.

The public session-server endpoint was considered. It adds a second profile
lookup and Base64 texture-property parsing that the already authenticated response
does not need. No username lookup, second authentication flow or public-profile
fallback can change the selected identity or grant access.

Skin processing runs on the existing access worker after profile/entitlement
verification and encrypted credential persistence. The verifier's public response
is reused, avoiding another profile request. On restoration of a still-valid
saved access lease, an existing usable skin is reused without network work. If
that cache is absent/unusable, the same authenticated profile request has a
five-second timeout. PNG acquisition uses existing `MaaGE::Download`, one attempt,
a three-second connect timeout, eight-second total timeout and 256 KiB limit.
Normal access renewal refreshes skin metadata alongside the roughly hourly
verification lease; explicit account refresh also picks up changes. A failed
optional refresh waits for the next account verification rather than retrying
in a gameplay or rendering loop.

Only the exact `textures.minecraft.net/texture/<64 lowercase hex digits>` route is
accepted. An official URL spelling with `http` is normalized to a newly constructed
HTTPS URL; no plaintext request is made. Ports, userinfo, query strings, fragments,
other hosts, redirects and non-200 download responses are rejected. TLS verification remains enabled. Profile
responses retain the existing 128 KiB bound. Access tokens stay inside the existing
authentication service and encrypted account storage.

## Cache and Startup Contract

The shared application-data location is:

```
minecraft/skins/<profile-uuid>/<texture-identity>.png
minecraft/skins/<profile-uuid>/current.json
```

One profile-scoped storage lock serializes writers. Downloads first enter staging;
the PNG signature, IHDR dimensions, compressed size and complete decoder result
must pass before atomic publication. Only 64x64 modern or classic 64x32 PNGs are
supported. Valid unchanged texture files are reused. Metadata records the public
appearance reference and check time, never credentials or token-derived URLs.
Metadata publishes after its image, so failed downloads/validation preserve the
previous known-good selection. Cache reads validate profile ownership and images;
the Client rejects linked cache directories and linked/nonregular image files.
Private download diagnostics suppress profile paths, URLs and raw remote errors.

Play copies the exact active verified account, reads only that UUID's cache and
rechecks selection/access immediately before direct subprocess creation. It does
not select an arbitrary verified account. Instance arguments cannot supply
`--player-appearance`, `--instance` or `--data-directory` overrides. The Launcher
adds one bounded argument:

```
--player-appearance=1:<uuid>:<username>:<classic|slim>:<texture-identity|->
```

The Client validates the version, lengths, character domains, model and duplicate
arguments at initialization. It derives the cache path below the shared data root;
no arbitrary path or URL enters the renderer. A direct Client launch without this
argument uses the original default. The reference conveys appearance only, not
proof of authentication or Server authority.

Current Client package descriptors, registry records and `--version-json` advertise
`player_appearance: 1` alongside `instance_storage: 1`. Older package/registry
records are rejected; reimport current Client packages or recreate disposable
development registry data. There is no migration or automatic deletion. No world
format change is required.

## Shared Player Presentation

`MaaGE::Skin` bounds/decodes through the existing Image API and normalizes one
64x64 RGBA image during initialization. Legacy classic skins mirror the original
right limbs into modern left-limb regions and keep unused outer regions clear.
Base skin faces become opaque, matching Minecraft skin handling; modern outer
alpha remains intact. Legacy all-opaque hat regions receive the reference alpha
cleanup. Slim metadata with a legacy 64x32 image is rejected rather than stretched.

The local 1.7.10 `unpacked` directory was inspected; the matching adjacent sources
JAR supplies `ModelBiped`, `ModelBox`, `RenderPlayer.renderFirstPersonArm`,
`AbstractClientPlayer` and `ImageBufferDownload.parseUserSkin`. That baseline uses
classic 64x32 geometry; modern independent limbs, three-pixel arms and clothing
layers extend it without reproducing obsolete username download behavior.

`src/client/player_model.c` defines the six base cuboids and their six outer
layers, with common face UV generation. Classic arms are four pixels wide; slim
arms are three, including their end faces and model offsets. Modern left/right
arms and legs have independent texture regions. Hat, jacket, sleeves and pants
use inflated geometry. Existing GUI mouse rotation, clipping, depth ordering and
lighting remain in the inventory preview. First-person geometry keeps the current
camera/equip/swing transforms and item positioning, with a correctly sized right
arm plus sleeve.

The skin decodes once in Client resource initialization and occupies one padded
region in the existing world/item atlas. The existing GUI atlas copies that atlas
and derives its skin UVs from the same resource; it does not load a second Steve
image or resolve a second account. Both views reuse their textures across frames
and world transitions. Outer first-person faces use the existing sorted translucent
material, preserving fractional alpha; GUI faces use existing alpha compositing.
No texture upload, file lookup or skin download occurs per frame or world entry.

Metal and Vulkan consume the same geometry, nearest sampling, materials and GUI
vertices through their existing APIs. Their native shaders, pipeline contracts and
vertex layouts did not need modification. No third-person gameplay renderer,
equipment/capes, skin editor/uploader or multiplayer synchronization was added.

## Failure Behavior and Compilation

Skin failure never grants, revokes or extends Minecraft access. A usable cached
skin remains available offline for the same account; existing authentication lease
and entitlement rules still govern whether the Launcher permits Play. Expired
access is not bypassed. Missing/corrupt skin files fall back once to the original
Steve resource; if that is also unavailable, the existing generated missing tile
keeps presentation usable. Model metadata resets to classic with the default.
Switching accounts cannot select another UUID's cached image. Existing Clients
retain their initialization-time appearance; refreshed skins apply to a new Client.

macOS ARM64 Debug and Release Launcher/Client compile and link successfully using
the enabled Metal and Vulkan/MoltenVK implementations. New model/UV/alpha work is
shared C90; unchanged native renderer implementations are reused. The Client
linker reports duplicate-library warnings. As-built uncompressed executable file
sizes are 3,912,744 bytes for Launcher and 3,987,872 bytes for Client; these are not
bundle sizes or disk-allocation measurements. Windows/Linux compilation, live Mojang
requests, PNG failure scenarios, shaders and visual behavior remain unverified.
No applications, tests, harnesses, fixtures, benchmarks or GitHub Actions ran.

## Pending Programmer Checklist

- **Identity:** Launch from each selected verified account and compare the skin;
  switch accounts and confirm that absent cache uses Steve rather than another
  account. Reserved instance arguments must fail validation.
- **Caching/offline:** Relaunch with an unchanged skin and observe cache reuse.
  Change the account skin, refresh verification and start a new Client. Go offline
  with a current access lease and confirm cached appearance; optional refresh
  failure must preserve it. Expired authorization still follows existing policy.
- **Models:** Check classic and slim skins, asymmetric left/right limbs and a
  legacy classic image. Compare the empty-hand arm, hand orientation, swing and
  held-item positioning with the full inventory model.
- **Outer layers:** Check transparent holes and partial alpha in hats, jackets,
  sleeves and pants, including cursor rotation, GUI scales and clipping.
- **Backends/lifecycle:** Repeat on Metal and Vulkan. Enter/leave worlds and close
  normally; appearance should remain stable without repeated skin uploads.
- **Fallbacks:** Try missing, interrupted, malformed and unsupported-size cache
  images; expect the correct account's known-good skin or original Steve fallback,
  bounded diagnostics and playable presentation.

These scenarios have not been executed. Commit details are supplied in the
completion response; concurrent Launcher/icon changes remain outside this work.
