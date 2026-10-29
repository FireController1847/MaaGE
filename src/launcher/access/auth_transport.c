#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "oauth.h"
#include "maage/time.h"
#include "maage/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "maage/crypto.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
typedef SOCKET maage_auth_socket;
#define MAAGE_AUTH_INVALID INVALID_SOCKET
#define maage_auth_socket_close closesocket
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
typedef int maage_auth_socket;
#define MAAGE_AUTH_INVALID (-1)
#define maage_auth_socket_close close
#endif
#define MAAGE_AUTH_HTTP_LIMIT 40960
struct maage_auth_listener {
    maage_auth_socket socket, client;
    unsigned port;
    maage_status_trace *trace;
};
static maage_status maage_auth_transport_failure(maage_status_trace *trace, unsigned char operation) {
    long native;
#ifdef _WIN32
    native = (long) WSAGetLastError();
#else
    native = (long) errno;
#endif
    return maage_status_trace_record(trace, maage_status_make(MAAGE_STATUS_IO_ERROR, MAAGE_CONTEXT_AUTH, operation, 0), MAAGE_CONTEXT_AUTH, operation, __FILE__, (unsigned long) __LINE__, native);
}
static int maage_auth_socket_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}
static int maage_auth_socket_retry(void) {
    int error = maage_auth_socket_error();
#ifdef _WIN32
    return error == WSAEINTR || error == WSAEWOULDBLOCK;
#else
    return error == EINTR || error == EAGAIN || error == EWOULDBLOCK;
#endif
}
static int maage_auth_network(void) {
#ifdef _WIN32
    WSADATA data;
    int result = WSAStartup(MAKEWORD(2, 2), &data);
    if (result) maage_log_verbose("maage.launcher.access.callback", "Cannot initialize loopback transport (OS error=%d).", result);
    return result == 0;
#else
    return 1;
#endif
}
static void maage_auth_network_done(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}
/* All sockets are nonblocking; each read/write is additionally bounded below.
 * Descriptor inheritance is disabled before launching the system browser. */
static int maage_auth_socket_prepare(maage_auth_socket socket) {
#ifdef _WIN32
    u_long nonblocking = 1;
    if (!SetHandleInformation((HANDLE) socket, HANDLE_FLAG_INHERIT, 0) || ioctlsocket(socket, FIONBIO, &nonblocking)) return 0;
#else
    int flags;
    if (socket >= FD_SETSIZE) return 0;
    flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFD, FD_CLOEXEC) || fcntl(socket, F_SETFL, flags | O_NONBLOCK)) return 0;
#ifdef SO_NOSIGPIPE
    {
        int enabled = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled))) return 0;
    }
#endif
#endif
    return 1;
}
static int maage_auth_ready(maage_auth_socket socket, int write_ready, unsigned ms) {
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    timeout.tv_sec = ms / 1000;
    timeout.tv_usec = (ms % 1000) * 1000;
#ifdef _WIN32
    return select(0, write_ready ? NULL : &set, write_ready ? &set : NULL, NULL, &timeout);
#else
    return select(socket + 1, write_ready ? NULL : &set, write_ready ? &set : NULL, NULL, &timeout);
#endif
}
maage_status maage_auth_listen(unsigned *port, maage_auth_listener **output, maage_status_trace *trace) {
    struct sockaddr_in address;
#ifdef _WIN32
    int length = sizeof(address), exclusive = 1;
#else
    socklen_t length = sizeof(address);
#endif
    maage_auth_listener *listener;
    maage_status result;
    if (output) *output = NULL;
    if (!port || !output) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, !port ? 0 : 1);
    *port = 0;
    if (!maage_auth_network()) return maage_auth_transport_failure(trace, MAAGE_OPERATION_INITIALIZE);
    listener = (maage_auth_listener *) malloc(sizeof(*listener));
    if (!listener) {
        maage_auth_network_done();
        return MAAGE_STATUS_TRACE(trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_OPEN);
    }
    listener->trace = trace;
    listener->socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    listener->client = MAAGE_AUTH_INVALID;
    listener->port = 0;
    if (listener->socket == MAAGE_AUTH_INVALID || !maage_auth_socket_prepare(listener->socket)) goto failed;
