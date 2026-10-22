# Vendored Vorbis decoder

`stb_vorbis.c` is unmodified stb_vorbis v1.22 by Sean Barrett, pinned to
[nothings/stb commit 1ee679ca2ef753a528db5ba6801e1067b40481b8](https://github.com/nothings/stb/blob/1ee679ca2ef753a528db5ba6801e1067b40481b8/stb_vorbis.c)
(Git blob SHA `3e5c2504c08f76b9d04e4adfbce80818631e835e`).
Distributed under the **MIT** alternative of its dual MIT/public-domain
license; see `LICENSE.stb_vorbis` and the license included in the source.

Only the `maage_vorbis_decoder` target compiles this vendor source with its
C99 target-local exception. `sound_bank.c` includes its declaration-only
section using `STB_VORBIS_HEADER_ONLY` and has no decoder implementation.
The decoder is never fetched by CMake, Launcher, or Client.
