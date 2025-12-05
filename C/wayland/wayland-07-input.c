/*
* wayland-07-input.c
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

/*  Prerequisites:
 - Debian/Ubuntu: $ sudo apt install libwayland-dev
 - Fedora/RHEL:   $ sudo dnf install wayland-devel

    Compile with:
 $ gcc ... `pkg-config --cflags --libs wayland-client`
*/

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
#include "_deps/xdg-wm-base-client-protocol.h"


char *os_button_code_to_string(uint32_t code)
{
    switch (code)
    {
# ifdef __linux__
      // (see "<linux/input-event-codes.h>")
      case 0x110: return "Left";
      case 0x111: return "Right";
      case 0x112: return "Middle";
# endif
      default: return "Other";
    }
}



// My prototypes

typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef enum {
    E_WL_SHELL = 0, E_XDG_WM_BASE = 1
} ShellId;


typedef struct {
    char*             shm_id;          // UNIX shared memory namespace...
    int               shm_fd;          // ...whose file descriptor is...
    void*             data;            // ...mem-mapped to a Raw buffer.
    struct wl_buffer* buffer;          // Wayland abstraction of the above.
} Buffer;

typedef struct {
    Buffer              buffer;          // Raw/Wayland buffer mapped to...
    struct wl_surface*  surface;         // ...a Wayland surface object...
    void*               shell_surface;   // ...handled by a window manager.

    int                 width;
    int                 height;
} Window;


typedef struct {
    struct wl_display*    display;       // 'display': root object

    CompositorId          compositorId;
    struct wl_compositor* compositor;    // 'compositor': surface manager

    ShellId               shellId;
    void*                 shell;         // 'shell': window manager
    struct wl_shell*      wl_shell;      //  (among: - deprecated
    struct xdg_wm_base*   xdg_wm_base;   //          - current stable)

    struct wl_shm*        shm;           // 'shared mem': software renderer

    struct wl_seat*       seat;          // 'seat': input devices
    struct wl_keyboard*   keyboard;      //  (among: - keyboard
    struct wl_pointer*    pointer;       //          - mouse
    struct wl_touch*      touch;         //          - touchscreen)
} InterfaceInfo;


char* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char*);
void destroy_shell_surface(InterfaceInfo*, void*);

Window* create_window(InterfaceInfo*, char*, int, int);
void destroy_window(InterfaceInfo*, Window*);


// Wayland predefined interfaces prototypes

void wl_interface_available(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
void wl_interface_removed(void*, struct wl_registry*, uint32_t);

static const struct wl_registry_listener wl_registry_listener = {
    wl_interface_available,
    wl_interface_removed
};


void wl_seat_handle_capabilities(void*, struct wl_seat*, enum wl_seat_capability);

static const struct wl_seat_listener wl_seat_listener = {
    wl_seat_handle_capabilities
};

void wl_pointer_handle_enter(void*, struct wl_pointer*, uint32_t, struct wl_surface*, wl_fixed_t, wl_fixed_t);
void wl_pointer_handle_leave(void*, struct wl_pointer*, uint32_t, struct wl_surface*);
void wl_pointer_handle_motion(void*, struct wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t);
void wl_pointer_handle_button(void*, struct wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t);

static const struct wl_pointer_listener wl_pointer_listener = {
    wl_pointer_handle_enter,
    wl_pointer_handle_leave,
    wl_pointer_handle_motion,
    wl_pointer_handle_button
};


void wl_shell_surface_handle_ping(void*, struct wl_shell_surface*, uint32_t);

static const struct wl_shell_surface_listener wl_shell_surface_listener = {
    wl_shell_surface_handle_ping
};


void xdg_wm_base_handle_ping(void*, struct xdg_wm_base*, uint32_t);

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_handle_ping
};

void xdg_surface_configure(void*, struct xdg_surface*, uint32_t);

static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure
};

void xdg_toplevel_configure(void*, struct xdg_toplevel*,
                            int32_t, int32_t, struct wl_array*);
