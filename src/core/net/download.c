#include "maage/download.h"
#include "maage/storage.h"
#include "maage/time.h"
#include "maage/log.h"
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

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
int maage_download_initialize(void) { return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK; }
void maage_download_shutdown(void) { curl_global_cleanup(); }
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
static int maage_transfer_start(maage_transfer *transfer, CURLM *multi)
{
    CURL *easy;
    if (strncmp(transfer->request->url, "https://", 8)) return 0;
    transfer->file = maage_storage_open(transfer->temporary, "wb");
    transfer->easy = easy = curl_easy_init();
    if (!transfer->file || !easy) return 0;
    transfer->received = 0; transfer->detail[0] = 0;
#define MAAGE_CURL(option, value) if (curl_easy_setopt(easy, option, value) != CURLE_OK) return 0
    MAAGE_CURL(CURLOPT_URL, transfer->request->url);
    MAAGE_CURL(CURLOPT_PROTOCOLS_STR, "https");
    MAAGE_CURL(CURLOPT_FOLLOWLOCATION, 0L);
    MAAGE_CURL(CURLOPT_SSL_VERIFYPEER, 1L);
    MAAGE_CURL(CURLOPT_SSL_VERIFYHOST, 2L);
    MAAGE_CURL(CURLOPT_CONNECTTIMEOUT, 15L);
    MAAGE_CURL(CURLOPT_TIMEOUT, 180L);
    MAAGE_CURL(CURLOPT_LOW_SPEED_LIMIT, 256L);
    MAAGE_CURL(CURLOPT_LOW_SPEED_TIME, 30L);
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
    if (curl_multi_add_handle(multi, easy) != CURLM_OK) return 0;
    transfer->active = 1; ++transfer->attempts;
    maage_log_info("maage.core.download", "Downloading (attempt %u/3): %s -> %s", transfer->attempts, transfer->request->url, transfer->request->destination);
    maage_log_debug("maage.core.download", "Temporary file: %s; expected bytes: %.0f; SHA-1: %s", transfer->temporary, transfer->request->size, transfer->request->sha1 ? transfer->request->sha1 : "not supplied");
    return 1;
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
int maage_download_batch(const maage_download_request *requests, unsigned int count,
    const maage_download_observer *observer, char error[512])
{
    maage_transfer transfers[4], *transfer;
    CURLM *multi;
    CURLMsg *message;
    unsigned int i, finished = 0;
    int running, messages, ok = 0;
    double now;
    char hash[41], fingerprint[128];
    double size;
    const char *reason;
    error[0] = 0;
    if (!count || count > 4) { strcpy(error, "Invalid download batch."); return 0; }
    memset(transfers, 0, sizeof(transfers));
    multi = curl_multi_init();
    if (!multi) { strcpy(error, "Cannot allocate HTTPS download scheduler."); return 0; }
    for (i = 0; i < count; ++i) {
        transfer = &transfers[i];
        transfer->request = &requests[i]; transfer->observer = observer;
        if (strlen(requests[i].destination) + 6 >= sizeof(transfer->temporary)) goto cleanup;
        strcpy(transfer->temporary, requests[i].destination); strcat(transfer->temporary, ".part");
        if (!maage_storage_remove(transfer->temporary)) { strcpy(error, "Cannot remove an interrupted download. Check file permissions."); goto cleanup; }
    }
    while (finished < count && !maage_transfer_cancel((void *)observer)) {
        now = maage_time_monotonic_seconds();
        if (now < 0) { strcpy(error, "Monotonic clock unavailable."); goto cleanup; }
        for (i = 0; i < count; ++i) {
            transfer = &transfers[i];
            if (!transfer->active && !transfer->done && now >= transfer->ready && !maage_transfer_start(transfer, multi)) {
                maage_log_error("maage.core.download", "Cannot start %s -> %s (temporary %s)", transfer->request->url, transfer->request->destination, transfer->temporary);
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
            if (maage_transfer_cancel((void *)observer)) goto cleanup;
            maage_log_debug("maage.core.download", "Verifying transfer: %s (%.0f bytes received)", transfer->temporary, transfer->received);
            if (transfer->result == CURLE_OK && maage_storage_stat(transfer->temporary, &size, fingerprint) &&
                (transfer->request->size < 0 || size == transfer->request->size) &&
                (!transfer->request->sha1 || (maage_storage_sha1(transfer->temporary, hash, maage_transfer_cancel, (void *)observer) && !strcmp(hash, transfer->request->sha1)))) {
                if (!maage_storage_replace(transfer->temporary, transfer->request->destination)) {
                    maage_log_error("maage.core.download", "Cannot install verified file: %s -> %s", transfer->temporary, transfer->request->destination);
                    strcpy(error, "Cannot atomically install a verified file. Check storage permissions and disk space."); goto cleanup;
                }
                maage_log_info("maage.core.download", "%s: %s (%.0f bytes; SHA-1: %s)", transfer->request->sha1 ? "Verified and installed" : "Downloaded and installed", transfer->request->destination, size, transfer->request->sha1 ? transfer->request->sha1 : "not supplied");
                transfer->done = 1; ++finished;
            } else if (transfer->attempts >= 3) {
                maage_log_error("maage.core.download", "Download failed: %s -> %s after %u attempts, %.0f bytes received: %s", transfer->request->url, transfer->request->destination, transfer->attempts, transfer->received,
                    transfer->result == CURLE_OK ? "size or SHA-1 mismatch" : transfer->detail[0] ? transfer->detail : curl_easy_strerror((CURLcode)transfer->result));
                sprintf(error, "Download failed after 3 attempts: %.120s (%s). Check your connection and free disk space, then Retry.", transfer->request->url,
                    transfer->result == CURLE_OK ? "size or SHA-1 mismatch" : curl_easy_strerror((CURLcode)transfer->result));
                goto cleanup;
            } else {
                transfer->ready = now + (double)(1U << (transfer->attempts - 1));
                reason = transfer->result == CURLE_OK ? "size or SHA-1 mismatch" :
                    transfer->detail[0] ? transfer->detail : curl_easy_strerror((CURLcode)transfer->result);
                maage_log_warn("maage.core.download", "Retrying %s -> %s in %u seconds after attempt %u: %s", transfer->request->url, transfer->request->destination, 1U << (transfer->attempts - 1), transfer->attempts, reason);
            }
        }
        if (finished < count && curl_multi_poll(multi, NULL, 0, 100, NULL) != CURLM_OK) goto cleanup;
    }
    ok = finished == count;
cleanup:
    for (i = 0; i < count; ++i) {
        maage_transfer_close(&transfers[i], multi);
        if (transfers[i].temporary[0] && !maage_storage_remove(transfers[i].temporary))
            maage_log_warn("maage.core.download", "Cannot remove temporary download: %s", transfers[i].temporary);
    }
    curl_multi_cleanup(multi);
    if (!ok && !error[0]) strcpy(error, maage_transfer_cancel((void *)observer) ? "Download cancelled." : "HTTPS download scheduler failed.");
    if (!ok) {
        if (maage_transfer_cancel((void *)observer))
            maage_log_info("maage.core.download", "Downloads cancelled; verified files retained.");
        else maage_log_error("maage.core.download", "%s", error);
    }
    return ok;
}
