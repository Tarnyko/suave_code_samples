/*
* wayland-09-drag_and_drop.c
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
// $ gcc ... _deps/*.c `pkg-config --cflags --libs wayland-client`

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
#include "_deps/xdg-shell-unstable-v6-client-protocol.h"


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

#define TITLEBAR_HEIGHT 40
#define SELECTION_WIDTH 20

typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef enum {
    E_WL_SHELL = 0, E_XDG_WM_BASE = 1, E_XDG_SHELL = 2
} ShellId;

typedef enum {
    E_TITLEBAR = 4, E_MINIMIZE = 3, E_MAXIMIZE = 2, E_CLOSE = 1, E_MAIN = 0
} ZoneId;


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

    ZoneId              active_zone;
    int                 active_selection[2];
    uint32_t            pointer_pressed_serial;

    char*               title;
    int                 orig_width,  width;
    int                 orig_height, height;
    bool                maximized;
    bool                wants_to_be_closed;
} Window;


typedef struct {
    Window*               window;

    struct wl_display*    display;       // 'display': root object

    CompositorId          compositorId;
    struct wl_compositor* compositor;    // 'compositor': surface manager

    ShellId               shellId;
    void*                 shell;         // 'shell': window manager
    struct wl_shell*      wl_shell;      //  (among: - deprecated
    struct zxdg_shell_v6* xdg_shell;     //          - former unstable
    struct xdg_wm_base*   xdg_wm_base;   //          - current stable)

    struct wl_shm*        shm;           // 'shared mem': software renderer

    struct wl_seat*       seat;          // 'seat': input devices
    struct wl_keyboard*   keyboard;      //  (among: - keyboard
    struct wl_pointer*    pointer;       //          - mouse
    struct wl_touch*      touch;         //          - touchscreen)

    struct wl_data_device_manager* dd_manager;   // drag & drop...
    struct wl_data_device* dd;               ;   // ... seat-specific:
    struct wl_data_source* ds;               ;   // - source data
} InterfaceInfo;


char* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char*);
void destroy_shell_surface(InterfaceInfo*, void*);
void minimize_shell_surface(InterfaceInfo*, void*);
void maximize_shell_surface(InterfaceInfo*, void*);
void move_shell_surface(InterfaceInfo*, void*, uint32_t);

Window* create_window(InterfaceInfo*, char*, int, int);
void destroy_window(InterfaceInfo*, Window*);
void create_window_buffer(InterfaceInfo*, Window*);
void destroy_window_buffer(InterfaceInfo*, Window*);
void resize_window(InterfaceInfo*, Window*, int, int);
void decorate_window(Window*);


typedef enum {
    E_BLACK = 0x00, E_GRAY = 0xAA, E_SILVER = 0xCC, E_WHITE = 0xFF
} ColorId;

void draw_titlebar(Window*, int);
void draw_zone(Window*, int, ColorId, ZoneId);
void draw_selection(Window*, ColorId, int, int);


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


void wl_data_source_target(void*, struct wl_data_source*, const char*);
void wl_data_source_send(void*, struct wl_data_source*, const char*, int32_t);
void wl_data_source_cancelled(void*, struct wl_data_source*);
 
static const struct wl_data_source_listener wl_data_source_listener = {
    wl_data_source_target,
    wl_data_source_send,
    wl_data_source_cancelled
};


void wl_shell_surface_handle_ping(void*, struct wl_shell_surface*, uint32_t);

void wl_shell_surface_configure(void*, struct wl_shell_surface*, uint32_t, int32_t, int32_t);

static const struct wl_shell_surface_listener wl_shell_surface_listener = {
    wl_shell_surface_handle_ping,
    wl_shell_surface_configure
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


void zxdg_shell_v6_handle_ping(void*, struct zxdg_shell_v6*, uint32_t);

static const struct zxdg_shell_v6_listener zxdg_shell_v6_listener = {
    zxdg_shell_v6_handle_ping
};

void zxdg_surface_v6_configure(void*, struct zxdg_surface_v6*, uint32_t);

static const struct zxdg_surface_v6_listener zxdg_surface_v6_listener = {
    zxdg_surface_v6_configure
};

void zxdg_toplevel_v6_configure(void*, struct zxdg_toplevel_v6*,
		                int32_t, int32_t, struct wl_array*);
void zxdg_toplevel_v6_close(void*, struct zxdg_toplevel_v6*);

static const struct zxdg_toplevel_v6_listener zxdg_toplevel_v6_listener = {
    zxdg_toplevel_v6_configure,
    zxdg_toplevel_v6_close
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

    // now this should have been filled by the registry callbacks
    printf("Compositor is: ");
    assert(_info.compositor);
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

        if (!_info.pointer) {
            fprintf(stderr, "No mouse found! The example will run, but lack purpose.\n"); }
        if (!_info.dd_manager) {
            fprintf(stderr, "No drag-and-drop support! The example will run, but lack purpose.\n"); }
    }

    // MAIN LOOP!
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        // if feasible, attach the window to pointer events...
        if (_info.pointer) {
            wl_pointer_add_listener(_info.pointer, &wl_pointer_listener, &_info);
            // ...  then attach drag & drop objects to the pointer's seat
            if (_info.dd_manager) {
                _info.dd = wl_data_device_manager_get_data_device(_info.dd_manager, _info.seat);
                _info.ds = wl_data_device_manager_create_data_source(_info.dd_manager);
                wl_data_source_add_listener(_info.ds, &wl_data_source_listener, window);
                wl_data_source_offer(_info.ds, "text/plain;charset=utf-8");
            }
        }

        printf("\nLooping...\n\n");

        while (loop_result != -1 && !window->wants_to_be_closed) {
            loop_result = wl_display_dispatch(display); }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
    if (_info.xdg_wm_base) {
        xdg_wm_base_destroy(_info.xdg_wm_base); }
    if (_info.xdg_shell) {
        zxdg_shell_v6_destroy(_info.xdg_shell); }
    if (_info.wl_shell) {
        wl_shell_destroy(_info.wl_shell); }
    if (_info.ds) {
        wl_data_source_destroy(_info.ds); }
    if (_info.dd) {
        wl_data_device_destroy(_info.dd); }
    if (_info.dd_manager) {
        wl_data_device_manager_destroy(_info.dd_manager); }
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
    // deprecated stable but easy-to-use; for old compositors
    } else if (_info->wl_shell) {
        _info->shell   = _info->wl_shell;
        _info->shellId = E_WL_SHELL;
        return "wl_shell";
    // former unstable version of 'xdg_wm_base'
    } else if (_info->xdg_shell) {
        _info->shell   = _info->xdg_shell;
        _info->shellId = E_XDG_SHELL;
        zxdg_shell_v6_add_listener(_info->xdg_shell, &zxdg_shell_v6_listener, NULL);
        return "zxdg_shell_v6";
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
       wl_shell_surface_add_listener(shell_surface, &wl_shell_surface_listener, _info);
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
       xdg_toplevel_add_listener(xdg_toplevel, &xdg_toplevel_listener, _info);
       xdg_toplevel_set_title(xdg_toplevel, arg);
       return xdg_toplevel;
     }
     case E_XDG_SHELL:
     {
       struct zxdg_surface_v6* zxdg_surface = zxdg_shell_v6_get_xdg_surface((struct zxdg_shell_v6*) _info->shell, surface);
       assert(zxdg_surface);
       zxdg_surface_v6_add_listener(zxdg_surface, &zxdg_surface_v6_listener, NULL);
       struct zxdg_toplevel_v6* zxdg_toplevel = zxdg_surface_v6_get_toplevel(zxdg_surface);
       assert(zxdg_toplevel);
       zxdg_toplevel_v6_add_listener(zxdg_toplevel, &zxdg_toplevel_v6_listener, _info);
       zxdg_toplevel_v6_set_title(zxdg_toplevel, arg);
       return zxdg_toplevel;
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
      case E_XDG_WM_BASE: return xdg_toplevel_destroy((struct xdg_toplevel*) shell_surface);
      case E_XDG_SHELL:   return zxdg_toplevel_v6_destroy((struct zxdg_toplevel_v6*) shell_surface);
      default:            return;
    }
}

void minimize_shell_surface(InterfaceInfo* _info, void* shell_surface)
{
    switch (_info->shellId)
    {
      case E_WL_SHELL:    puts("(unimplemented: 'wl_shell' cannot minimize)"); return;
      case E_XDG_WM_BASE: return xdg_toplevel_set_minimized((struct xdg_toplevel*) shell_surface);
      case E_XDG_SHELL:   return zxdg_toplevel_v6_set_minimized((struct zxdg_toplevel_v6*) shell_surface);
      default:            return;
    }
}

void maximize_shell_surface(InterfaceInfo* _info, void* shell_surface)
{
    Window* window = _info->window;

    switch (_info->shellId)
    {
      case E_WL_SHELL:
      {
        if (!window->maximized) {
            window->orig_width  = window->width;
            window->orig_height = window->height;
            window->maximized   = true;
            return wl_shell_surface_set_maximized((struct wl_shell_surface*) shell_surface, NULL);
        }
        window->maximized = false;
        wl_shell_surface_set_toplevel((struct wl_shell_surface*) shell_surface);
        return resize_window(_info, window, window->orig_width, window->orig_height);
      }
      case E_XDG_WM_BASE:
      { 
        if (!window->maximized) {
            return xdg_toplevel_set_maximized((struct xdg_toplevel*) shell_surface); }
        window->maximized = false;
        xdg_toplevel_unset_maximized((struct xdg_toplevel*) shell_surface);
        return resize_window(_info, window, window->orig_width, window->orig_height);
      }
      case E_XDG_SHELL:
      {
        if (!window->maximized) {
            return zxdg_toplevel_v6_set_maximized((struct zxdg_toplevel_v6*) shell_surface); }
        window->maximized = false;
        zxdg_toplevel_v6_unset_maximized((struct zxdg_toplevel_v6*) shell_surface);
        return resize_window(_info, window, window->orig_width, window->orig_height);
      }
      default:
        return;
    }
}

void move_shell_surface(InterfaceInfo* _info, void* shell_surface, uint32_t serial)
{
    switch (_info->shellId)
    {
      case E_WL_SHELL:    return wl_shell_surface_move((struct wl_shell_surface*) shell_surface, _info->seat, serial);
      case E_XDG_WM_BASE: return xdg_toplevel_move    ((struct xdg_toplevel*)     shell_surface, _info->seat, serial);
      case E_XDG_SHELL:   return zxdg_toplevel_v6_move((struct zxdg_toplevel_v6*) shell_surface, _info->seat, serial);
      default:            return;
    }
}


Window* create_window(InterfaceInfo* _info, char* title, int width, int height)
{
    Window* window = calloc(1, sizeof(Window));

    window->title  = title;
    window->width  = width;
    window->height = height;

    window->surface = wl_compositor_create_surface(_info->compositor);
    assert(window->surface);
    
    window->shell_surface = create_shell_surface(_info, window->surface, title);
    assert(window->shell_surface);

    // [/!\ xdg-wm-base/shell expects a commit before attaching a buffer (/!\)]
    wl_surface_commit(window->surface);

    // [/!\ xdg-wm-base expects a configure event before attaching a buffer (/!\)]
    wl_display_roundtrip(_info->display);

    create_window_buffer(_info, window);

    decorate_window(window);

    wl_surface_damage(window->surface, 0, 0, width, height);
    wl_surface_commit(window->surface);

    _info->window = window;
    return window;
}

void destroy_window(InterfaceInfo* _info, Window* window)
{
    destroy_window_buffer(_info, window);

    destroy_shell_surface(_info, window->shell_surface);

    wl_surface_destroy(window->surface);

    _info->window = NULL;
    free(window);
}

void create_window_buffer(InterfaceInfo* _info, Window* window)
{
    Buffer* buffer = &window->buffer;

    size_t buffer_size = window->width * window->height * 4;   // *4 = RGBA

    // 1) create a POSIX shared memory object (name must contain a single '/')
    char* slash = strrchr(window->title, '/');
    asprintf(&buffer->shm_id, "/%s", (slash ? slash + 1 : window->title));
    buffer->shm_fd = shm_open(buffer->shm_id, O_CREAT|O_RDWR, 0600);
    // allocate as much raw bytes as needed pixels in the file descriptor
    assert(!ftruncate(buffer->shm_fd, buffer_size));

    // 2) expose it as a void* buffer, so our code can use 'memset()' directly
    buffer->data = mmap(NULL, buffer_size, PROT_READ|PROT_WRITE, MAP_SHARED,
                        buffer->shm_fd, 0);
    assert(buffer->data);

    // 3) pass the descriptor to the compositor through its 'wl_shm_pool' interface
    struct wl_shm_pool* shm_pool = wl_shm_create_pool(_info->shm, buffer->shm_fd,
                                                      buffer_size);
    // and create the final 'wl_buffer' abstraction
    buffer->buffer = wl_shm_pool_create_buffer(shm_pool, 0,
                                     window->width, window->height,
                                     window->width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(shm_pool);

    // set the initial raw buffer content, only White pixels
    memset(buffer->data, E_WHITE, buffer_size);

    // 4) attach our 'wl_buffer' to our 'wl_surface'
    wl_surface_attach(window->surface, buffer->buffer, 0, 0);
}

void destroy_window_buffer(InterfaceInfo* _info, Window* window)
{
    Buffer* buffer = &window->buffer;

    wl_buffer_destroy(buffer->buffer);

    munmap(buffer->data, window->width * window->height * 4);

    close(buffer->shm_fd);
    shm_unlink(buffer->shm_id);
    free(buffer->shm_id);
}

void resize_window(InterfaceInfo* _info, Window* window, int width, int height)
{
  // [/!\ xdg-wm-base: acknowledge previous state (maximized?) before resize (/!\)]
  wl_display_roundtrip(_info->display);

  destroy_window_buffer(_info, window);

  window->width  = width;
  window->height = height;

  create_window_buffer(_info, window);

  decorate_window(window);

  wl_surface_damage(window->surface, 0, 0, width, height);
  wl_surface_commit(window->surface);
}

void decorate_window(Window* window)
{
    if (!window->maximized) {
        draw_titlebar(window, TITLEBAR_HEIGHT); }

    const int width = TITLEBAR_HEIGHT;
    draw_zone(window, width, E_BLACK,  E_CLOSE);
    draw_zone(window, width, E_GRAY,   E_MAXIMIZE);
    draw_zone(window, width, E_SILVER, E_MINIMIZE);
}

void draw_titlebar(Window* window, int width)
{
    memset(window->buffer.data + (width*4)*(width*4)*2, E_BLACK, window->width*4);
}

void draw_zone(Window* window, int width, ColorId color, ZoneId zone)
{
    for (int i = 0; i < width; i++) {
        memset(window->buffer.data + (i+1)*(window->width - zone * width) * 4
                                   +   (i)*(zone * width * 4), color, width * 4); }
}

void draw_selection(Window* window, ColorId color, int x, int y)
{
    const int potential_width = SELECTION_WIDTH;

    const int potential_x = x - potential_width / 2;
    int real_x = (potential_x < 0) ? 0 : ((potential_x > window->width) ? window->width : potential_x);

    const int potential_y = y - potential_width / 2;
    int real_y = (potential_y < TITLEBAR_HEIGHT+1) ? TITLEBAR_HEIGHT+1 : ((potential_y > window->height) ? window->height : potential_y);

    int real_width  = (real_x + potential_width < window->width) ? potential_width : window->width - real_x;
    int real_height = (real_y + potential_width < window->height) ? potential_width : window->height - real_y;
    
    memset(window->buffer.data + (TITLEBAR_HEIGHT+1) * window->width * 4, E_WHITE,
           (window->width * window->height * 4) - (TITLEBAR_HEIGHT+1) * window->width * 4);

    for (int i = 0; i < real_height; i++) {
        memset(window->buffer.data + ((real_y + i) * window->width * 4)
                                   + (real_x * 4), color, real_width*4); }

    wl_surface_attach(window->surface, window->buffer.buffer, 0, 0);
    wl_surface_damage(window->surface, 0, 0, window->width, window->height);
    wl_surface_commit(window->surface);
}


// WAYLAND REGISTRY CALLBACKS

void wl_interface_available(void* data, struct wl_registry* registry, uint32_t serial, const char* name, uint32_t version)
{
    InterfaceInfo* _info = data;

    if (!strcmp(name, "wl_compositor"))                 { _info->compositor  = wl_registry_bind(registry, serial, &wl_compositor_interface, 1);
    } else if (!strcmp(name, "wl_shm"))                 { _info->shm         = wl_registry_bind(registry, serial, &wl_shm_interface, 1);
    } else if (!strcmp(name, "wl_seat"))                { _info->seat        = wl_registry_bind(registry, serial, &wl_seat_interface, 1);
    } else if (!strcmp(name, "wl_data_device_manager")) { _info->dd_manager  = wl_registry_bind(registry, serial, &wl_data_device_manager_interface, 1);
    } else if (!strcmp(name, "wl_shell"))               { _info->wl_shell    = wl_registry_bind(registry, serial, &wl_shell_interface, 1);
    } else if (!strcmp(name, "xdg_wm_base"))            { _info->xdg_wm_base = wl_registry_bind(registry, serial, &xdg_wm_base_interface, 1);
    } else if (strstr(name, "xdg_shell"))               { _info->xdg_shell   = wl_registry_bind(registry, serial, &zxdg_shell_v6_interface, 1);
    } else if (strstr(name, "gtk_shell"))               { _info->compositorId = E_GNOME;
    } else if (strstr(name, "plasma_shell"))            { _info->compositorId = E_KDE;
    } else if (strstr(name, "wlr_layer_shell"))         { _info->compositorId = E_WLROOTS;
    } else if (strstr(name, "weston"))                  { _info->compositorId = E_WESTON; }
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
                             wl_fixed_t, wl_fixed_t)
{ puts("Mouse enters window!"); }

void wl_pointer_handle_leave(void* data, struct wl_pointer* pointer, uint32_t serial, struct wl_surface* surface)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  puts("Mouse leaves window!");

  window->pointer_pressed_serial = 0;
}

void wl_pointer_handle_motion(void* data, struct wl_pointer* pointer, uint32_t serial, wl_fixed_t sx, wl_fixed_t sy)
{ 
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  int x = wl_fixed_to_int(sx);
  int y = wl_fixed_to_int(sy);
  printf("Mouse moves at: %d:%d\n", x, y);

  if (y > TITLEBAR_HEIGHT) {
      window->active_zone = E_MAIN;
 
      if (window->pointer_pressed_serial > 0 && !window->maximized) {
        window->active_selection[0] = x;
        window->active_selection[1] = y;
        puts("drag-and-drop: action initiated!");
        wl_data_device_start_drag(_info->dd, _info->ds, window->surface,
                                  NULL, window->pointer_pressed_serial);
      } else {
        draw_selection(window, E_GRAY, x, y);
      }
  } else {
    if (x > (window->width - (E_CLOSE * TITLEBAR_HEIGHT))) {
      window->active_zone = E_CLOSE;
    } else if (x > (window->width - (E_MAXIMIZE * TITLEBAR_HEIGHT))) {
      window->active_zone = E_MAXIMIZE;
    } else if (x > (window->width - (E_MINIMIZE * TITLEBAR_HEIGHT))) {
      window->active_zone = E_MINIMIZE;
    } else {
      window->active_zone = E_TITLEBAR;

      if (window->pointer_pressed_serial > 0 && !window->maximized) {
        puts("'TITLEBAR' is being dragged!");
        move_shell_surface(_info, window->shell_surface, window->pointer_pressed_serial);
      }
    }
  }
}

void wl_pointer_handle_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time,
                              uint32_t button, uint32_t state)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  char* button_str = os_button_code_to_string(button);

  switch (state)
  {
    case WL_POINTER_BUTTON_STATE_RELEASED:
    {
        printf("Mouse button '%s' released!\n", button_str);
        window->pointer_pressed_serial = 0;
        return;
    }
    case WL_POINTER_BUTTON_STATE_PRESSED:
    {
      printf("Mouse button '%s' pressed!\n", button_str);
      window->pointer_pressed_serial = serial;

      switch (window->active_zone)
      {
        case E_CLOSE:    { puts("'CLOSE' button has been pressed!");
                           window->wants_to_be_closed = true;
                           return; }
        case E_MAXIMIZE: { puts("'MAXIMIZE' button has been pressed!");
                           return maximize_shell_surface(_info, window->shell_surface); }
        case E_MINIMIZE: { puts("'MINIMIZE' button has been pressed!");
                           return minimize_shell_surface(_info, window->shell_surface); }
        default:
      }
    }
    default:
  }
}


// WAYLAND DATA_SOURCE CALLBACKS

void wl_data_source_target(void* data, struct wl_data_source* source, const char* mime_type)
{
  if (!mime_type) {
    puts("drag-and-drop: destination client does not accept this MIME type.");
    return;
  }
  printf("drag-and-drop: MIME type '%s' accepted by destination client.\n", mime_type);
}

void wl_data_source_send(void* data, struct wl_data_source* source, const char* mime_type, int32_t fd)
{
  if (!mime_type) {
    puts("drag-and-drop: destination client does not accept this MIME type.");
    return;
  }
  Window* window = data;

  char* text;
  asprintf(&text, "Hello from '%s', my selection was %dx%d!\n",
           window->title, window->active_selection[0], window->active_selection[1]);
  write(fd, text, strlen(text));
  free(text);
}

void wl_data_source_cancelled(void* data, struct wl_data_source* source)
{
  puts("drag-and-drop: action cancelled by destination client.");
}


// WAYLAND SHELL CALLBACKS

void wl_shell_surface_handle_ping(void* data, struct wl_shell_surface* shell_surface, uint32_t serial)
{
  wl_shell_surface_pong(shell_surface, serial);
}

void wl_shell_surface_configure(void* data, struct wl_shell_surface* shell_surface, uint32_t edges,
                                int32_t width, int32_t height)
{
  InterfaceInfo* _info = data;

  if (width > 0 && height > 0) {
    resize_window(_info, _info->window, width, height); }
}


void xdg_wm_base_handle_ping(void* data, struct xdg_wm_base* xdg_wm_base, uint32_t serial)
{
  xdg_wm_base_pong(xdg_wm_base, serial);
}

void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial)
{
  xdg_surface_ack_configure(xdg_surface, serial);
}

void xdg_toplevel_close(void* data, struct xdg_toplevel* xdg_toplevel)
{ }

void xdg_toplevel_configure(void* data, struct xdg_toplevel* xdg_toplevel,
                            int32_t width, int32_t height, struct wl_array* states)
{ 
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  uint32_t *state;
  wl_array_for_each(state, states)
  {
    if (width > 0 && height > 0) {
      if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED ||
          *state == XDG_TOPLEVEL_STATE_FULLSCREEN) {
        window->maximized   = true;
        window->orig_width  = window->width;
        window->orig_height = window->height;
      }
      resize_window(_info, _info->window, width, height);
    }
  }
}


void zxdg_shell_v6_handle_ping(void* data, struct zxdg_shell_v6* xdg_shell, uint32_t serial)
{
  zxdg_shell_v6_pong(xdg_shell, serial);
}

void zxdg_surface_v6_configure(void* data, struct zxdg_surface_v6* xdg_surface, uint32_t serial)
{
  zxdg_surface_v6_ack_configure(xdg_surface, serial);
}

void zxdg_toplevel_v6_close(void* data, struct zxdg_toplevel_v6* xdg_toplevel)
{ }

void zxdg_toplevel_v6_configure(void* data, struct zxdg_toplevel_v6* xdg_toplevel,
                                int32_t width, int32_t height, struct wl_array* states)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  uint32_t *state;
  wl_array_for_each(state, states) {
    if (width > 0 && height > 0) {
      if (*state == ZXDG_TOPLEVEL_V6_STATE_MAXIMIZED ||
          *state == ZXDG_TOPLEVEL_V6_STATE_FULLSCREEN) {
        window->maximized   = true;
        window->orig_width  = window->width;
        window->orig_height = window->height;
      }
      resize_window(_info, _info->window, width, height);
    }
  }
}

