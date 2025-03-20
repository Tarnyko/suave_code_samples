/*
* wayland-06-accelerated_rendering-vulkan.c
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
// $ gcc ... _deps/*.c `pkg-config --cflags --libs wayland-client vulkan`

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Wayland
#include <wayland-client.h>
#include "_deps/xdg-wm-base-client-protocol.h"
#include "_deps/xdg-shell-unstable-v6-client-protocol.h"

// Vulkan
#define VK_USE_PLATFORM_WAYLAND_KHR   // for "VkWaylandSurfaceCreateInfoKHR()"
#include <vulkan/vulkan.h>


// My prototypes

typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef enum {
    E_WL_SHELL = 0, E_XDG_WM_BASE = 1, E_XDG_SHELL = 2
} ShellId;


typedef struct {
    VkSurfaceKHR          vk_surface;      // Vulkan surface attached to...
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
    struct zxdg_shell_v6* xdg_shell;     //          - former unstable
    struct xdg_wm_base*   xdg_wm_base;   //          - current stable)

    VkInstance            vk_instance;   // Vulkan: - instance
    bool has_vulkan;
} InterfaceInfo;


char* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char*);
void destroy_shell_surface(InterfaceInfo*, void*);

void initialize_vulkan(InterfaceInfo*);

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

    // check & initiliaze Vulkan
    initialize_vulkan(&_info);
    if (!_info.has_vulkan) {
        fprintf(stderr, "No valid Vulkan implementation found! Exiting...\n");
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
    if (_info.has_vulkan) {
        vkDestroyInstance(_info.vk_instance, NULL); }
    if (_info.xdg_wm_base) {
        xdg_wm_base_destroy(_info.xdg_wm_base); }
    if (_info.xdg_shell) {
        zxdg_shell_v6_destroy(_info.xdg_shell); }
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
    // deprecated but easy-to-use; for old compositors
    if (_info->wl_shell) {
        _info->shell   = _info->wl_shell;
        _info->shellId = E_WL_SHELL;
        return "wl_shell";
    // stable
    } else if (_info->xdg_wm_base) {
        _info->shell   = _info->xdg_wm_base;
        _info->shellId = E_XDG_WM_BASE;
        xdg_wm_base_add_listener(_info->xdg_wm_base, &xdg_wm_base_listener, NULL);
        return "xdg_wm_base";
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
     case E_XDG_SHELL:
     {
       struct zxdg_surface_v6* zxdg_surface = zxdg_shell_v6_get_xdg_surface((struct zxdg_shell_v6*) _info->shell, surface);
       assert(zxdg_surface);
       zxdg_surface_v6_add_listener(zxdg_surface, &zxdg_surface_v6_listener, NULL);
       struct zxdg_toplevel_v6* zxdg_toplevel = zxdg_surface_v6_get_toplevel(zxdg_surface);
       assert(zxdg_toplevel);
       zxdg_toplevel_v6_add_listener(zxdg_toplevel, &zxdg_toplevel_v6_listener, NULL);
       zxdg_toplevel_v6_set_title(zxdg_toplevel, arg);
       return zxdg_surface;
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
      case E_XDG_SHELL:   return zxdg_surface_v6_destroy((struct zxdg_surface_v6*) shell_surface);
      default:
    }
}


void initialize_vulkan(InterfaceInfo* _info)
{
    // Check if instance can support Wayland

    uint32_t vkext_count = 0;

    if (vkEnumerateInstanceExtensionProperties(NULL, &vkext_count, NULL)
          || vkext_count == 0) {
        return; }

    bool instance_has_wayland = false;

    VkExtensionProperties* vkexts = calloc(vkext_count, sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &vkext_count, vkexts);
    for (uint32_t c = 0; c < vkext_count; c++) {
        if (!strcmp(vkexts[c].extensionName, "VK_KHR_wayland_surface")) {
            instance_has_wayland = true;
            break;
        }
    }
    free(vkexts);

    if (!instance_has_wayland) {
        return; }

    // Print Vulkan version

    uint32_t vulkan_major, vulkan_minor, vulkan_patch, version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&version);
    vulkan_major = VK_VERSION_MAJOR(version);
    vulkan_minor = VK_VERSION_MINOR(version);
    vulkan_patch = VK_VERSION_PATCH(version);

    printf("Vulkan version: %d.%d.%d ", vulkan_major, vulkan_minor, vulkan_patch);

    // Create instance

    char* wayland_extensions[2] = { "VK_KHR_surface", "VK_KHR_wayland_surface" };

    VkInstanceCreateInfo vkinfo = {0};
    vkinfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    vkinfo.enabledExtensionCount   = sizeof(wayland_extensions) / sizeof(char*);
    vkinfo.ppEnabledExtensionNames = (const char**) wayland_extensions;

    VkInstance vk_instance = {0};

    if (vkCreateInstance(&vkinfo, NULL, &vk_instance)) {
	_info->has_vulkan = false;
        return; }

    // Enumerate GPUs...

    uint32_t vulkan_gpu_count = 0;

    vkEnumeratePhysicalDevices(vk_instance, &vulkan_gpu_count, NULL);
    if (vulkan_gpu_count == 0) {
       vkDestroyInstance(vk_instance, NULL);
       _info->has_vulkan = false;
       return;
    }

    printf("[GPUs:%d", vulkan_gpu_count);

    // ...along with their physical properties (name, VRAM)

    VkPhysicalDevice vk_gpu, *vkgpus = calloc(vulkan_gpu_count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vk_instance, &vulkan_gpu_count, vkgpus);
    for (uint32_t c = 0; c < vulkan_gpu_count; c++) {
        VkPhysicalDeviceProperties vkgpu_props = {0};
        VkPhysicalDeviceMemoryProperties vkgpu_memprops = {0};

        vkGetPhysicalDeviceProperties(vkgpus[c], &vkgpu_props);
        vkGetPhysicalDeviceMemoryProperties(vkgpus[c], &vkgpu_memprops);

	printf(", (%s: %ldMb)", vkgpu_props.deviceName, vkgpu_memprops.memoryHeaps[0].size / 1000000);
    }
    vk_gpu = vkgpus[0];
    free(vkgpus);

    puts("]");

    // Check if main GPU can support swapchain commands    

    uint32_t vkgpuext_count = 0;

    if (vkEnumerateDeviceExtensionProperties(vk_gpu, NULL, &vkgpuext_count, NULL)
          || vkgpuext_count == 0) {
        return; }

    bool gpu_has_swapchain = false;

    VkExtensionProperties* vkgpuexts = calloc(vkgpuext_count, sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(vk_gpu, NULL, &vkgpuext_count, vkgpuexts);
    for (uint32_t c = 0; c < vkgpuext_count; c++) {
        if (!strcmp(vkgpuexts[c].extensionName, "VK_KHR_swapchain")) {
            gpu_has_swapchain = true;
            break;
        }
    }
    free(vkgpuexts);

    if (!gpu_has_swapchain) {
        return; }

    // Success!

    _info->has_vulkan = true;
    _info->vk_instance = vk_instance;
}


Window* create_window(InterfaceInfo* _info, char* title, int width, int height)
{
    Window* window = calloc(1, sizeof(Window));

    window->width = width;
    window->height = height;

    window->surface = wl_compositor_create_surface(_info->compositor);
    assert(window->surface);

    VkWaylandSurfaceCreateInfoKHR vkcreateinfo = {0};
    vkcreateinfo.sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR;
    vkcreateinfo.display = _info->display;
    vkcreateinfo.surface = window->surface;

    assert(!vkCreateWaylandSurfaceKHR(_info->vk_instance, &vkcreateinfo, NULL, &window->vk_surface));

    window->shell_surface = create_shell_surface(_info, window->surface, title);
    assert(window->shell_surface);

    // [/!\ xdg-wm-base/shell expects a commit before redrawing (/!\)]
    wl_surface_commit(window->surface);

    // [/!\ xdg-wm-base expects a configure event before redrawing (/!\)]
    wl_display_roundtrip(_info->display);

    return window;
}

void redraw_window(InterfaceInfo* _info, Window* window)
{ }

void destroy_window(InterfaceInfo* _info, Window* window)
{
    destroy_shell_surface(_info, window->shell_surface);

    vkDestroySurfaceKHR(_info->vk_instance, window->vk_surface, NULL);

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
    } else if (strstr(name, "xdg_shell"))       { _info->xdg_shell   = wl_registry_bind(registry, serial, &zxdg_shell_v6_interface, 1);
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


void zxdg_shell_v6_handle_ping(void* data, struct zxdg_shell_v6* xdg_shell, uint32_t serial)
{ zxdg_shell_v6_pong(xdg_shell, serial); }

void zxdg_surface_v6_configure(void* data, struct zxdg_surface_v6* xdg_surface, uint32_t serial)
{ zxdg_surface_v6_ack_configure(xdg_surface, serial); }

void zxdg_toplevel_v6_close(void* data, struct zxdg_toplevel_v6* xdg_toplevel)
{ }

void zxdg_toplevel_v6_configure(void* data, struct zxdg_toplevel_v6* xdg_toplevel,
                                int32_t width, int32_t height, struct wl_array* states)
{ }

