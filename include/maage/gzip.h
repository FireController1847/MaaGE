#ifndef MAAGE_GZIP_H
#define MAAGE_GZIP_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Compress input from its current position to EOF as one GZip member. Streams
 * must be distinct, binary and exclusively caller-owned during the call.
 * Uses bounded buffers; returns 1 on success, 0 on I/O/compression failure.
 * Does not close or flush either stream. Failed output may be incomplete. */
int maage_gzip_compress(FILE *input, FILE *output);
#ifdef __cplusplus
}
#endif
#endif