void xdg_toplevel_close(void*, struct xdg_toplevel*);

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    xdg_toplevel_configure,
    xdg_toplevel_close
};


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
    _info.display = display;
    wl_registry_add_listener(registry, &wl_registry_listener, &_info);

    // sync-wait for a compositor roundtrip, so all callbacks are fired (see 'WL_REGISTRY_CALLBACKS' below)
    wl_display_roundtrip(display);
    assert(_info.compositor);

    // now this should have been filled by the registry callbacks
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

    // look for the mouse, and warn if it is missing
    if (!_info.seat) {
        fprintf(stderr, "No input 'wl_seat' interface found! The example will run, but lack purpose.\n");
    } else {
        wl_seat_add_listener(_info.seat, &wl_seat_listener, &_info);
        wl_display_roundtrip(display);

        if(!_info.pointer) {
            fprintf(stderr, "No mouse found! The example will run, but lack purpose.\n");
        } else {
            wl_pointer_add_listener(_info.pointer, &wl_pointer_listener, NULL); } }

    // MAIN LOOP!
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        printf("\nLooping...\n\n");

        while (loop_result != -1) {
            loop_result = wl_display_dispatch(display); }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
    if (_info.xdg_wm_base) {
        xdg_wm_base_destroy(_info.xdg_wm_base); }
    if (_info.wl_shell) {
        wl_shell_destroy(_info.wl_shell); }
    if (_info.touch) {
        wl_touch_destroy(_info.touch); }
    if (_info.pointer) {
        wl_pointer_destroy(_info.pointer); }
    if (_info.keyboard) {
        wl_keyboard_destroy(_info.keyboard); }
    if (_info.seat) {
        wl_seat_destroy(_info.seat); }
    if (_info.shm) {
        wl_shm_destroy(_info.shm); }
    wl_compositor_destroy(_info.compositor);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return result;
}


char* elect_shell(InterfaceInfo* _info)
{
    // stable
    if (_info->xdg_wm_base) {
        _info->shell   = _info->xdg_wm_base;
        _info->shellId = E_XDG_WM_BASE;
        xdg_wm_base_add_listener(_info->xdg_wm_base, &xdg_wm_base_listener, NULL);
        return "xdg_wm_base";
    // deprecated but easy-to-use; for old compositors
    } else if (_info->wl_shell) {
        _info->shell   = _info->wl_shell;
        _info->shellId = E_WL_SHELL;
        return "wl_shell";
    }
 
    return NULL;
}

void* create_shell_surface(InterfaceInfo* _info, struct wl_surface* surface, char* arg)
{
   switch(_info->shellId)
   {
     case E_WL_SHELL:
     {
       struct wl_shell_surface* shell_surface = wl_shell_get_shell_surface((struct wl_shell*) _info->shell, surface);
       assert(shell_surface);
       wl_shell_surface_add_listener(shell_surface, &wl_shell_surface_listener, NULL);
       wl_shell_surface_set_title(shell_surface, arg);
       wl_shell_surface_set_toplevel(shell_surface);
       return shell_surface;
     }
     case E_XDG_WM_BASE:
     {
       struct xdg_surface* xdg_surface = xdg_wm_base_get_xdg_surface((struct xdg_wm_base*) _info->shell, surface);
       assert(xdg_surface);
       xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
       struct xdg_toplevel* xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);
       assert(xdg_toplevel);
       xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);
       xdg_toplevel_set_title(xdg_toplevel, arg);
       return xdg_surface;
     }
     default:
       return NULL;
   }
}

