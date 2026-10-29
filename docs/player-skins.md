# Authenticated Local-Player Skins

## Profile and Acquisition

The existing authenticated `GET https://api.minecraftservices.com/minecraft/profile`
supplies `id`, `name`, and the `skins` array. Only an `ACTIVE` entry supplies
`url` and `variant` (`CLASSIC` or `SLIM`). UUID/name must match the verified Java
profile; ambiguous active entries, invalid types/models and unsafe URLs fail as
optional resource problems. An empty active-skin selection chooses the original
default appearance.
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
alpha remains intact. Legacy all-opaque hat regions are cleared. Slim metadata with a legacy 64x32 image is rejected rather than stretched.

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
vertex layouts did not need modification. [Third-person rendering](third-person.md) now consumes those same cuboids and
UVs through cached part instances. Armor/capes, a skin editor/uploader and
multiplayer synchronization remain outside this appearance contract.

## Failure Behavior

Skin failure never grants, revokes or extends Minecraft access. A usable cached
skin remains available offline for the same account; existing authentication lease
and entitlement rules still govern whether the Launcher permits Play. Expired
access is not bypassed. Missing/corrupt skin files fall back once to the original
Steve resource; if that is also unavailable, the existing generated missing tile
keeps presentation usable. Model metadata resets to classic with the default.
Switching accounts cannot select another UUID's cached image. Existing Clients
retain their initialization-time appearance; refreshed skins apply to a new Client.

## Manual Review

Check selected-account isolation, unchanged-cache reuse, optional refresh failure,
current/expired access leases and reserved launch arguments. Compare classic,
slim, asymmetric and legacy skins plus partially transparent clothing in the arm,
inventory and third-person views. Missing/corrupt caches must retain the same
account's usable appearance or default fallback. Repeat on Metal and Vulkan;
these runtime scenarios remain programmer-owned.
