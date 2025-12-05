/*
* wayland-05-accelerated_rendering-opengl.c
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
 - Debian/Ubuntu: $ sudo apt install libwayland-dev libegl-dev libgles-dev
 - Fedora/RHEL:   $ sudo dnf install wayland-devel libglvnd-devel

    Compile with:
 $ gcc ... `pkg-config --cflags --libs wayland-client wayland-egl egl glesv2`
*/

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Wayland
#include <wayland-client.h>
#include "_deps/xdg-wm-base-client-protocol.h"

// EGL (OpenGL, OpenGL ES)
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>


// My prototypes

typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef enum {
    E_WL_SHELL = 0, E_XDG_WM_BASE = 1
} ShellId;


typedef struct {
    EGLSurface            egl_surface;     // EGL surface attached to...
    struct wl_egl_window* egl_window;      // ...a Wayland-EGL glue to...
    struct wl_surface*    surface;         // ...a Wayland surface object...
    void*                 shell_surface;   // ...handled by a window manager.

    int                   width;
    int                   height;
} Window;


typedef struct {
    struct wl_display*    display;       // 'display': root object

    CompositorId          compositorId;
    struct wl_compositor* compositor;    // 'compositor': surface manager

    ShellId               shellId;
    void*                 shell;         // 'shell': window manager
    struct wl_shell*      wl_shell;      //  (among: - deprecated
    struct xdg_wm_base*   xdg_wm_base;   //          - current stable)

    EGLDisplay            egl_display;   // EGL: - display
    EGLConfig             egl_config;    //      - configuration
    EGLContext            egl_context;   //      - current context

    bool has_egl;
    bool has_opengl;
    bool has_opengles;
} InterfaceInfo;


char* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char*);
void destroy_shell_surface(InterfaceInfo*, void*);

void initialize_egl(InterfaceInfo*, struct wl_display*);
bool initialize_egl_api(InterfaceInfo*, EGLDisplay, EGLenum);

Window* create_window(InterfaceInfo*, char*, int, int);
void redraw_window(InterfaceInfo*, Window*);
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

    // choose a shell/window manager in a most-to-less-compatible order
    if (!(shell_name = elect_shell(&_info))) {
        fprintf(stderr, "No compatible window manager/shell interface found! Exiting...\n");
        goto error;
    }
    printf("Shell/window manager: '%s'\n\n", shell_name);

    // check & initiliaze EGL
    initialize_egl(&_info, display);
    if (!_info.has_egl || (!_info.has_opengl && !_info.has_opengles)) {
        fprintf(stderr, "No valid EGL/OpenGL(ES) implementation found! Exiting...\n");
        goto error;
    }

    // MAIN LOOP!
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        printf("\nLooping...\n\n");

        while (loop_result != -1) {
            loop_result = wl_display_dispatch_pending(display);
            redraw_window(&_info, window);
        }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
    if (_info.has_opengl || _info.has_opengles) {
        eglDestroyContext(_info.egl_display, _info.egl_context); }
    if (_info.has_egl) {
        eglTerminate(_info.egl_display); }
    if (_info.xdg_wm_base) {
        xdg_wm_base_destroy(_info.xdg_wm_base); }
    if (_info.wl_shell) {
        wl_shell_destroy(_info.wl_shell); }
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

void initialize_egl(InterfaceInfo* _info, struct wl_display* display)
{
    EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType) display);
    if (!egl_display) {
        return; }

    EGLint egl_major, egl_minor;
    if (!eglInitialize(egl_display, &egl_major, &egl_minor)) {
        return; }

    printf("EGL version:\t %d.%d [%s]\n", egl_major, egl_minor,
                                          eglQueryString(egl_display, EGL_VENDOR));
    _info->has_egl = true;

    _info->has_opengles = initialize_egl_api(_info, egl_display, EGL_OPENGL_ES_API);
    if (!_info->has_opengles) {
        _info->has_opengl = initialize_egl_api(_info, egl_display, EGL_OPENGL_API); }

    _info->egl_display = egl_display;
}

