/*
* socket_tcp_client_tls.c
* Copyright (C) 2025  Manuel Bachmann <tarnyko.tarnyko.net>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*  Compile with:
 - Linux (glibc>=2.31): gcc -std=c11 -D_DEFAULT_SOURCE ... `pkg-config --cflags --libs openssl`
 - Linux (older):       gcc -std=c11 -D_BSD_SOURCE -DHAVE_TIMESPEC_GET ... `pkg-config --cflags --libs openssl`
 - Windows (MinGW>=14): gcc -std=c11 -D_UCRT ...  `pkg-config --cflags --libs openssl` -lws2_32 -lucrt
 - Windows (older):     gcc -std=c11 ... `pkg-config --cflags --libs openssl` -lws2_32
*/


#ifdef __unix__
#  define _GNU_SOURCE // for "asprintf()"
#endif
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>   // for "timespec" [C11]


#ifdef _WIN32
#  include "_deps/asprintf.h"
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define ioctl(X,Y,Z)   ioctlsocket(X,Y,Z)
#  define SLEEP(X)       Sleep(X)
#  define SHUT_RDWR      SD_BOTH
#else
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  define SLEEP(X)       usleep(X*1000)
#  define SOCKET_ERROR   -1
#endif


#include <openssl/ssl.h>
#include <openssl/err.h>

#if OPENSSL_VERSION_MAJOR < 3
#  error "We need OpenSSL version 3.x APIs!"
#endif


static int                 _status = EXIT_SUCCESS;
static int                 _socket = SOCKET_ERROR;
static struct sockaddr_in* _server = NULL;
static SSL_CTX*            _ctx    = NULL;
static SSL*                _ssl    = NULL;


bool init_os_sockets()
{
# ifdef _WIN32
    return !WSAStartup(MAKEWORD(2,0), &(WSADATA){});
# else
    return true;
# endif
}

void deinit_os_sockets()
{
# ifdef _WIN32
    WSACleanup();
# endif
}


static void close_sockets(int)
{
    if (_ssl) {
        SSL_free(_ssl);
    }
    if (_socket != SOCKET_ERROR) {
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
    }
    if (_server) {
       free(_server);
    }
    if (_ctx) {
        SSL_CTX_free(_ctx);
    }
    deinit_os_sockets();
    exit(_status);
}

static void handle_sigpipe(int)
{
    printf("\nServer died!\n\n");
    close_sockets(0);
}


bool init_openssl_keynames(const char* arg, char** pubkey, char** privkey)
{
# ifdef _WIN32
    if ((asprintf(pubkey, "%.*s-public.pem", strrchr(arg,'.')-arg, arg) == -1) ||
        (asprintf(privkey, "%.*s-private.pem", strrchr(arg,'.')-arg, arg) == -1)) {
# else
    if ((asprintf(pubkey, "%s-public.pem", arg) == -1) ||
        (asprintf(privkey, "%s-private.pem", arg) == -1)) {
# endif
        return false; }

    return true;
}

SSL_CTX* init_openssl(const char* arg)
{
    SSL_CTX* ctx    = NULL;
    char* error_ssl = NULL;
    char* pubkey = NULL, * privkey = NULL;

    if (!OPENSSL_init_ssl(0, NULL)) {
        return NULL; }
    if (!(ctx = SSL_CTX_new(TLS_method()))) {
        return NULL; }
    if (!SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION)) {
        error_ssl = "Could not set TLS version to 1.3";
        goto err_ssl;
    }
    if (!SSL_CTX_set_ciphersuites(ctx, "TLS_CHACHA20_POLY1305_SHA256")) {
        error_ssl = "Could not set TLS ciphersuite to 'TLS_CHACHA20_POLY1305_SHA256'";
        goto err_ssl;
    }
    if (!init_openssl_keynames(arg, &pubkey, &privkey)) {
        error_ssl = "Could not deduce SSL key filenames";
        goto err_ssl;
    }
    if (SSL_CTX_use_certificate_file(ctx, pubkey, SSL_FILETYPE_PEM) <= 0) {
        error_ssl = "Could not use public key";
        goto err_ssl;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, privkey, SSL_FILETYPE_PEM) <= 0) {
        error_ssl = "Could not use private key";
        goto err_ssl;
    }
    if (!SSL_CTX_check_private_key(ctx)) {
        error_ssl = "Could not check private key against public key";
        goto err_ssl;
    }
    goto end_ssl;

    err_ssl:
      fprintf(stderr, "[SSL_ERROR] %s!\n", error_ssl);
      SSL_CTX_free(ctx);
      ctx = NULL;

    end_ssl:
      if (pubkey) {
          free(pubkey); }
      if (privkey) {
          free(privkey); }
      return ctx;
}


struct sockaddr_in* resolve_server(const char* address)
{
    struct sockaddr_in* server = NULL;

    struct addrinfo* info   = NULL;
    struct addrinfo  hints  = {0};
    hints = (struct addrinfo) { .ai_family   = AF_UNSPEC,
                                .ai_socktype = SOCK_STREAM,
                                .ai_protocol = IPPROTO_TCP };

    if (getaddrinfo(address, "6000", &hints, &info) != 0) {
        return NULL; }

