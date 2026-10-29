#ifndef MAAGE_DOWNLOAD_H
#define MAAGE_DOWNLOAD_H
#include "maage/status.h"
/* Blocking worker-thread API. HTTPS/HTTP 200 only, verified TLS, no redirects.
 * Caller owns and serializes destinations. Files are replaced atomically
 * only after size/hash verification. Concurrency is independent of count. */
typedef struct maage_download_request {
    const char *url;
    const char *destination;
    const char *sha1; /* NULL only for metadata without a supplied digest. */
    double size;      /* -1 when unknown. */
    double limit;
} maage_download_request;
typedef struct maage_download_observer {
    int (*cancelled)(void *context);
    void (*bytes)(void *context, double bytes);
    void *context;
} maage_download_observer;
typedef struct maage_download_options {
    unsigned int max_concurrent, max_attempts;
    int private_diagnostics; /* Hide URLs, paths, digests and server error text. */
    long connect_timeout_seconds, timeout_seconds;
    long low_speed_bytes_per_second, low_speed_seconds;
} maage_download_options;
maage_status maage_download_options_init(maage_download_options *options);
maage_status maage_download_batch_with_options(const maage_download_request *requests, unsigned int count,
                                               const maage_download_observer *observer, const maage_download_options *options, char error[512], maage_status_trace *trace);
maage_status maage_download_initialize(void);
maage_status maage_download_shutdown(void);
maage_status maage_download_batch(const maage_download_request *requests, unsigned int count,
                                  const maage_download_observer *observer, char error[512], maage_status_trace *trace);
#endif
