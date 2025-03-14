/*
* wayland-03-shm_software_rendering.c
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

//  Prerequisites (Debian/Ubuntu):
// $ sudo apt install libwayland-dev

//  Compile with:
// - Weston:       $ gcc ... `pkg-config --cflags --libs wayland-client`
// - Weston/GNOME: $ gcc -DXDG_SHELL ... _deps/*.c `pkg-config --cflags --libs wayland-client`

#define _GNU_SOURCE      // for "asprintf()"
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Shared memory headers
#include <sys/mman.h>    // for "shm_open()"
#include <fcntl.h>       // for "O_CREAT","O_RDWR"

// Wayland headers
#include <wayland-client.h>
#ifdef XDG_SHELL
#  include "_deps/xdg-shell-unstable-v6-client-protocol.h"
#endif


// My prototypes

typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef enum {
    E_WL_SHELL = 0, E_XDG_SHELL = 1, E_GTK_SHELL = 2, E_PLASMA_SHELL = 3
} ShellId;


typedef struct {
    char*             shm_id;          // UNIX in-memory namespace..
    int               shm_fd;          // ... with file descriptor...
    void*             data;            // ... mem-mapped to a raw buffer

    struct wl_buffer* buffer;          // Wayland abstraction of the above
} Buffer;

typedef struct {
    Buffer              buffer;

    int width;
    int height;
    struct wl_surface*  surface;         // Wayland surface object...
    void*               shell_surface;   // ...handled by a window manager
} Window;


typedef struct {
    CompositorId          compositorId;
    struct wl_compositor* compositor;    // 'compositor': surface manager

    ShellId               shellId;
    void*                 shell;         // 'shell': window manager
    struct wl_shell*      wl_shell;
# ifdef XDG_SHELL
    struct zxdg_shell_v6* xdg_shell;
# endif

    struct wl_shm*        shm;           // 'shared mem': software renderer
} InterfaceInfo;


void* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char* arg);
void destroy_shell_surface(InterfaceInfo*, void*);

Window* create_window(InterfaceInfo*, char* title, int width, int height);
void destroy_window(InterfaceInfo*, Window*);


// Wayland predefined interfaces prototypes

void wl_interface_available(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
void wl_interface_removed(void*, struct wl_registry*, uint32_t);

static const struct wl_registry_listener wl_registry_listener = {
    wl_interface_available,
    wl_interface_removed
};


void wl_shell_surface_handle_ping(void*, struct wl_shell_surface*, uint32_t);

static const struct wl_shell_surface_listener wl_shell_surface_listener = {
    wl_shell_surface_handle_ping
};


#ifdef XDG_SHELL
void zxdg_shell_v6_handle_ping(void*, struct zxdg_shell_v6*, uint32_t);

static const struct zxdg_shell_v6_listener zxdg_shell_v6_listener = {
    zxdg_shell_v6_handle_ping
};

void zxdg_toplevel_v6_configure(void*, struct zxdg_toplevel_v6*,
		                int32_t, int32_t, struct wl_array*);
void zxdg_toplevel_v6_close(void*, struct zxdg_toplevel_v6*);

static const struct zxdg_toplevel_v6_listener zxdg_toplevel_v6_listener = {
    zxdg_toplevel_v6_configure,
    zxdg_toplevel_v6_close
};

void zxdg_surface_v6_configure(void*, struct zxdg_surface_v6*, uint32_t);

static const struct zxdg_surface_v6_listener zxdg_surface_v6_listener = {
    zxdg_surface_v6_configure
};
#endif


int main(int argc, char* argv[])
{
    struct wl_display* display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "No Wayland compositor found! Do you have a '$XDG_RUNTIME_DIR/wayland-0' socket?\n"
                        "If not, start it, and set environment variables:\n"
                        "$ export XDG_RUNTIME_DIR=/run/user/$UID\n"
                        "$ export WAYLAND_DISPLAY=wayland-0\n\n");
        return EXIT_FAILURE;
    }

    struct wl_registry* registry = wl_display_get_registry(display);
    assert(registry);

    // attach an asynchronous callback struct (=list) with an '_info' object itself attached
    InterfaceInfo _info = {0};
    wl_registry_add_listener(registry, &wl_registry_listener, &_info);

    // sync-wait for a compositor roundtrip, so all callbacks are fired (see 'WL_REGISTRY_CALLBACKS' below)
    wl_display_roundtrip(display);
    assert(_info.compositor);

    // now this should have been filled by the callbacks
    printf("Compositor is: ");
    switch (_info.compositorId)
    {
        case E_WESTON : printf("Weston.\n\n");     break;
        case E_GNOME  : printf("GNOME.\n\n");      break;
        case E_KDE    : printf("KDE Plasma.\n\n"); break;
        case E_WLROOTS: printf("wlroots.\n\n");    break;
        default       : printf("Unknown...\n\n");
    }

    char* shell_name;
    int loop_result, result = EXIT_SUCCESS;

    // no need to bother if shm (software rendering) is not available...
    if (!_info.shm) {
        fprintf(stderr, "No software rendering 'wl_shm' interface found! Exiting...\n");
        goto error;
    }

    // choose a shell/window manager in a most-to-less-compatible order
    if (!(shell_name = elect_shell(&_info))) {
        fprintf(stderr, "No compatible window manager/shell interface found! Exiting...\n");
        goto error;
    }
    printf("Shell/window manager: '%s'\n\n", shell_name);

    // MAIN LOOP!
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        printf("Looping...\n\n");

        while (loop_result != -1) {
            loop_result = wl_display_dispatch(display); }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
# ifdef XDG_SHELL
    if (_info.xdg_shell) {
        zxdg_shell_v6_destroy(_info.xdg_shell); }
# endif
    if (_info.wl_shell) {
        wl_shell_destroy(_info.wl_shell); }
    if (_info.shm) {
        wl_shm_destroy(_info.shm); }
    wl_compositor_destroy(_info.compositor);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return result;
}


void* elect_shell(InterfaceInfo* _info)
{
    // the most compatible... but not available under GNOME :-(
    if (_info->wl_shell) {
        _info->shellId = E_WL_SHELL;
        _info->shell   = _info->wl_shell;
        return "wl_shell";
# ifdef XDG_SHELL
    // available under both Weston and GNOME
    } else if (_info->xdg_shell) {
        _info->shellId = E_XDG_SHELL;
        _info->shell   = _info->xdg_shell;
	zxdg_shell_v6_add_listener(_info->xdg_shell, &zxdg_shell_v6_listener, NULL);
        return "zxdg_shell_v6";
# endif
    }
 
    return NULL;
}

void* create_shell_surface(InterfaceInfo* _info, struct wl_surface* surface, char* arg)
{
   switch(_info->shellId)
   {
     case E_WL_SHELL:
       struct wl_shell_surface* shell_surface = wl_shell_get_shell_surface((struct wl_shell*) _info->shell, surface);
       assert(shell_surface);
       wl_shell_surface_add_listener(shell_surface, &wl_shell_surface_listener, NULL);
       wl_shell_surface_set_title(shell_surface, arg);
       wl_shell_surface_set_toplevel(shell_surface);
       return shell_surface;
# ifdef XDG_SHELL
     case E_XDG_SHELL:
       struct zxdg_surface_v6* xdg_surface = zxdg_shell_v6_get_xdg_surface((struct zxdg_shell_v6*) _info->shell, surface);
       assert(xdg_surface);
       zxdg_surface_v6_add_listener(xdg_surface, &zxdg_surface_v6_listener, NULL);
       struct zxdg_toplevel_v6* xdg_toplevel = zxdg_surface_v6_get_toplevel(xdg_surface);
       assert(xdg_toplevel);
       zxdg_toplevel_v6_add_listener(xdg_toplevel, &zxdg_toplevel_v6_listener, NULL);
       zxdg_toplevel_v6_set_title(xdg_toplevel, arg);
       return xdg_surface;
# endif
     default:
       return NULL;
   }
}

void destroy_shell_surface(InterfaceInfo* _info, void* shell_surface)
{
    switch (_info->shellId)
    {
      case E_WL_SHELL:  return wl_shell_surface_destroy((struct wl_shell_surface*) shell_surface);
# ifdef XDG_SHELL
      case E_XDG_SHELL: return zxdg_surface_v6_destroy((struct zxdg_surface_v6*) shell_surface);
# endif
      default:
    }
}


Window* create_window(InterfaceInfo* _info, char* title, int width, int height)
{
    Window* window = calloc(1, sizeof(Window));
    Buffer* buffer = &window->buffer;

    window->width = width;
    window->height = height;

    window->surface = wl_compositor_create_surface(_info->compositor);
    assert(window->surface);
    
    window->shell_surface = create_shell_surface(_info, window->surface, title);
    assert(window->shell_surface);

    // [xdg-shell expects a commit before attaching any buffer (/!\)]
    wl_surface_commit(window->surface);

    // 1) create a POSIX in-memory object (the name must contain a single '/')
    char* slash = strrchr(title, '/');
    asprintf(&buffer->shm_id, "/%s", (slash ? slash + 1 : title));
    buffer->shm_fd = shm_open(buffer->shm_id, O_CREAT|O_RDWR, 0600);
    // allocate as much raw bytes as needed pixels inside
    assert(!ftruncate(buffer->shm_fd, width * height * 4));   // *4 = RGBA

    // 2) expose it as a void* buffer, so our code can use 'memset()' directly
    buffer->data = mmap(NULL, width * height * 4, PROT_READ|PROT_WRITE,
                        MAP_SHARED, buffer->shm_fd, 0);
    assert(buffer->data);

    // 3) pass the object to the compositor through its 'wl_shm_pool' interface,
    struct wl_shm_pool* shm_pool = wl_shm_create_pool(_info->shm, buffer->shm_fd,
                                                      width * height * 4);
    // and create the final 'wl_buffer' abstraction
    buffer->buffer = wl_shm_pool_create_buffer(shm_pool, 0, width, height,
		                               width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(shm_pool);

    // set the initial raw buffer content, only White pixels (0xFF)
    memset(buffer->data, 0xFF, width * height * 4);

    // 4) attach the 'wl_buffer' to the root 'wl_surface', and set it as damaged
    wl_surface_attach(window->surface, buffer->buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, width, height);
    wl_surface_commit(window->surface);

    return window;
}

void destroy_window(InterfaceInfo* _info, Window* window)
{
    Buffer* buffer = &window->buffer;

    wl_buffer_destroy(buffer->buffer);

    munmap(buffer->data, window->width * window->height * 4);

    close(buffer->shm_fd);
    shm_unlink(buffer->shm_id);
    free(buffer->shm_id);

    destroy_shell_surface(_info, window->shell_surface);

    wl_surface_destroy(window->surface);

    free(window);
}


// WAYLAND REGISTRY CALLBACKS

void wl_interface_available(void* data, struct wl_registry* registry, uint32_t serial, const char* name, uint32_t version)
{
    InterfaceInfo* _info = data;

    if (!strcmp(name, "wl_compositor"))         { _info->compositor = wl_registry_bind(registry, serial, &wl_compositor_interface, 1);
    } else if (!strcmp(name, "wl_shm"))         { _info->shm        = wl_registry_bind(registry, serial, &wl_shm_interface, 1);
    } else if (!strcmp(name, "wl_shell"))       { _info->wl_shell   = wl_registry_bind(registry, serial, &wl_shell_interface, 1);
# ifdef XDG_SHELL
    } else if (strstr(name, "xdg_shell"))       { _info->xdg_shell  = wl_registry_bind(registry, serial, &zxdg_shell_v6_interface, 1);
# endif
    } else if (strstr(name, "gtk_shell"))       { _info->compositorId = E_GNOME;
    } else if (strstr(name, "plasma_shell"))    { _info->compositorId = E_KDE;
    } else if (strstr(name, "wlr_layer_shell")) { _info->compositorId = E_WLROOTS;
    } else if (strstr(name, "weston"))          { _info->compositorId = E_WESTON; }
}

void wl_interface_removed(void* data, struct wl_registry* registry, uint32_t serial)
{ }


// WAYLAND SHELL CALLBACKS

void wl_shell_surface_handle_ping(void* data, struct wl_shell_surface* shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}


#ifdef XDG_SHELL
void zxdg_shell_v6_handle_ping(void* data, struct zxdg_shell_v6* xdg_shell, uint32_t serial)
{
    zxdg_shell_v6_pong(xdg_shell, serial);
}

void zxdg_toplevel_v6_close(void* data, struct zxdg_toplevel_v6* xdg_toplevel)
{ }

void zxdg_toplevel_v6_configure(void* data, struct zxdg_toplevel_v6* xdg_toplevel,
                                int32_t width, int32_t height, struct wl_array* states)
{ }

void zxdg_surface_v6_configure(void* data, struct zxdg_surface_v6* xdg_surface, uint32_t serial)
{
    zxdg_surface_v6_ack_configure(xdg_surface, serial);
}
#endif

