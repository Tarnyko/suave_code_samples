/*
* socket_udp_broadcast_client.c
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
 - Linux (older):       gcc -std=c11 -D_BSD_SOURCE ...
 - Windows:             gcc -std=c11 ... -lws2_32
*/


#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#  include <winsock2.h>
#  define ioctl(X,Y,Z)   ioctlsocket(X,Y,Z)
#  define SLEEP(X)       Sleep(X)
#  define SHUT_RDWR      SD_BOTH
#else
#  include <sys/socket.h>
#  include <sys/ioctl.h>
#  include <arpa/inet.h>
#  define SLEEP(X)       usleep(X*1000)
#  define SOCKET_ERROR   -1
#endif


static int                 _status = EXIT_SUCCESS;
static int                 _socket = SOCKET_ERROR;


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
    if (_socket != SOCKET_ERROR) {
        shutdown(_socket, SHUT_RDWR);
        close(_socket);
    }
    deinit_os_sockets();
    exit(_status);
}


void send_messages_to_network_interval(const int socket, const unsigned short port, const int interval)
{
    struct sockaddr_in network = {0};
    network = (struct sockaddr_in) { .sin_family      = AF_INET,
                                     .sin_addr.s_addr = htonl(INADDR_BROADCAST),
                                     .sin_port        = htons(port) };

    char msg_buffer[8] = "Hello!\n";

    printf("Broadcasting with interval of %d seconds...\n", interval);

    while (sendto(socket, msg_buffer, sizeof(msg_buffer), 0,
                  (struct sockaddr*)&network, sizeof(struct sockaddr)) != SOCKET_ERROR) {
        SLEEP(interval * 1000); }
}


int main (int argc, char *argv[])
{
    char* error = NULL;

    if (!init_os_sockets()) {
        error = "Could not initialize network sockets";
        goto err;
    }

    if ((_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) == SOCKET_ERROR) {
        error = "Could not create socket";
        goto err;
    }

    if (setsockopt(_socket, SOL_SOCKET, SO_BROADCAST,
          (void*) &(int){true}, sizeof(int)) == SOCKET_ERROR) {
        error = "Could not set socket to broadcast mode";
        goto err;
    }

    if (ioctl(_socket, FIONBIO, &(unsigned long){true})) {
        error = "Could not set socket as non-blocking";
        goto err;
    }

    signal(SIGINT, close_sockets);   // make sure we handle [Ctrl]+[C] properly

    send_messages_to_network_interval(_socket, 6001, 2);

    goto end;

    err:
      fprintf(stderr, "[ERROR] %s! Exiting...\n", error);
      _status = EXIT_FAILURE;

    end:
      close_sockets(0);

    return _status;
}