bool initialize_egl_api(InterfaceInfo* _info, EGLDisplay egl_display, EGLenum api)
{
    if (!eglBindAPI(api)) {
        return false; }

    EGLint cfg_count = 0;
    eglGetConfigs(egl_display, NULL, 0, &cfg_count);
    if (cfg_count == 0) {
        return false; }

    EGLConfig egl_config, *cfgs = calloc(cfg_count, sizeof(EGLConfig));
    EGLint valid_cfg_count, valid_cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1, EGL_GREEN_SIZE, 1, EGL_BLUE_SIZE, 1, EGL_DEPTH_SIZE, 1,
        EGL_RENDERABLE_TYPE, (api == EGL_OPENGL_ES_API ? EGL_OPENGL_ES_BIT : EGL_OPENGL_BIT),
        EGL_NONE
    };
    eglChooseConfig(egl_display, valid_cfg_attrs, cfgs, cfg_count, &valid_cfg_count);
    if (valid_cfg_count == 0) {
        free(cfgs);
        return false;
    }
    egl_config = cfgs[0];
    free(cfgs);

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 1,
        EGL_NONE
    };
    EGLContext egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctx_attrs);
    if (!egl_context) {
        return false; }

    _info->egl_config  = egl_config;
    _info->egl_context = egl_context;
    return true;
}


Window* create_window(InterfaceInfo* _info, char* title, int width, int height)
{
    Window* window = calloc(1, sizeof(Window));

    window->width = width;
    window->height = height;

    window->surface = wl_compositor_create_surface(_info->compositor);
    assert(window->surface);

    window->egl_window = wl_egl_window_create(window->surface, width, height);
    assert(window->egl_window);
    
    window->egl_surface = eglCreateWindowSurface(_info->egl_display, _info->egl_config,
                                       (EGLNativeWindowType) window->egl_window, NULL);
    assert(window->egl_surface != EGL_NO_SURFACE);

    assert(eglMakeCurrent(_info->egl_display, window->egl_surface, window->egl_surface,
                          _info->egl_context));

    printf("OpenGL version:\t %s [%s]\n", glGetString(GL_VERSION), glGetString(GL_VENDOR));

    window->shell_surface = create_shell_surface(_info, window->surface, title);
    assert(window->shell_surface);

    // [/!\ xdg-wm-base expects a commit before redrawing (/!\)]
    wl_surface_commit(window->surface);

    // [/!\ xdg-wm-base expects a configure event before redrawing (/!\)]
    wl_display_roundtrip(_info->display);

    return window;
}

void redraw_window(InterfaceInfo* _info, Window* window)
{
    static float color[3] = { 1.0, 1.0, 1.0 };   // =White (RGB)

    // these functions are common to OpenGL<->OpenGLES
    glViewport(0, 0, window->width, window->height);
    glClearColor(color[0], color[1], color[2], 0.0);
    glClear(GL_COLOR_BUFFER_BIT);
    eglSwapBuffers(_info->egl_display, window->egl_surface);

    for (int c = 0; c < sizeof(color); c++) {
        color[c] = (color[c] <= 0.0) ? 1.0 : color[c] - 0.01; }
}

void destroy_window(InterfaceInfo* _info, Window* window)
{
    destroy_shell_surface(_info, window->shell_surface);

    eglDestroySurface(_info->egl_display, window->egl_surface);

    wl_egl_window_destroy(window->egl_window);

    wl_surface_destroy(window->surface);

    free(window);
}


// WAYLAND REGISTRY CALLBACKS

void wl_interface_available(void* data, struct wl_registry* registry, uint32_t serial, const char* name, uint32_t version)
{
    InterfaceInfo* _info = data;

    if (!strcmp(name, "wl_compositor"))         { _info->compositor  = wl_registry_bind(registry, serial, &wl_compositor_interface, 1);
    } else if (!strcmp(name, "wl_shell"))       { _info->wl_shell    = wl_registry_bind(registry, serial, &wl_shell_interface, 1);
    } else if (!strcmp(name, "xdg_wm_base"))    { _info->xdg_wm_base = wl_registry_bind(registry, serial, &xdg_wm_base_interface, 1);
    } else if (strstr(name, "gtk_shell"))       { _info->compositorId = E_GNOME;
    } else if (strstr(name, "plasma_shell"))    { _info->compositorId = E_KDE;
    } else if (strstr(name, "wlr_layer_shell")) { _info->compositorId = E_WLROOTS;
    } else if (strstr(name, "weston"))          { _info->compositorId = E_WESTON; }
}

void wl_interface_removed(void* data, struct wl_registry* registry, uint32_t serial)
{ }


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

