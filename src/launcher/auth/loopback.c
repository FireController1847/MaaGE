#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#elif !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "internal.h"
#include "maage/time.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET maage_auth_socket;
#define MAAGE_AUTH_BAD_SOCKET INVALID_SOCKET
#define maage_auth_close closesocket
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
typedef int maage_auth_socket;
#define MAAGE_AUTH_BAD_SOCKET (-1)
#define maage_auth_close close
#endif
static int maage_auth_socket_ready(maage_auth_socket socket)
{
    fd_set set; struct timeval wait = {0, 100000};
    FD_ZERO(&set); FD_SET(socket, &set);
    return select((int)socket + 1, &set, NULL, NULL, &wait) > 0;
}
static int maage_auth_unhex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int maage_auth_query_value(const char *query, const char *name, char *output, size_t capacity)
{
    const char *end, *value, *cursor = query;
    size_t length = strlen(name), written;
    int found = 0, high, low, byte;
    output[0] = '\0';
    while (*cursor) {
        end = strchr(cursor, '&'); if (!end) end = cursor + strlen(cursor);
        if ((size_t)(end - cursor) > length && !strncmp(cursor, name, length) && cursor[length] == '=') {
            if (found) return -1;
            found = 1; written = 0; value = cursor + length + 1;
            while (value < end) {
                byte = (unsigned char)*value++;
                if (byte == '%') {
                    if (end - value < 2 || (high = maage_auth_unhex(value[0])) < 0 || (low = maage_auth_unhex(value[1])) < 0) return -1;
                    byte = high * 16 + low; value += 2;
                } else if (byte == '+') byte = ' ';
                if (byte <= 32 || byte >= 127 || written + 1 >= capacity) return -1;
                output[written++] = (char)byte;
            }
            output[written] = '\0';
        }
        cursor = *end ? end + 1 : end;
    }
    return found;
}
maage_auth_error maage_auth_loopback(maage_auth *auth, const char *state, const char *challenge, char *code, size_t capacity)
{
    struct sockaddr_in address;
    maage_auth_socket listener = MAAGE_AUTH_BAD_SOCKET, client;
    char request[8192], received_state[128], failure[128], host[128], expected_host[128], url[4096];
    char *redirect = NULL, *scope = NULL, *port_end, *line, *target_end, *query;
    const char *port_text, *path, *reply;
    unsigned long port;
    int count, valid, reuse = 1, code_found, error_found;
    size_t used;
    double deadline, request_deadline, now;
    maage_auth_error result = MAAGE_AUTH_CONFIGURATION;
#ifdef _WIN32
    WSADATA sockets;
    if (WSAStartup(MAKEWORD(2, 2), &sockets)) return result;
#endif
    port_text = !strncmp(auth->config.redirect, "http://127.0.0.1:", 17) ? auth->config.redirect + 17 :
        !strncmp(auth->config.redirect, "http://localhost:", 17) ? auth->config.redirect + 17 : NULL;
    if (!port_text || *port_text < '0' || *port_text > '9') goto cleanup;
    port = strtoul(port_text, &port_end, 10); path = port_end;
    if (port < 1024 || port > 65535 || strcmp(path, "/callback")) goto cleanup;
    memset(&address, 0, sizeof(address)); address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons((unsigned short)port);
    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener == MAAGE_AUTH_BAD_SOCKET) goto cleanup;
#ifdef _WIN32
    if (setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&reuse, sizeof(reuse))) goto cleanup;
#else
    if (listener >= FD_SETSIZE) goto cleanup;
    fcntl(listener, F_SETFD, FD_CLOEXEC);
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) || listen(listener, 4)) goto cleanup;
#ifdef _WIN32
    { u_long nonblocking = 1; if (ioctlsocket(listener, FIONBIO, &nonblocking)) goto cleanup; }
#else
    if (fcntl(listener, F_SETFL, O_NONBLOCK)) goto cleanup;