#ifdef _WIN32
    if (setsockopt(listener->socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *) &exclusive, sizeof(exclusive))) goto failed;
#endif
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(0x7f000001UL); /* 127.0.0.1, never ANY or a resolved hostname. */
    address.sin_port = 0;                          /* The OS chooses an available ephemeral port. */
    if (bind(listener->socket, (struct sockaddr *) &address, sizeof(address)) || listen(listener->socket, 4) || getsockname(listener->socket, (struct sockaddr *) &address, &length)) goto failed;
    listener->port = ntohs(address.sin_port);
    *port = listener->port;
    maage_log_info("maage.launcher.access.callback", "OAuth HTTP listener ready on 127.0.0.1:%u (expected path=/; method=GET).", *port);
    *output = listener;
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_OPEN, 0);
failed:
    result = maage_auth_transport_failure(trace, MAAGE_OPERATION_OPEN);
    maage_auth_close(listener);
    return result;
}
static void maage_auth_client_close(maage_auth_listener *listener) {
    if (listener->client != MAAGE_AUTH_INVALID) {
        maage_auth_socket_close(listener->client);
        listener->client = MAAGE_AUTH_INVALID;
    }
}
maage_status maage_auth_close(maage_auth_listener *listener) {
    maage_status result = maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_CLOSE, 0);
    if (!listener) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_CLOSE, 0);
    maage_auth_client_close(listener);
    if (listener->socket != MAAGE_AUTH_INVALID) {
        if (maage_auth_socket_close(listener->socket)) result = maage_auth_transport_failure(listener->trace, MAAGE_OPERATION_CLOSE);
        maage_log_info("maage.launcher.access.callback", "OAuth loopback listener closed (port=%u).", listener->port);
    }
    free(listener);
    maage_auth_network_done();
    return result;
}
static maage_status maage_auth_http_reply(maage_auth_listener *listener, int status) {
    const char *title = status == 200 ? "Microsoft authorization received" : "Authorization callback not accepted";
    const char *message = status == 200 ? "Return to MaaGE to finish verifying Minecraft access. You can close this tab." : "Return to MaaGE to retry or cancel sign-in. You can close this tab.";
    const char *label = status == 200 ? "OK" : status == 404 ? "Not Found"
                                           : status == 405   ? "Method Not Allowed"
                                                             : "Bad Request";
    char body[512], response[1024];
    size_t size, sent = 0;
    double deadline = maage_time_monotonic_seconds() + .5;
    int ready, count;
    if (listener->client == MAAGE_AUTH_INVALID) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WRITE, 0);
    sprintf(body, "<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>MaaGE sign-in</title><h1>%s</h1><p>%s</p></html>", title, message);
    sprintf(response, "HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nPragma: no-cache\r\nReferrer-Policy: no-referrer\r\nContent-Security-Policy: default-src 'none'; frame-ancestors 'none'; base-uri 'none'\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n%s\r\n%s", status, label, (unsigned long) strlen(body), status == 405 ? "Allow: GET\r\n" : "", body);
    size = strlen(response);
    while (sent < size && maage_time_monotonic_seconds() >= 0 && maage_time_monotonic_seconds() < deadline) {
        ready = maage_auth_ready(listener->client, 1, 50);
        if (ready < 0) {
            if (maage_auth_socket_retry()) continue;
            break;
        }
        if (!ready) continue;
#ifdef MSG_NOSIGNAL
        count = send(listener->client, response + sent, (int) (size - sent), MSG_NOSIGNAL);
#else
        count = send(listener->client, response + sent, (int) (size - sent), 0);
#endif
        if (count < 0 && maage_auth_socket_retry()) continue;
        if (count <= 0) break;
        sent += (size_t) count;
    }
    if (sent != size) maage_log_verbose("maage.launcher.access.callback", "Browser callback page could not be fully delivered; authorization result unchanged.");
    maage_auth_client_close(listener);
    return maage_status_make(sent == size ? MAAGE_STATUS_SUCCESS : MAAGE_STATUS_PARTIAL, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WRITE, 0);
}
maage_status maage_auth_reply(maage_auth_listener *listener, int accepted) {
    if (!listener) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WRITE, 0);
    /* Stop accepting connections as soon as the state-validated result completes
     * an attempt. Keep only the already accepted socket to return its static page. */
    if (accepted && listener->socket != MAAGE_AUTH_INVALID) {
        maage_auth_socket_close(listener->socket);
        listener->socket = MAAGE_AUTH_INVALID;
        maage_log_info("maage.launcher.access.callback", "OAuth loopback listener closed after validated callback (port=%u).", listener->port);
    }
    return MAAGE_STATUS_PROPAGATE(listener->trace, maage_auth_http_reply(listener, accepted > 0 ? 200 : 400), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WRITE);
}
static int maage_auth_ascii_equal(const char *left, const char *right) {
    int a, b;
    do {
        a = (unsigned char) *left++;
        b = (unsigned char) *right++;
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (b >= 'A' && b <= 'Z') b += 'a' - 'A';
        if (a != b) return 0;
    } while (a);
    return 1;
}
/* This is a bounded, single-request HTTP adapter, not a general web server.
 * Only origin-form GET /, an exact localhost Host header, and an empty body are
 * accepted. Header/query values never enter logs or browser response pages. */
