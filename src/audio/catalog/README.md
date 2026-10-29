# Numeric Sound Catalog

The Client-owned catalog maps numeric Session event/material IDs from
`maage/session_protocol.h` to the single resource manifest in
`sound_catalog_manifest.c`. SoundBank resolves its `minecraft/sounds/<stem>[N].ogg`
paths below reconstructed resources. Registration/preload resolves names and
copies decoded variants into Audio buffers; playback performs no filesystem work,
name construction or allocation. One audio unit is one world block.

`maage_sound_catalog_create` borrows SoundBank and Audio. Set the copied callback
port once, then preload before dispatch. All callbacks run on the Client audio
owner. Decode returns owned immutable PCM; upload copies it into an independently
owned buffer; PCM releases even if upload fails. Play borrows a buffer. Destroy
releases successful buffers once; borrowed services must outlive it. The Client
adapter maps the five catalog bus requests to Audio categories and owns listener
configuration. Music and long-lived ambience scheduling belong to the Client scene,
not this one-shot catalog.

Preload is idempotent. Unavailable variants remain absent until destruction/reload;
`PARTIAL` retains usable variants, and playback returns `NOT_FOUND` if no compatible
family is loaded. Material-specific footsteps/digging use compatible stone fallback;
missing ambience can remain silent. Landing events select small/big fall samples,
but ordinary jumps, landings and inventory slot changes emit no such event.
GUI clicks are local/non-positional; thunder requires a real lightning event.
