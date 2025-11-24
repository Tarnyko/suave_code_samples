/*
* socket_tcp_keepalive_server_tls.c
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
 - Windows (MinGW>=14): gcc -std=c11 -D_UCRT ... `pkg-config --cflags --libs openssl` -lws2_32 -lucrt
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
#include <time.h>     // for "timespec" [C11]


#ifdef _WIN32
#  include "_deps/asprintf.h"
#  include <winsock2.h>
#  define ioctl(X,Y,Z)   ioctlsocket(X,Y,Z)
#  define SLEEP(X)       Sleep(X)
#  define SHUT_RDWR      SD_BOTH
#  define socklen_t      int
#else
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  define SLEEP(X)       usleep(X*1000)
#  define SOCKET_ERROR   -1
#endif


#include <openssl/ssl.h>
#include <openssl/err.h>

#if OPENSSL_VERSION_MAJOR < 3
#  error "We need OpenSSL version 3.x APIs!"
#endif

#ifndef SHOW_RAW_MESSAGES
#  define SHOW_RAW_MESSAGES false
#endif


static int                 _status = EXIT_SUCCESS;
static int                 _socket = SOCKET_ERROR;
static struct sockaddr_in* _client = NULL;
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
        SSL_shutdown(_ssl);
        SSL_free(_ssl);
    }
    if (_client) {
        free(_client);
    }
    if (_socket != SOCKET_ERROR) {
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
    }
    if (_ctx) {
        SSL_CTX_free(_ctx);
    }
    deinit_os_sockets();
    exit(_status);
}

static void handle_sigpipe(int)
{
    printf("\nClient died!\n\n");
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


bool bind_socket_to(const int socket, const unsigned short port)
{
    struct sockaddr_in server = {0};
    server = (struct sockaddr_in) { .sin_family      = AF_INET,
                                    .sin_addr.s_addr = htonl(INADDR_ANY),
                                    .sin_port        = htons(port) };

    return bind(socket, (struct sockaddr*)&server, sizeof(struct sockaddr)) != SOCKET_ERROR;
}


struct sockaddr_in* wait_for_client(const int socket, int* queue)
{
    struct sockaddr_in* client = calloc(1, sizeof(struct sockaddr_in));

    while (*queue == SOCKET_ERROR) {
        *queue = accept(socket, (struct sockaddr*)client, &(socklen_t){sizeof(struct sockaddr)});
        SLEEP(1000);
    }
    return client;
}


void display_client(const struct sockaddr_in* client)
{
    printf("Client connected! (IP: %s, port: %hu)\n",
           inet_ntoa(client->sin_addr), ntohs(client->sin_port));
}


SSL* wait_for_ssl_client_timeout(const int queue,  const int timeout, const SSL_CTX* ctx, int *ssl_queue)
{
    SSL* ssl = SSL_new((SSL_CTX*) ctx);
    SSL_set_fd(ssl, queue);

    struct timespec prev_time, curr_time = {0};
    timespec_get(&prev_time, TIME_UTC);

    while (*ssl_queue == SOCKET_ERROR) {
        *ssl_queue = SSL_accept(ssl);

        timespec_get(&curr_time, TIME_UTC);
        if (curr_time.tv_sec - prev_time.tv_sec > timeout) {
            SSL_free(ssl);
            return NULL; }

        SLEEP(1000);
    }

    return ssl;
}


void display_ssl_client(const SSL* ssl)
{
    printf("Ciphers supported by client: ");

    const STACK_OF(SSL_CIPHER)* ciphers = SSL_get1_supported_ciphers((SSL*) ssl);
    for (size_t c = 0; c < sk_SSL_CIPHER_num(ciphers); c++) {
        printf("%s ", SSL_CIPHER_get_name(sk_SSL_CIPHER_value(ciphers, c))); }

    printf("\n\n");
}


void display_ssl_client_queue_timeout(const SSL* ssl, const int queue, const int timeout)
{
    ssize_t msg_size = 1;
    char msg_buffer[16] = {0};

    struct timespec prev_time, curr_time = {0};
    timespec_get(&prev_time, TIME_UTC);

    while (msg_size != 0) {
        msg_size = SHOW_RAW_MESSAGES
                     ? recv(queue, msg_buffer, sizeof(msg_buffer), 0)
                     : SSL_read((SSL*)ssl, msg_buffer, sizeof(msg_buffer));
        if (msg_size > 0) {
            printf(" - Read %zd bytes (%s): %s", msg_size,
                   SHOW_RAW_MESSAGES ? "raw" : "decrypted", msg_buffer);
            fflush(stdout);
            memset(msg_buffer, 0, sizeof(msg_buffer));
            continue;
        }

        // nothing received...
        // "keepalive": after timeout, send a reply to check client is alive

        timespec_get(&curr_time, TIME_UTC);
        if (curr_time.tv_sec - prev_time.tv_sec > timeout) {
            if (SSL_write((SSL*)ssl, &(const char){0}, 1) == SOCKET_ERROR) {
                    printf("\nClient died!\n\n");
                    return;
            }
            timespec_get(&prev_time, TIME_UTC);
        }

        SLEEP(500);
    }

    printf("\nClient disconnected.\n\n");
}


int main (int argc, char *argv[])
{
    char* error = NULL;

    printf("OpenSSL version: %d.%d.%d\n", OPENSSL_VERSION_MAJOR,
                                          OPENSSL_VERSION_MINOR,
                                          OPENSSL_VERSION_PATCH);

    if (!init_os_sockets()) {
        error = "Could not initialize network sockets";
        goto err;
    }

    if (!(_ctx = init_openssl(argv[0]))) {
        error = "Could not initialize OpenSSL";
        goto err;
    }

    if ((_socket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) == SOCKET_ERROR) {
        error = "Could not create socket";
        goto err;
    }

    if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR,
          (void*) &(int){true}, sizeof(int)) == SOCKET_ERROR) {
        error = "Could not set socket as reuseable (to avoid zombie port)";
        goto err;
    }

    if (ioctl(_socket, FIONBIO, &(unsigned long){true})) {
        error = "Could not set socket as non-blocking";
        goto err;
    }

    if (!bind_socket_to(_socket, 6000)) {
        error = "Could not bind socket to IP/port";
        goto err;
    }

    puts("Listening on TCP port 6000... waiting for client (press [Ctrl-C] to stop).");

    int queue = SOCKET_ERROR;
    listen(_socket, 0);
    _client = wait_for_client(_socket, &queue);

# ifdef __unix__
    signal(SIGPIPE, handle_sigpipe); // don't abort(), but act upon client death
# endif
    signal(SIGINT, close_sockets);   // make sure we handle [Ctrl]+[C] properly

    display_client(_client);

    int ssl_queue = SOCKET_ERROR;
    if (!(_ssl = wait_for_ssl_client_timeout(queue, 3, _ctx, &ssl_queue))) {
        error = "Client cannot establish TLS communication";
        goto err;
    }

    display_ssl_client(_ssl);

    display_ssl_client_queue_timeout(_ssl, queue, 5);

    goto end;

    err:
      fprintf(stderr, "[ERROR] %s! Exiting...\n", error);
      _status = EXIT_FAILURE;

    end:
      close_sockets(0);

    return _status;
}
