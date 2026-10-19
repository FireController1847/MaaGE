#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "oauth.h"
#include "maage/time.h"
#include <stdlib.h>
#include <string.h>
#include <openssl/crypto.h>
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
typedef int maage_auth_socket;
#define MAAGE_AUTH_INVALID (-1)
#define maage_auth_socket_close close
#endif
struct maage_auth_listener { maage_auth_socket socket; };
static int maage_auth_network(void)
{
#ifdef _WIN32
    WSADATA data;
    return WSAStartup(MAKEWORD(2,2), &data) == 0;
#else
    return 1;
#endif
}
static void maage_auth_network_done(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}
static maage_auth_socket maage_auth_socket_create(void)
{
    maage_auth_socket s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s != MAAGE_AUTH_INVALID) SetHandleInformation((HANDLE)s, HANDLE_FLAG_INHERIT, 0);
#else
    if (s >= FD_SETSIZE) { close(s); return MAAGE_AUTH_INVALID; }
    if (s != MAAGE_AUTH_INVALID) fcntl(s, F_SETFD, FD_CLOEXEC);
#ifdef SO_NOSIGPIPE
    if (s != MAAGE_AUTH_INVALID) { int enabled = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)); }
#endif
#endif
    return s;
}
static int maage_auth_ready(maage_auth_socket s, int write_ready, unsigned ms)
{
    fd_set set;
    struct timeval timeout;
    FD_ZERO(&set); FD_SET(s, &set); timeout.tv_sec = ms / 1000; timeout.tv_usec = (ms % 1000) * 1000;
    return select((int)s + 1, write_ready ? NULL : &set, write_ready ? &set : NULL, NULL, &timeout) > 0;
}
static int maage_auth_connect_local(maage_auth_socket s, struct sockaddr_in *address)
{
    int result, error = 0;
#ifdef _WIN32
    u_long nonblocking = 1;
    int size = sizeof(error);
    if (ioctlsocket(s, FIONBIO, &nonblocking)) return 0;
#else
    socklen_t size = sizeof(error);
    int flags = fcntl(s, F_GETFL, 0);
    if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK)) return 0;
#endif
    result = connect(s, (struct sockaddr *)address, sizeof(*address));
    if (result && !maage_auth_ready(s, 1, 200)) return 0;
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (void *)&error, &size) || error) return 0;
    return 1;
}
maage_auth_listener *maage_auth_listen(unsigned *port)
{
    struct sockaddr_in address;
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    maage_auth_listener *listener;
    if (!maage_auth_network()) return NULL;
    listener = (maage_auth_listener *)malloc(sizeof(*listener));
    if (!listener) { maage_auth_network_done(); return NULL; }
    listener->socket = maage_auth_socket_create();
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(0x7f000001UL);
    if (listener->socket == MAAGE_AUTH_INVALID || bind(listener->socket, (struct sockaddr *)&address, sizeof(address)) ||
        listen(listener->socket, 4) || getsockname(listener->socket, (struct sockaddr *)&address, &length)) {
        maage_auth_close(listener); return NULL;
    }
    *port = ntohs(address.sin_port); return listener;
}
void maage_auth_close(maage_auth_listener *listener)
{
    if (!listener) return;
    if (listener->socket != MAAGE_AUTH_INVALID) maage_auth_socket_close(listener->socket);
    free(listener); maage_auth_network_done();
}
int maage_auth_receive(maage_auth_listener *listener, char *uri, size_t capacity)
{
    maage_auth_socket client;
    size_t used = 0;
    int count, complete = 0;
    double deadline;
    if (!maage_auth_ready(listener->socket, 0, 100)) return 0;
    client = accept(listener->socket, NULL, NULL); if (client == MAAGE_AUTH_INVALID) return 0;
#ifndef _WIN32
    if (client >= FD_SETSIZE) { close(client); return 0; }
#endif
    deadline = maage_time_monotonic_seconds() + 1;
    while (used + 1 < capacity && maage_time_monotonic_seconds() < deadline && maage_auth_ready(client, 0, 100)) {
        count = recv(client, uri + used, (int)(capacity - 1 - used), 0);
        if (count <= 0) { complete = count == 0; break; }
        if (memchr(uri + used, 0, (size_t)count)) break;
        used += (size_t)count;
    }
    maage_auth_socket_close(client); uri[used] = 0;
    if (!complete) { OPENSSL_cleanse(uri, capacity); return 0; }
    return used != 0;
}
/* Strict callback parsing lives in oauth.c. This helper only routes activation;
 * possession of a port never grants authorization. No URI or code is logged. */
int maage_oauth_forward_url(const char *uri)
{
    const char *state, *end;
    char *parsed;
    unsigned long port;
    struct sockaddr_in address;
    maage_auth_socket s;
    size_t size, sent = 0;
    int count, ok = 0;
    if (strncmp(uri, MAAGE_MICROSOFT_REDIRECT "?", sizeof(MAAGE_MICROSOFT_REDIRECT))) return 0;
    size = strlen(uri); if (size > 32768 || strchr(uri, '#')) return 0;
    state = strstr(uri, "?state="); if (!state) state = strstr(uri, "&state=");
    if (!state) return 0;
    state += 7; port = strtoul(state, &parsed, 10); end = parsed;
    if (!port || port > 65535 || *end != '.' || end - state > 5 || strlen(end + 1) < 64) return 0;
    if (!maage_auth_network()) return 0;
    s = maage_auth_socket_create();
    if (s == MAAGE_AUTH_INVALID) { maage_auth_network_done(); return 0; }
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(0x7f000001UL); address.sin_port = htons((unsigned short)port);
    /* A loopback-only connect fails promptly when no login is pending. */
    if (maage_auth_connect_local(s, &address)) {
        while (sent < size && maage_auth_ready(s, 1, 100)) {
#ifdef MSG_NOSIGNAL
            count = send(s, uri + sent, (int)(size - sent), MSG_NOSIGNAL);
#else
            count = send(s, uri + sent, (int)(size - sent), 0);
#endif
            if (count <= 0) break;
            sent += (size_t)count;
        }
        ok = sent == size;
    }
    maage_auth_socket_close(s); maage_auth_network_done(); return ok;
}
int maage_auth_wait(double seconds, const maage_auth_observer *observer)
{
    double start = maage_time_monotonic_seconds(), end;
    if (start < 0) return 0;
    end = start + seconds;
    do {
        if (observer->cancelled(observer->context) || maage_time_monotonic_seconds() < 0) return 0;
        maage_auth_sleep(100);
    } while (maage_time_monotonic_seconds() < end);
    return 1;
}