#endif
    sprintf(expected_host, "%.*s", (int)(path - auth->config.redirect - 7), auth->config.redirect + 7);
    redirect = maage_auth_escape(auth->config.redirect); scope = maage_auth_escape("XboxLive.SignIn XboxLive.offline_access");
    if (!redirect || !scope || strlen(auth->config.authorize) > 2000) goto cleanup;
    sprintf(url, "%s?client_id=%s&response_type=code&response_mode=query&redirect_uri=%s&scope=%s&state=%s&code_challenge=%s&code_challenge_method=S256&prompt=select_account",
        auth->config.authorize, auth->config.client_id, redirect, scope, state, challenge);
    maage_auth_lock(auth->runtime); strcpy(auth->browser_url, url); maage_auth_unlock(auth->runtime);
    now = maage_time_monotonic_seconds();
    if (now < 0) goto cleanup;
    deadline = now + 300;
    result = MAAGE_AUTH_CANCELLED;
    while (!maage_auth_cancelled(auth) && (now = maage_time_monotonic_seconds()) >= 0 && now < deadline) {
        if (!maage_auth_socket_ready(listener)) continue;
        client = accept(listener, NULL, NULL);
        if (client == MAAGE_AUTH_BAD_SOCKET) continue;
#ifndef _WIN32
        if (client >= FD_SETSIZE) { maage_auth_close(client); continue; }
        fcntl(client, F_SETFD, FD_CLOEXEC);
#ifdef __APPLE__
        setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &reuse, sizeof(reuse));
#endif
#endif
        used = 0; request[0] = '\0'; request_deadline = maage_time_monotonic_seconds() + 5;
        while (used < sizeof(request) - 1 && !maage_auth_cancelled(auth) && (now = maage_time_monotonic_seconds()) >= 0 && now < request_deadline) {
            if (!maage_auth_socket_ready(client)) continue;
            count = recv(client, request + used, (int)(sizeof(request) - 1 - used), 0);
            if (count <= 0) break;
            if (memchr(request + used, 0, (size_t)count)) break;
            used += (size_t)count; request[used] = '\0';
            if (strstr(request, "\r\n\r\n")) break;
        }
        valid = !strncmp(request, "GET /callback?", 14) && strstr(request, "\r\n\r\n") != NULL;
        host[0] = '\0';
        line = strstr(request, "\r\n");
        while (line && line[2] && line[2] != '\r') {
            line += 2;
            if (tolower((unsigned char)line[0]) == 'h' && tolower((unsigned char)line[1]) == 'o' &&
                tolower((unsigned char)line[2]) == 's' && tolower((unsigned char)line[3]) == 't' && line[4] == ':' && line[5] == ' ') {
                char *end = strstr(line, "\r\n");
                if (!end || host[0] || end - (line + 6) >= (int)sizeof(host)) { valid = 0; break; }
                memcpy(host, line + 6, (size_t)(end - line - 6)); host[end - line - 6] = '\0';
            }
            line = strstr(line, "\r\n");
        }
        if (strcmp(host, expected_host)) valid = 0;
        target_end = valid ? strchr(request + 4, ' ') : NULL;
        if (!target_end || (strncmp(target_end, " HTTP/1.1\r\n", 11) && strncmp(target_end, " HTTP/1.0\r\n", 11))) valid = 0;
        reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        if (valid) {
            *target_end = '\0'; query = request + 14;
            code_found = maage_auth_query_value(query, "code", code, capacity);
            error_found = maage_auth_query_value(query, "error", failure, sizeof(failure));
            if (maage_auth_query_value(query, "state", received_state, sizeof(received_state)) != 1 || strcmp(received_state, state)) result = MAAGE_AUTH_STATE;
            else if (code_found < 0 || error_found < 0 || (code_found && error_found)) result = MAAGE_AUTH_PROTOCOL;
            else if (error_found) result = !strcmp(failure, "access_denied") ? MAAGE_AUTH_CANCELLED : MAAGE_AUTH_REAUTH;
            else result = code_found == 1 && *code ? MAAGE_AUTH_OK : MAAGE_AUTH_PROTOCOL;
            reply = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nCache-Control: no-store\r\nContent-Security-Policy: default-src 'none'\r\nConnection: close\r\n\r\nReturn to MaaGE to see your sign-in result. You can close this browser tab.";
        }
#ifdef MSG_NOSIGNAL
        send(client, reply, (int)strlen(reply), MSG_NOSIGNAL);
#else
        send(client, reply, (int)strlen(reply), 0);
#endif
        maage_auth_close(client); maage_auth_wipe(request, sizeof(request));
        if (valid) break;
    }
cleanup:
    if (listener != MAAGE_AUTH_BAD_SOCKET) maage_auth_close(listener);
#ifdef _WIN32
    WSACleanup();
#endif
    maage_auth_free_secret(redirect); maage_auth_free_secret(scope);
    maage_auth_wipe(request, sizeof(request));
    return result;
}
