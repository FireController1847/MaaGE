#include "maage/gzip.h"
#include <string.h>
#include <zlib.h>

int maage_gzip_compress(FILE *input, FILE *output)
{
    unsigned char source[16384], compressed[16384];
    z_stream stream;
    size_t count;
    int status = Z_OK, flush, ok = 1;
    if (!input || !output || input == output) return 0;
    memset(&stream, 0, sizeof(stream));
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return 0;
    while (ok && status != Z_STREAM_END) {
        count = fread(source, 1, sizeof(source), input);
        if (ferror(input)) { ok = 0; break; }
        flush = feof(input) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = source; stream.avail_in = (uInt)count;
        do {
            stream.next_out = compressed; stream.avail_out = sizeof(compressed);
            status = deflate(&stream, flush);
            if (status != Z_OK && status != Z_STREAM_END) { ok = 0; break; }
            count = sizeof(compressed) - stream.avail_out;
            if (fwrite(compressed, 1, count, output) != count) { ok = 0; break; }
        } while (status != Z_STREAM_END &&
            (stream.avail_in || flush == Z_FINISH));
    }
    if (deflateEnd(&stream) != Z_OK) ok = 0;
    return ok && status == Z_STREAM_END;
}