    for (struct addrinfo* i = info; i != NULL; i = i->ai_next) {
        if (i->ai_addr->sa_family == AF_INET) {
            server = calloc(1, sizeof(struct sockaddr_in));
            memcpy(server, i->ai_addr, sizeof(struct sockaddr_in));
        }
    }

    freeaddrinfo(info);

    return server;
}


void display_server(const char* address, const struct sockaddr_in* server)
{
     printf("Successfully resolved '%s' to '%s'.\n",
            address, inet_ntoa(server->sin_addr));
}


bool connect_to_server_timeout(int* sock, const struct sockaddr_in* server, const int timeout)
{
    int*             curr_sock   = sock;
    struct sockaddr* serv        = (struct sockaddr*) server;

    fd_set           sel_fds;
    struct timeval   sel_timeout = {0};

    struct timespec curr_time    = {0};
    struct timespec prev_time;
    timespec_get(&prev_time, TIME_UTC);

    while (true)
    {
        if (connect(*curr_sock, serv, sizeof(struct sockaddr)) != SOCKET_ERROR) {
            break; }

#   ifdef _WIN32
        errno = (WSAGetLastError() == WSAEWOULDBLOCK) ? EINPROGRESS : ECONNREFUSED;
#   endif

        if (errno == EINPROGRESS)
        {
            FD_ZERO(&sel_fds);
            FD_SET(*curr_sock, &sel_fds);

            if (select(1, NULL, &sel_fds, NULL, &sel_timeout)
                  && FD_ISSET(*curr_sock, &sel_fds)) {
                break; }   // Win32 succeeds here
#     ifdef __unix__
            if (connect(*curr_sock, serv, sizeof(struct sockaddr)) != SOCKET_ERROR) {
                break; }   // UNIX succeeds here
#     endif
        }

        // need to recreate socket before retrying connect()
        shutdown(*curr_sock, SHUT_RDWR);
        close(*curr_sock);
        *curr_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
        ioctl(*curr_sock, FIONBIO, &(unsigned long){true});

        timespec_get(&curr_time, TIME_UTC);
        if (curr_time.tv_sec - prev_time.tv_sec > timeout) {
            return false; }

        SLEEP(250);
    }

    sock = curr_sock;
    return true;
}

SSL* connect_to_ssl_server_timeout(const int sock, const SSL_CTX* ctx, const int timeout)
{
    SSL* ssl = SSL_new((SSL_CTX*)ctx);
    SSL_set_fd(ssl, sock);

    struct timespec prev_time, curr_time = {0};
    timespec_get(&prev_time, TIME_UTC);

    int res = SOCKET_ERROR;

    while (res == SOCKET_ERROR) {
        res = SSL_connect(ssl);
        if (res != SOCKET_ERROR) {
            break; }

        if (SSL_get_error(ssl, res) != SSL_ERROR_WANT_READ) {
            SSL_free(ssl);
            return NULL; }

        timespec_get(&curr_time, TIME_UTC);
        if (curr_time.tv_sec - prev_time.tv_sec > timeout) {
            SSL_free(ssl);
            return NULL; }

        SLEEP(1000);
    }

    return ssl;
}


void send_messages_to_ssl_server_interval(const SSL* ssl, const int interval)
{
    char msg_buffer[8] = "Hello!\n";

    printf("Sending with interval of %d seconds...\n", interval);

    while (SSL_write((SSL*)ssl, msg_buffer, sizeof(msg_buffer)) != SOCKET_ERROR) {
        SLEEP(interval * 1000); }

    printf("\nServer died!\n\n");
}


int main (int argc, char *argv[])
{
    char* error = NULL;

    printf("OpenSSL version: %d.%d.%d\n", OPENSSL_VERSION_MAJOR,
                                          OPENSSL_VERSION_MINOR,
                                          OPENSSL_VERSION_PATCH);

    if (argc < 2) {
        printf("Usage: %s <IP-or-hostname>\n\n", argv[0]);
        return EXIT_SUCCESS;
    }

    if (!init_os_sockets()) {
        error = "Could not initialize network sockets";
        goto err;
    }

    if (!(_ctx = init_openssl(argv[0]))) {
        error = "Could not initialize OpenSSL";
        goto err;
    }

    if (!(_server = resolve_server(argv[1]))) {
        error = "Could not resolve destination address";
        goto err;
    }

    display_server(argv[1], _server);

    if ((_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
        error = "Could not create socket";
        goto err;
    }

    if (ioctl(_socket, FIONBIO, &(unsigned long){true})) {
        error = "Could not set socket as non-blocking";
        goto err;
    }

    if (!connect_to_server_timeout(&_socket, _server, 5)) {
        error = "Could not connect to destination (port not open?)";
        goto err;
    }

# ifdef __unix__
    signal(SIGPIPE, handle_sigpipe); // don't abort(), but act upon server death
# endif
    signal(SIGINT, close_sockets);   // make sure we handle [Ctrl]+[C] properly

    if (!(_ssl = connect_to_ssl_server_timeout(_socket, _ctx, 5))) {
        error = "Could not open TLS communucation to destination (invalid key?)";
        goto err;
    }

    send_messages_to_ssl_server_interval(_ssl, 2);

    goto end;

    err:
      fprintf(stderr, "[ERROR] %s! Exiting...\n", error);
      _status = EXIT_FAILURE;

    end:
      close_sockets(0);

    return _status;
}
