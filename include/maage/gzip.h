#ifndef MAAGE_GZIP_H
#define MAAGE_GZIP_H
#include <stdio.h>
#include "maage/status.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Compress input from its current position to EOF as one GZip member. Streams
 * must be distinct, binary and exclusively caller-owned during the call.
 * Uses bounded buffers and reports native/compression failures through a status.
 * Does not close or flush either stream. Failed output may be incomplete. */
maage_status maage_gzip_compress(FILE *input, FILE *output, maage_status_trace *trace);
#ifdef __cplusplus
}
#endif
#endif
