# Numeric Sound Catalog

The Client-owned catalog maps numeric Session event/material IDs from
`maage/session_protocol.h` to the resource manifest in
`sound_catalog_manifest.c`. SoundBank registers numeric clip IDs and resolves
reconstructed asset paths at initialization; this does **not** decode PCM.

A single catalog decoder worker handles requested sound variants on demand.
The owner schedules a first request without blocking presentation; when the
worker completes, the audio owner uploads immutable PCM into Audio, releases
temporary decoder memory, and plays a recent queued first occurrence. Idle
buffers are retired after 20 seconds of nonuse; active mixer voices retain
their buffer independently until playback ends. On subsequent use, retired
variants decode again. Failed optional variants do not disable other sounds.

`maage_sound_catalog_create` borrows SoundBank and Audio. Install its copied
port, call `maage_sound_catalog_prepare`, and regularly call
`maage_sound_catalog_poll` on the audio owner. The background worker reads
only previously registered IDs. Shutdown joins the worker before releasing
decoded PCM and catalog buffers; the borrowed bank and engine outlive it.

Material-specific footsteps and digging retain the stone fallback mapping.
GUI clicks are non-positional; ordinary jumps, landings and slot changes
remain silent. Music and environmental effects are scheduled separately:
music retains its existing selected-track streaming worker, while
environmental effects have their own on-demand background decoder and
20-second idle-buffer retirement.