void destroy_shell_surface(InterfaceInfo* _info, void* shell_surface)
{
    switch (_info->shellId)
    {
      case E_WL_SHELL:    return wl_shell_surface_destroy((struct wl_shell_surface*) shell_surface);
      case E_XDG_WM_BASE: return xdg_surface_destroy((struct xdg_surface*) shell_surface);
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

    // [/!\ xdg-wm-base expects a commit before attaching a buffer (/!\)]
    wl_surface_commit(window->surface);

    // [/!\ xdg-wm-base expects a configure event before attaching a buffer (/!\)]
    wl_display_roundtrip(_info->display);

    // 1) create a POSIX shared memory object (name must contain a single '/')
    char* slash = strrchr(title, '/');
    asprintf(&buffer->shm_id, "/%s", (slash ? slash + 1 : title));
    buffer->shm_fd = shm_open(buffer->shm_id, O_CREAT|O_RDWR, 0600);
    // allocate as much raw bytes as needed pixels in the file descriptor
    assert(!ftruncate(buffer->shm_fd, width * height * 4));   // *4 = RGBA

    // 2) expose it as a void* buffer, so our code can use 'memset()' directly
    buffer->data = mmap(NULL, width * height * 4, PROT_READ|PROT_WRITE,
                        MAP_SHARED, buffer->shm_fd, 0);
    assert(buffer->data);

    // 3) pass the descriptor to the compositor through its 'wl_shm_pool' interface
    struct wl_shm_pool* shm_pool = wl_shm_create_pool(_info->shm, buffer->shm_fd,
                                                      width * height * 4);
    // and create the final 'wl_buffer' abstraction
    buffer->buffer = wl_shm_pool_create_buffer(shm_pool, 0, width, height,
		                               width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(shm_pool);

    // set the initial raw buffer content, only White pixels (0xFF)
    memset(buffer->data, 0xFF, width * height * 4);

    // 4) attach our 'wl_buffer' to our 'wl_surface', and commit it
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

    if (!strcmp(name, "wl_compositor"))         { _info->compositor  = wl_registry_bind(registry, serial, &wl_compositor_interface, 1);
    } else if (!strcmp(name, "wl_shm"))         { _info->shm         = wl_registry_bind(registry, serial, &wl_shm_interface, 1);
    } else if (!strcmp(name, "wl_seat"))        { _info->seat        = wl_registry_bind(registry, serial, &wl_seat_interface, 1);
    } else if (!strcmp(name, "wl_shell"))       { _info->wl_shell    = wl_registry_bind(registry, serial, &wl_shell_interface, 1);
    } else if (!strcmp(name, "xdg_wm_base"))    { _info->xdg_wm_base = wl_registry_bind(registry, serial, &xdg_wm_base_interface, 1);
    } else if (strstr(name, "gtk_shell"))       { _info->compositorId = E_GNOME;
    } else if (strstr(name, "plasma_shell"))    { _info->compositorId = E_KDE;
    } else if (strstr(name, "wlr_layer_shell")) { _info->compositorId = E_WLROOTS;
    } else if (strstr(name, "weston"))          { _info->compositorId = E_WESTON; }
}

void wl_interface_removed(void* data, struct wl_registry* registry, uint32_t serial)
{ }


// WAYLAND SEAT CALLBACKS

void wl_seat_handle_capabilities(void* data, struct wl_seat* seat, enum wl_seat_capability capabilities)
{
    InterfaceInfo* _info = data;

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        puts("Seats: keyboard discovered!");
        _info->keyboard = wl_seat_get_keyboard(seat);
    }
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        puts("Seats: mouse discovered!");
        _info->pointer = wl_seat_get_pointer(seat);
    }
    if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
        puts("Seats: touchscreen discovered!");
        _info->touch = wl_seat_get_touch(seat);
    }
}

void wl_pointer_handle_enter(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface,
                             wl_fixed_t sx, wl_fixed_t sy)
{ puts("Mouse enters window!"); }

void wl_pointer_handle_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{ puts("Mouse leaves window!"); }

void wl_pointer_handle_motion(void* data, struct wl_pointer* pointer, uint32_t serial, wl_fixed_t sx, wl_fixed_t sy)
{ printf("Mouse moves at: %d:%d\n", wl_fixed_to_int(sx), wl_fixed_to_int(sy)); }

void wl_pointer_handle_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time,
                              uint32_t button, uint32_t state)
{
  char* button_str = os_button_code_to_string(button);

  switch (state) {
    case WL_POINTER_BUTTON_STATE_RELEASED:  printf("Mouse button '%s' released!\n", button_str); return;
    case WL_POINTER_BUTTON_STATE_PRESSED:   printf("Mouse button '%s' pressed!\n", button_str); return;
  }
}


// WAYLAND SHELL CALLBACKS

void wl_shell_surface_handle_ping(void* data, struct wl_shell_surface* shell_surface, uint32_t serial)
{ wl_shell_surface_pong(shell_surface, serial); }


void xdg_wm_base_handle_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial)
{ xdg_wm_base_pong(xdg_wm_base, serial); }

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial)
{ xdg_surface_ack_configure(xdg_surface, serial); }

void xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel)
{ }

void xdg_toplevel_configure(void* data, struct xdg_toplevel* xdg_toplevel,
                            int32_t width, int32_t height, struct wl_array* states)
{ }