static int maage_auth_http_query(char *request, unsigned port, char *query, size_t capacity, int *status, const char **reason) {
    char *line, *end, *method_end, *target, *target_end, *version, *colon, *value, *tail;
    const unsigned char *ch;
    char host[64];
    int host_seen = 0;
    size_t size;
    *status = 400;
    *reason = "malformed HTTP request";
    end = strstr(request, "\r\n");
    if (!end) return 0;
    *end = 0;
    method_end = strchr(request, ' ');
    if (!method_end) return 0;
    *method_end = 0;
    target = method_end + 1;
    target_end = strchr(target, ' ');
    if (!target_end) return 0;
    *target_end = 0;
    version = target_end + 1;
    if (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0")) return 0;
    if (strcmp(request, "GET")) {
        *status = 405;
        *reason = "unexpected HTTP method";
        return 0;
    }
    if (target[0] != '/' || (target[1] != '?' && target[1] != 0)) {
        *status = 404;
        *reason = "unexpected callback path";
        return 0;
    }
    for (ch = (const unsigned char *) target; *ch; ++ch)
        if (*ch <= 32 || *ch >= 127 || *ch == '#' || *ch == '\\') return 0;
    sprintf(host, "localhost:%u", port);
    line = end + 2;
    while (*line) {
        end = strstr(line, "\r\n");
        if (!end) return 0;
        if (end == line) break;
        *end = 0;
        colon = strchr(line, ':');
        if (!colon || colon == line) return 0;
        for (ch = (const unsigned char *) line; ch < (const unsigned char *) colon; ++ch)
            if (!((*ch >= 'a' && *ch <= 'z') || (*ch >= 'A' && *ch <= 'Z') || (*ch >= '0' && *ch <= '9') || strchr("!#$%&'*+-.^_`|~", *ch))) return 0;
        *colon = 0;
        value = colon + 1;
        while (*value == ' ' || *value == '\t')
            ++value;
        for (ch = (const unsigned char *) value; *ch; ++ch)
            if ((*ch < 32 && *ch != '\t') || *ch == 127) return 0;
        tail = value + strlen(value);
        while (tail > value && (tail[-1] == ' ' || tail[-1] == '\t'))
            *--tail = 0;
        if (maage_auth_ascii_equal(line, "Host")) {
            if (host_seen++ || !maage_auth_ascii_equal(value, host)) {
                *reason = "unexpected or duplicate HTTP Host";
                return 0;
            }
        } else if (maage_auth_ascii_equal(line, "Transfer-Encoding") || (maage_auth_ascii_equal(line, "Content-Length") && strcmp(value, "0"))) {
            *reason = "unexpected callback request body";
            return 0;
        }
        line = end + 2;
    }
    if (host_seen != 1) {
        *reason = "missing localhost Host";
        return 0;
    }
    if (*target == '/' && target[1] == '?') target += 2;
    else
        target += 1;
    size = strlen(target);
    if (size >= capacity) {
        *reason = "callback query exceeds size limit";
        return 0;
    }
    memcpy(query, target, size + 1);
    return 1;
}
maage_status maage_auth_receive(maage_auth_listener *listener, char *query, size_t capacity, const maage_auth_observer *observer) {
    char *request, *complete;
    const char *reason = "incomplete, oversized, or malformed HTTP request";
    size_t used = 0;
    int ready, count, status = 400, accepted = 0;
    double deadline;
    if (!listener || !query || !capacity || !observer || !observer->cancelled) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_READ, 0);
    query[0] = 0;
    ready = maage_auth_ready(listener->socket, 0, 100);
    if (!ready || (ready < 0 && maage_auth_socket_retry())) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
    if (ready < 0) return maage_auth_transport_failure(observer->trace, MAAGE_OPERATION_READ);
    listener->client = accept(listener->socket, NULL, NULL);
    if (listener->client == MAAGE_AUTH_INVALID) {
        if (maage_auth_socket_retry()) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
        return maage_auth_transport_failure(observer->trace, MAAGE_OPERATION_READ);
    }
    if (!maage_auth_socket_prepare(listener->client)) {
        maage_auth_client_close(listener);
        return maage_auth_transport_failure(observer->trace, MAAGE_OPERATION_READ);
    }
    request = (char *) calloc(1, MAAGE_AUTH_HTTP_LIMIT + 1);
    if (!request) {
        maage_auth_client_close(listener);
        return MAAGE_STATUS_TRACE(observer->trace, maage_status_make(MAAGE_STATUS_ALLOCATION_FAILED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_READ, 0), MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_READ);
    }
    deadline = maage_time_monotonic_seconds() + 2;
    while (used < MAAGE_AUTH_HTTP_LIMIT && !observer->cancelled(observer->context) && maage_time_monotonic_seconds() >= 0 && maage_time_monotonic_seconds() < deadline) {
        ready = maage_auth_ready(listener->client, 0, 100);
        if (ready < 0) {
            if (maage_auth_socket_retry()) continue;
            break;
        }
        if (!ready) continue;
        count = recv(listener->client, request + used, (int) (MAAGE_AUTH_HTTP_LIMIT - used), 0);
        if (count < 0 && maage_auth_socket_retry()) continue;
        if (count <= 0 || memchr(request + used, 0, (size_t) count)) break;
        used += (size_t) count;
        request[used] = 0;
        complete = strstr(request, "\r\n\r\n");
        if (complete) {
            if (complete + 4 == request + used) accepted = maage_auth_http_query(request, listener->port, query, capacity, &status, &reason);
            else
                reason = "unexpected callback request body";
            break;
        }
    }
    maage_crypto_clear(request, MAAGE_AUTH_HTTP_LIMIT + 1);
    free(request);
    if (observer->cancelled(observer->context)) {
        maage_auth_client_close(listener);
        return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_READ, 0);
    }
    if (!accepted) {
        maage_log_verbose("maage.launcher.access.callback", "Rejected loopback request: %s (HTTP %d); authorization remains pending.", reason, status);
        maage_auth_http_reply(listener, status);
        return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_READ, 0);
    }
    maage_log_info("maage.launcher.access.callback", "Received expected HTTP callback; validating OAuth state.");
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_READ, 0);
}
maage_status maage_auth_wait(double seconds, const maage_auth_observer *observer) {
    double start = maage_time_monotonic_seconds(), end;
    maage_status result;
    if (!observer || !observer->cancelled || !(seconds >= 0)) return maage_status_make(MAAGE_STATUS_INVALID_ARGUMENT, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
    if (start < 0) return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
    if (!seconds) return maage_status_make(MAAGE_STATUS_NOOP, 0, MAAGE_OPERATION_WAIT, 0);
    end = start + seconds;
    do {
        if (observer->cancelled(observer->context)) return maage_status_make(MAAGE_STATUS_INTERRUPTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
        if (maage_time_monotonic_seconds() < 0) return maage_status_make(MAAGE_STATUS_SYSTEM_NOT_SUPPORTED, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT, 0);
        result = maage_auth_sleep(100);
        if (maage_status_failed(result)) return MAAGE_STATUS_PROPAGATE(observer->trace, result, MAAGE_CONTEXT_AUTH, MAAGE_OPERATION_WAIT);
    } while (maage_time_monotonic_seconds() < end);
    return maage_status_make(MAAGE_STATUS_SUCCESS, 0, MAAGE_OPERATION_WAIT, 0);
}
