/*
* socket_tcp_client.c
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
 - Linux (glibc>=2.31): gcc -std=c11 -D_DEFAULT_SOURCE ...
 - Linux (older):       gcc -std=c11 -D_BSD_SOURCE -DHAVE_TIMESPEC_GET ...
 - Win32 (MinGW>=13):   gcc -std=c11 -D_UCRT ... -lws2_32 -lucrt
 - Win32 (older):       gcc -std=c11 ... -lws2_32
*/


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>   // for "timespec" [C11]

#ifdef _WIN32
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


static int                 _status = EXIT_SUCCESS;
static int                 _socket = SOCKET_ERROR;
static struct sockaddr_in* _server = NULL;


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
    if (_server) {
       free(_server);
    }
    if (_socket != SOCKET_ERROR) {
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
    }
    deinit_os_sockets();
    exit(_status);
}

static void handle_sigpipe(int)
{
    printf("\nServer died!\n\n");
    close_sockets(0);
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

        // need to recreate socket before retrying connect() above
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


void send_messages_to_server_interval(const int socket, const int interval)
{
    char msg_buffer[8] = "Hello!\n";

    printf("Sending with interval of %d seconds...\n", interval);

    while (send(socket, msg_buffer, sizeof(msg_buffer), 0) != SOCKET_ERROR) {
        SLEEP(interval * 1000); }

    printf("\nServer died!\n\n");
}


int main (int argc, char *argv[])
{
    char* error = NULL;

    if (argc < 2) {
        printf("Usage: %s <IP-or-hostname>\n\n", argv[0]);
        return EXIT_SUCCESS;
    }

    if (!init_os_sockets()) {
        error = "Could not initialize network sockets";
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

    send_messages_to_server_interval(_socket, 2);

    goto end;

    err:
      fprintf(stderr, "[ERROR] %s! Exiting...\n", error);
      _status = EXIT_FAILURE;

    end:
      close_sockets(0);

    return _status;
}
