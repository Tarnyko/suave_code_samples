/*
* wayland-08bis-input_shell-libdecor.c
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
 - Debian/Ubuntu: $ sudo apt install libwayland-dev libdecor-0-dev
 - Fedora/RHEL:   $ sudo dnf install wayland-devel libdecor-devel

    Compile with:
 $ gcc ... `pkg-config --cflags --libs wayland-client libdecor-0`
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

// libdecor headers
#include <libdecor.h>


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


typedef struct {
    char*             shm_id;          // UNIX shared memory namespace...
    int               shm_fd;          // ...whose file descriptor is...
    void*             data;            // ...mem-mapped to a Raw buffer.
    struct wl_buffer* buffer;          // Wayland abstraction of the above.
} Buffer;

typedef struct {
    Buffer                     buffer;   // Raw/Wayland buffer mapped to...
    struct wl_surface*         surface;  // ...a Wayland surface object...
    struct libdecor_frame*     frame;    // ...handled by a window manager.

    char*                      title;
    int                        width;
    int                        height;
    enum libdecor_window_state stateId;
    bool                       wants_to_be_closed;
} Window;


typedef struct {
    Window*               window;

    struct wl_display*    display;       // 'display': root object

    CompositorId          compositorId;
    struct wl_compositor* compositor;    // 'compositor': surface manager

    struct wl_shm*        shm;           // 'shared mem': software renderer

    struct wl_seat*       seat;          // 'seat': input devices
    struct wl_keyboard*   keyboard;      //  (among: - keyboard
    struct wl_pointer*    pointer;       //          - mouse
    struct wl_touch*      touch;         //          - touchscreen)

    struct libdecor*      decor;         // 'libdecor': wraps window manager
} InterfaceInfo;


Window* create_window(InterfaceInfo*, char*, int, int);
void destroy_window(InterfaceInfo*, Window*);
void create_window_buffer(InterfaceInfo*, Window*);
void destroy_window_buffer(InterfaceInfo*, Window*);
void resize_window(InterfaceInfo*, Window*, int, int);


// Wayland predefined interfaces prototypes

void wl_interface_available(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
void wl_interface_removed(void*, struct wl_registry*, uint32_t);

static const struct wl_registry_listener wl_registry_listener = {
    wl_interface_available,
    wl_interface_removed
};


void decor_handle_error(struct libdecor*, enum libdecor_error, const char*);

static struct libdecor_interface libdecor_interface = {
    decor_handle_error
};

void decor_frame_configure(struct libdecor_frame*, struct libdecor_configuration*, void*);
void decor_frame_close(struct libdecor_frame*, void*);
void decor_frame_commit(struct libdecor_frame*, void*);
void decor_frame_dismiss_popup(struct libdecor_frame*, const char*, void*);

static struct libdecor_frame_interface libdecor_frame_interface = {
    decor_frame_configure,
    decor_frame_close,
    decor_frame_commit,
    decor_frame_dismiss_popup,
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
    _info.decor   = libdecor_new(display, &libdecor_interface);
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

    // no need to bother if shm (software rendering) is not available...
    if (!_info.shm) {
        fprintf(stderr, "No software rendering 'wl_shm' interface found! Exiting...\n");
        goto error;
    }

    // look for the mouse, and warn if it is missing
    if (!_info.seat) {
        fprintf(stderr, "No input 'wl_seat' interface found! The example will run, but lack purpose.\n");
    } else {
        wl_seat_add_listener(_info.seat, &wl_seat_listener, &_info);
        wl_display_roundtrip(display);

        if (!_info.pointer) {
            fprintf(stderr, "No mouse found! The example will run, but lack purpose.\n"); }
    }

    // MAIN LOOP!
    int loop_result, result = EXIT_SUCCESS;
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        // if feasible, attach the window to pointer events
        if (_info.pointer) {
            wl_pointer_add_listener(_info.pointer, &wl_pointer_listener, &_info); }

        printf("\nLooping...\n\n");

        while (loop_result != -1 && !window->wants_to_be_closed) {
            loop_result = libdecor_dispatch(_info.decor, 0); }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
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
    libdecor_unref(_info.decor);
    wl_compositor_destroy(_info.compositor);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return result;
}


Window* create_window(InterfaceInfo* _info, char* title, int width, int height)
{
    Window* window = calloc(1, sizeof(Window));
    _info->window = window;

    window->title   = title;
    window->width   = width;
    window->height  = height;
    window->stateId = LIBDECOR_WINDOW_STATE_ACTIVE;

    window->surface = wl_compositor_create_surface(_info->compositor);
    assert(window->surface);

    window->frame = libdecor_decorate(_info->decor, window->surface,
                                      &libdecor_frame_interface, _info);
    assert(window->frame);

    // 'app-id' is paramount, 'title' is secondary
    libdecor_frame_set_app_id(window->frame, title);
    libdecor_frame_set_title(window->frame, title);

    // callbacks fire from there; "_info->window" is better reachable
    libdecor_frame_map(window->frame);

    create_window_buffer(_info, window);
    wl_surface_damage(window->surface, 0, 0, width, height);

    return window;
}

void destroy_window(InterfaceInfo* _info, Window* window)
{
    destroy_window_buffer(_info, window);

    libdecor_frame_unref(window->frame);

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
    memset(buffer->data, 0xFF, buffer_size);

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

  wl_surface_damage(window->surface, 0, 0, width, height);
  wl_surface_commit(window->surface);
}


// WAYLAND REGISTRY CALLBACKS

void wl_interface_available(void* data, struct wl_registry* registry, uint32_t serial, const char* name, uint32_t version)
{
    InterfaceInfo* _info = data;

    if (!strcmp(name, "wl_compositor"))         { _info->compositor  = wl_registry_bind(registry, serial, &wl_compositor_interface, 1);
    } else if (!strcmp(name, "wl_shm"))         { _info->shm         = wl_registry_bind(registry, serial, &wl_shm_interface, 1);
    } else if (!strcmp(name, "wl_seat"))        { _info->seat        = wl_registry_bind(registry, serial, &wl_seat_interface, 1);
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
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  int x = wl_fixed_to_int(sx);
  int y = wl_fixed_to_int(sy);
  printf("Mouse moves at: %d:%d\n", x, y);
}

void wl_pointer_handle_button(void* data, struct wl_pointer* pointer, uint32_t serial, uint32_t time,
                              uint32_t button, uint32_t state)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  char* button_str = os_button_code_to_string(button);

  switch (state)
  {
    case WL_POINTER_BUTTON_STATE_RELEASED: printf("Mouse button '%s' released!\n", button_str); return;
    case WL_POINTER_BUTTON_STATE_PRESSED : printf("Mouse button '%s' pressed!\n", button_str); return;
  }
}


// WAYLAND DECOR CALLBACKS

void decor_handle_error(struct libdecor* decor, enum libdecor_error error, const char* message)
{ }

void decor_frame_configure(struct libdecor_frame* frame, struct libdecor_configuration*
                           configuration, void* data)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  int width  = window->width;
  int height = window->height;
  enum libdecor_window_state stateId = window->stateId;

  if (libdecor_configuration_get_content_size(configuration, frame, &width, &height)) {
      window->width  = (width > 0)  ? width  : window->width;
      window->height = (height > 0) ? height : window->height;
  }

  if (libdecor_configuration_get_window_state(configuration, &stateId)) {
      window->stateId = stateId;
  }

  struct libdecor_state* state = libdecor_state_new(width, height);
  libdecor_frame_commit(frame, state, configuration);
  libdecor_state_free(state);

  resize_window(_info, window, width, height);
}

void decor_frame_close(struct libdecor_frame* frame, void* data)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  window->wants_to_be_closed = true;
}

void decor_frame_commit(struct libdecor_frame* frame, void* data)
{
  InterfaceInfo* _info = data;
  Window* window = _info->window;

  wl_surface_commit(window->surface);
}

void decor_frame_dismiss_popup(struct libdecor_frame* frame, const char* seat_name, void* data)
{ }

