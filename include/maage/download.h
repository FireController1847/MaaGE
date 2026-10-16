#ifndef MAAGE_DOWNLOAD_H
#define MAAGE_DOWNLOAD_H
/* Blocking worker-thread API. Up to four requests, three attempts each. HTTPS
 * only, verified TLS, no redirects. Caller serializes destination ownership.
 * Existing destinations are replaced only after size/hash verification. */
typedef struct maage_download_request {
    const char *url;
    const char *destination;
    const char *sha1; /* NULL only for metadata without a supplied digest. */
    double size; /* -1 when unknown. */
    double limit;
} maage_download_request;
typedef struct maage_download_observer {
    int (*cancelled)(void *context);
    void (*bytes)(void *context, double bytes);
    void *context;
} maage_download_observer;
int maage_download_initialize(void);
void maage_download_shutdown(void);
int maage_download_batch(const maage_download_request *requests, unsigned int count,
    const maage_download_observer *observer, char error[512]);
#endif
