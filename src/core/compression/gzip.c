#include "maage/gzip.h"
#include <string.h>
#include <zlib.h>

static maage_status maage_gzip_result(int native) {
    unsigned char code = MAAGE_STATUS_ERROR;
    if (native == Z_OK || native == Z_STREAM_END)
        code = MAAGE_STATUS_SUCCESS;
    else if (native == Z_MEM_ERROR)
        code = MAAGE_STATUS_ALLOCATION_FAILED;
    else if (native == Z_STREAM_ERROR)
        code = MAAGE_STATUS_INVALID_STATE;
    else if (native == Z_VERSION_ERROR)
        code = MAAGE_STATUS_SYSTEM_NOT_SUPPORTED;
    else if (native == Z_BUF_ERROR)
        code = MAAGE_STATUS_BUFFER_OVERFLOW;
    else if (native == Z_DATA_ERROR)
        code = MAAGE_STATUS_MALFORMED_INPUT;
    return maage_status_make(code, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_COMPRESS, 0);
}
maage_status maage_gzip_compress(FILE *input, FILE *output, maage_status_trace *trace) {
    unsigned char source[16384], compressed[16384];
    z_stream stream;
    size_t count;
    int native, flush, finished = 0;
    maage_status result, closed;
    if (!input || !output || input == output) return MAAGE_STATUS_TRACE(trace,
                                                                        maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_COMPRESS,
                                                                                          !input ? 0 : 1),
                                                                        MAAGE_CONTEXT_CORE, MAAGE_OPERATION_COMPRESS);
    memset(&stream, 0, sizeof(stream));
    native = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                          MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
    result = maage_gzip_result(native);
    if (maage_status_failed(result)) return maage_status_trace_record(trace, result,
                                                                      MAAGE_CONTEXT_CORE, MAAGE_OPERATION_INITIALIZE, __FILE__, (unsigned long) __LINE__, native);
    while (!maage_status_failed(result) && !finished) {
        count = fread(source, 1, sizeof(source), input);
        if (ferror(input)) {
            result = maage_status_make(MAAGE_STATUS_IO_ERROR,
                                       MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_READ, 0);
            break;
        }
        flush = feof(input) ? Z_FINISH : Z_NO_FLUSH;
        stream.next_in = source;
        stream.avail_in = (uInt) count;
        do {
            stream.next_out = compressed;
            stream.avail_out = sizeof(compressed);
            native = deflate(&stream, flush);
            result = maage_gzip_result(native);
            if (maage_status_failed(result)) break;
            finished = native == Z_STREAM_END;
            count = sizeof(compressed) - stream.avail_out;
            if (fwrite(compressed, 1, count, output) != count) {
                result = maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_STORAGE, MAAGE_OPERATION_WRITE, 0);
                break;
            }
        } while (!finished && (stream.avail_in || flush == Z_FINISH));
    }
    if (maage_status_failed(result)) maage_status_trace_record(trace, result,
                                                               maage_status_context(result), maage_status_operation(result), __FILE__, (unsigned long) __LINE__, native);
    native = deflateEnd(&stream);
    closed = maage_gzip_result(native);
    if (!maage_status_failed(result) && maage_status_failed(closed)) result = closed;
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_COMPRESS);
}
