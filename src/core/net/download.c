#include "maage/download.h"
#include "maage/storage.h"
#include "maage/time.h"
#include "maage/log.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct maage_transfer {
    CURL *easy;
    FILE *file;
    const maage_download_request *request;
    const maage_download_observer *observer;
    char temporary[MAAGE_PATH_CAPACITY], detail[CURL_ERROR_SIZE];
    double received, ready;
    unsigned int attempts;
    int done, active, result;
} maage_transfer;
maage_status maage_download_options_init(maage_download_options *options)
{
    if (!options) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_INITIALIZE, 0);
    options->max_concurrent = 4;
    options->max_attempts = 3;
    options->connect_timeout_seconds = 15;
    options->timeout_seconds = 180;
    options->low_speed_bytes_per_second = 256;
    options->low_speed_seconds = 30;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_INITIALIZE, 0);
}
static maage_status maage_download_result(CURLcode native, unsigned char operation)
{
    unsigned char code = MAAGE_STATUS_ERROR;
    if (native == CURLE_OK) code = MAAGE_STATUS_SUCCESS;
    else if (native == CURLE_OPERATION_TIMEDOUT) code = MAAGE_STATUS_TIMEOUT;
    else if (native == CURLE_ABORTED_BY_CALLBACK) code = MAAGE_STATUS_INTERRUPTED;
    else if (native == CURLE_OUT_OF_MEMORY) code = MAAGE_STATUS_ALLOCATION_FAILED;
    else if (native == CURLE_UNSUPPORTED_PROTOCOL) code = MAAGE_STATUS_NOT_IMPLEMENTED;
    else if (native == CURLE_REMOTE_ACCESS_DENIED) code = MAAGE_STATUS_ACCESS_DENIED;
    else if (native == CURLE_READ_ERROR || native == CURLE_WRITE_ERROR) code = MAAGE_STATUS_IO_ERROR;
    return maage_status_make(code, MAAGE_CONTEXT_DOWNLOAD, operation, 0);
}
maage_status maage_download_initialize(void)
{ return maage_download_result(curl_global_init(CURL_GLOBAL_DEFAULT), MAAGE_OPERATION_INITIALIZE); }
maage_status maage_download_shutdown(void)
{
    curl_global_cleanup();
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DESTROY, 0);
}
static int maage_transfer_cancel(void *context)
{
    const maage_download_observer *observer = (const maage_download_observer *)context;
    return observer && observer->cancelled && observer->cancelled(observer->context);
}
static size_t maage_transfer_write(char *data, size_t size, size_t count, void *context)
{
    maage_transfer *transfer = (maage_transfer *)context;
    size_t n;
    if (size && count > (size_t)-1 / size) return 0;
    n = size * count;
    if (maage_transfer_cancel((void *)transfer->observer) || transfer->received + (double)n > transfer->request->limit) return 0;
    if (fwrite(data, 1, n, transfer->file) != n) return 0;
    transfer->received += (double)n;
    if (transfer->observer && transfer->observer->bytes) transfer->observer->bytes(transfer->observer->context, (double)n);
    return n;
}
static int maage_transfer_progress(void *context, curl_off_t total, curl_off_t now, curl_off_t upload_total, curl_off_t upload_now)
{
    maage_transfer *transfer = (maage_transfer *)context;
    (void)total; (void)now; (void)upload_total; (void)upload_now;
    return maage_transfer_cancel((void *)transfer->observer);
}
static maage_status maage_transfer_start(maage_transfer *transfer, CURLM *multi, const maage_download_options *options)
{
    CURL *easy;
    CURLcode native;
    if (strncmp(transfer->request->url, "https://", 8)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_OPEN, 0);
    transfer->file = maage_storage_open(transfer->temporary, "wb");
    transfer->easy = easy = curl_easy_init();
    if (!transfer->file || !easy) return maage_status_make(!easy ? MAAGE_STATUS_ALLOCATION_FAILED : MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_OPEN, 0);
    transfer->received = 0; transfer->detail[0] = 0;
#define MAAGE_CURL(option, value) do { native = curl_easy_setopt(easy, option, value); if (native != CURLE_OK) return maage_download_result(native, MAAGE_OPERATION_OPEN); } while (0)
    MAAGE_CURL(CURLOPT_URL, transfer->request->url);
    MAAGE_CURL(CURLOPT_PROTOCOLS_STR, "https");
    MAAGE_CURL(CURLOPT_FOLLOWLOCATION, 0L);
    MAAGE_CURL(CURLOPT_SSL_VERIFYPEER, 1L);
    MAAGE_CURL(CURLOPT_SSL_VERIFYHOST, 2L);
    MAAGE_CURL(CURLOPT_CONNECTTIMEOUT, options->connect_timeout_seconds);
    MAAGE_CURL(CURLOPT_TIMEOUT, options->timeout_seconds);
    MAAGE_CURL(CURLOPT_LOW_SPEED_LIMIT, options->low_speed_bytes_per_second);
    MAAGE_CURL(CURLOPT_LOW_SPEED_TIME, options->low_speed_seconds);
    MAAGE_CURL(CURLOPT_NOSIGNAL, 1L);
    MAAGE_CURL(CURLOPT_FAILONERROR, 1L);
    MAAGE_CURL(CURLOPT_USERAGENT, "MaaGE-Launcher/1.0 (asset setup)");
    MAAGE_CURL(CURLOPT_WRITEFUNCTION, maage_transfer_write);
    MAAGE_CURL(CURLOPT_WRITEDATA, transfer);
    MAAGE_CURL(CURLOPT_XFERINFOFUNCTION, maage_transfer_progress);
    MAAGE_CURL(CURLOPT_XFERINFODATA, transfer);
    MAAGE_CURL(CURLOPT_NOPROGRESS, 0L);
    MAAGE_CURL(CURLOPT_ERRORBUFFER, transfer->detail);
    MAAGE_CURL(CURLOPT_PRIVATE, transfer);
#undef MAAGE_CURL
    if (curl_multi_add_handle(multi, easy) != CURLM_OK) return maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_SUBMIT, 0);
    transfer->active = 1; ++transfer->attempts;
    maage_log_info("maage.core.download", "Downloading (attempt %u/%u): %s -> %s", transfer->attempts, options->max_attempts, transfer->request->url, transfer->request->destination);
    maage_log_debug("maage.core.download", "Temporary file: %s; expected bytes: %.0f; SHA-1: %s", transfer->temporary, transfer->request->size, transfer->request->sha1 ? transfer->request->sha1 : "not supplied");
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_START, 0);
}
static void maage_transfer_close(maage_transfer *transfer, CURLM *multi)
{
    if (transfer->active) curl_multi_remove_handle(multi, transfer->easy);
    transfer->active = 0;
    if (transfer->easy) curl_easy_cleanup(transfer->easy);
    transfer->easy = NULL;
    if (transfer->file) fclose(transfer->file);
    transfer->file = NULL;
}
maage_status maage_download_batch_with_options(const maage_download_request *requests, unsigned int count,
    const maage_download_observer *observer, const maage_download_options *settings, char error[512], maage_status_trace *trace)
{
    maage_download_options defaults;
    const maage_download_options *options = settings;
    maage_transfer *transfers, *transfer;
    CURLM *multi;
    CURLMsg *message;
    unsigned int i, finished = 0, active = 0, delay;
    int running, messages, ok = 0;
    double now;
    char hash[41], fingerprint[128];
    double size;
    const char *reason;
    maage_status result = maage_status_make(MAAGE_STATUS_ERROR, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, 0);
    maage_status child;
    if (!error) return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
        MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, 4), MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD);
    error[0] = 0;
    if (!options) { maage_download_options_init(&defaults); options = &defaults; }
    if (!requests || !count || !options->max_concurrent || !options->max_attempts ||
        options->connect_timeout_seconds <= 0 || options->timeout_seconds < 0 ||
        options->low_speed_bytes_per_second <= 0 || options->low_speed_seconds <= 0 ||
        (size_t)count > (size_t)-1 / sizeof(*transfers)) {
        strcpy(error, "Invalid download requests or policy.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT,
            MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, MAAGE_DETAIL_ARGUMENT_UNKNOWN), MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD);
    }
    transfers = (maage_transfer *)calloc((size_t)count, sizeof(*transfers));
    if (!transfers) { strcpy(error, "Cannot allocate download transfers.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, 0), MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD); }
    multi = curl_multi_init();
    if (!multi) { free(transfers); strcpy(error, "Cannot allocate HTTPS download scheduler.");
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED,
            MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_INITIALIZE, 0), MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD); }
    for (i = 0; i < count; ++i) {
        transfer = &transfers[i];
        transfer->request = &requests[i]; transfer->observer = observer;
        if (!requests[i].destination || !requests[i].url || strlen(requests[i].destination) + 6 >= sizeof(transfer->temporary)) {
            result = maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, 0); goto cleanup;
        }
        strcpy(transfer->temporary, requests[i].destination); strcat(transfer->temporary, ".part");
        child = maage_storage_remove(transfer->temporary);
        if (maage_status_failed(child)) { result = MAAGE_STATUS_PROPAGATE(trace, child, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD);
            strcpy(error, "Cannot remove an interrupted download. Check file permissions."); goto cleanup; }
    }
    while (finished < count && !maage_transfer_cancel((void *)observer)) {
        now = maage_time_monotonic_seconds();
        if (now < 0) { result = maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_CORE, MAAGE_OPERATION_QUERY, 0); strcpy(error, "Monotonic clock unavailable."); goto cleanup; }
        for (i = 0; i < count; ++i) {
            transfer = &transfers[i];
            if (!transfer->active && !transfer->done && active < options->max_concurrent && now >= transfer->ready) {
                child = maage_transfer_start(transfer, multi, options);
                if (!maage_status_failed(child)) { ++active; continue; }
                result = MAAGE_STATUS_PROPAGATE(trace, child, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_START);
                strcpy(error, "Cannot start HTTPS request or open temporary file. Check storage permissions."); goto cleanup;
            }
        }
        if (curl_multi_perform(multi, &running) != CURLM_OK) goto cleanup;
        while ((message = curl_multi_info_read(multi, &messages)) != NULL) {
            if (message->msg != CURLMSG_DONE) continue;
            transfer = NULL;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &transfer);
            if (!transfer) goto cleanup;
            transfer->result = message->data.result;
            if (transfer->file && fclose(transfer->file) != 0) transfer->result = CURLE_WRITE_ERROR;
            transfer->file = NULL;
            maage_transfer_close(transfer, multi);
            if (active) --active;
            if (maage_transfer_cancel((void *)observer)) goto cleanup;
            maage_log_debug("maage.core.download", "Verifying transfer: %s (%.0f bytes received)", transfer->temporary, transfer->received);
            child = maage_download_result((CURLcode)transfer->result, MAAGE_OPERATION_DOWNLOAD);
            if (!maage_status_failed(child)) child = maage_storage_stat(transfer->temporary, &size, fingerprint);
            if (!maage_status_failed(child) && transfer->request->size >= 0 && size != transfer->request->size)
                child = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_VERIFY, 0);
            if (!maage_status_failed(child) && transfer->request->sha1) {
                child = maage_storage_sha1(transfer->temporary, hash, maage_transfer_cancel, (void *)observer);
                if (!maage_status_failed(child) && strcmp(hash, transfer->request->sha1))
                    child = maage_status_make(MAAGE_STATUS_MALFORMED_INPUT, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_VERIFY, 0);
            }
            if (!maage_status_failed(child)) {
                child = maage_storage_replace(transfer->temporary, transfer->request->destination);
                if (maage_status_failed(child)) {
                    result = MAAGE_STATUS_PROPAGATE(trace, child, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_SAVE);
                    strcpy(error, "Cannot atomically install a verified file. Check storage permissions and disk space."); goto cleanup;
                }
                if (transfer->attempts > 1) maage_status_trace_recover(trace, child, MAAGE_CONTEXT_DOWNLOAD,
                    MAAGE_OPERATION_VERIFY, __FILE__, (unsigned long)__LINE__);
                maage_log_info("maage.core.download", "%s: %s (%.0f bytes; SHA-1: %s)", transfer->request->sha1 ? "Verified and installed" : "Downloaded and installed", transfer->request->destination, size, transfer->request->sha1 ? transfer->request->sha1 : "not supplied");
                transfer->done = 1; ++finished;
            } else if (transfer->attempts >= options->max_attempts) {
                result = maage_status_trace_record(trace, child, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD,
                    __FILE__, (unsigned long)__LINE__, transfer->result);
                sprintf(error, "Download failed after %u attempts: %.120s (%s). Check your connection and free disk space, then Retry.", options->max_attempts, transfer->request->url,
                    transfer->result == CURLE_OK ? "size or SHA-1 mismatch" : curl_easy_strerror((CURLcode)transfer->result));
                goto cleanup;
            } else {
                delay = transfer->attempts >= 7 ? 60U : (1U << (transfer->attempts - 1));
                transfer->ready = now + (double)delay;
                reason = transfer->result == CURLE_OK ? "size or SHA-1 mismatch" :
                    transfer->detail[0] ? transfer->detail : curl_easy_strerror((CURLcode)transfer->result);
                maage_status_trace_record(trace, child, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD,
                    __FILE__, (unsigned long)__LINE__, transfer->result);
                maage_log_status(MAAGE_LOG_WARNING, "maage.core.download", child,
                    "Retrying %s -> %s in %u seconds after attempt %u: %s", transfer->request->url,
                    transfer->request->destination, delay, transfer->attempts, reason);
            }
        }
        if (finished < count && curl_multi_poll(multi, NULL, 0, 100, NULL) != CURLM_OK) goto cleanup;
    }
    ok = finished == count;
cleanup:
    for (i = 0; i < count; ++i) {
        maage_transfer_close(&transfers[i], multi);
        if (transfers[i].temporary[0] && maage_status_failed(maage_storage_remove(transfers[i].temporary)))
            maage_log_warn("maage.core.download", "Cannot remove temporary download: %s", transfers[i].temporary);
    }
    curl_multi_cleanup(multi);
    free(transfers);
    if (!ok && !error[0]) strcpy(error, maage_transfer_cancel((void *)observer) ? "Download cancelled." : "HTTPS download scheduler failed.");
    if (ok) return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_DOWNLOAD, 0);
    if (maage_transfer_cancel((void *)observer)) result = maage_status_make(MAAGE_STATUS_INTERRUPTED,
        MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD, 0);
    return MAAGE_STATUS_PROPAGATE(trace, result, MAAGE_CONTEXT_DOWNLOAD, MAAGE_OPERATION_DOWNLOAD);
}

maage_status maage_download_batch(const maage_download_request *requests, unsigned int count,
    const maage_download_observer *observer, char error[512], maage_status_trace *trace)
{
    return maage_download_batch_with_options(requests, count, observer, NULL, error, trace);
}
