/*
* socket_tcp_keepalive_server.c
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
 - Windows (MinGW>=14): gcc -std=c11 -D_UCRT ... -lws2_32 -lucrt
 - Windows (older):     gcc -std=c11 ... -lws2_32
*/


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>   // for "timespec" [C11]

#ifdef _WIN32
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


static int                 _status = EXIT_SUCCESS;
static int                 _socket = SOCKET_ERROR;
static struct sockaddr_in* _client = NULL;


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
    if (_client) {
       free(_client);
    }
    if (_socket != SOCKET_ERROR) {
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
    }
    deinit_os_sockets();
    exit(_status);
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


void display_client_queue_timeout(const int queue, const int timeout)
{
    ssize_t msg_size = 1;
    char msg_buffer[16] = {0};

    struct timespec prev_time, curr_time = {0};
    timespec_get(&prev_time, TIME_UTC);

    while (msg_size != 0) {
        msg_size = recv(queue, msg_buffer, sizeof(msg_buffer), 0);
        if (msg_size > 0) {
            printf("- Read %zd bytes: %s", msg_size, msg_buffer);
            fflush(stdout);
            memset(msg_buffer, 0, sizeof(msg_buffer));
            continue;
        }

        // nothing received...
        // "keepalive": after timeout, send a reply to check client is alive
        timespec_get(&curr_time, TIME_UTC);
        if (curr_time.tv_sec - prev_time.tv_sec > timeout) {
            if (send(queue, &(const char){0}, 1, 0) == SOCKET_ERROR) {
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

    if (!init_os_sockets()) {
        error = "Could not initialize network sockets";
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

    signal(SIGINT, close_sockets);   // make sure we handle [Ctrl]+[C] properly

    display_client(_client);

    display_client_queue_timeout(queue, 5);

    goto end;

    err:
      fprintf(stderr, "[ERROR] %s! Exiting...\n", error);
      _status = EXIT_FAILURE;

    end:
      close_sockets(0);

    return _status;
}
