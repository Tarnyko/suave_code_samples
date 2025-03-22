/*
* wayland-02-list_interfaces-opengl_vulkan.c
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
 - Debian/Ubuntu: $ sudo apt install libwayland-dev libegl-dev libgles-dev libvulkan-dev
 - Fedora/RHEL:   $ sudo dnf install wayland-devel libglvnd-devel vulkan-loader-devel

    Compile with:
 $ gcc ... `pkg-config --cflags --libs wayland-client wayland-egl egl glesv2 vulkan`
*/

#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Wayland
#include <wayland-client.h>

// EGL (OpenGL, OpenGL ES)
#include <wayland-egl.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

// Vulkan
#include <vulkan/vulkan.h>


typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3, E_WLROOTS = 4
} CompositorId;

typedef struct {
    struct wl_compositor* compositor;
    CompositorId id;

    bool has_egl;
    char* egl_vendor;
    EGLint egl_major, egl_minor;

    bool has_opengl;
    bool has_opengles;
    char* opengl_vendor;
    char* opengl_version;
    char* opengles_vendor;
    char* opengles_version;

    bool has_vulkan;
    uint32_t  vulkan_major, vulkan_minor, vulkan_patch;
    uint32_t  vulkan_gpu_count;
    char**    vulkan_gpu_names;
    uint32_t* vulkan_gpu_rams;
} InterfaceInfo;


void check_egl(InterfaceInfo*, struct wl_display*);
void check_egl_api(InterfaceInfo*, EGLDisplay, EGLenum);
void check_vulkan(InterfaceInfo*);


void wl_interface_available(void*, struct wl_registry*, uint32_t, const char*, uint32_t);
void wl_interface_removed(void*, struct wl_registry*, uint32_t);

static const struct wl_registry_listener wl_registry_listener = {
    wl_interface_available,
    wl_interface_removed
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
    wl_registry_add_listener(registry, &wl_registry_listener, &_info);

    // sync-wait for a compositor roundtrip, so all callbacks are fired (see 'WL_REGISTRY_CALLBACKS' below)
    wl_display_roundtrip(display);

    // check for EGL/Vulkan, without callbacks
    check_egl(&_info, display);
    check_vulkan(&_info);

    // now this should have been filled by the registry callbacks
    printf("\nCompositor is: ");
    switch (_info.id)
    {
        case E_WESTON : printf("Weston.\n\n");     break;
        case E_GNOME  : printf("GNOME.\n\n");      break;
        case E_KDE    : printf("KDE Plasma.\n\n"); break;
        case E_WLROOTS: printf("wlroots.\n\n");    break;
        default       : printf("Unknown...\n\n");
    }

    //now this should have been filled by the rest
    if (_info.has_egl) {
        printf("EGL version:\t\t\t %d.%d [%s]\n", _info.egl_major, _info.egl_minor, _info.egl_vendor);

        if (_info.has_opengl) {
            printf("OpenGL (desktop) version:\t %s [%s]\n", _info.opengl_version, _info.opengl_vendor);
            free(_info.opengl_version); free(_info.opengl_vendor);
        }
        if (_info.has_opengles) {
            printf("OpenGL ES (mobile) version:\t %s [%s]\n", _info.opengles_version, _info.opengles_vendor);
            free(_info.opengles_version); free(_info.opengles_vendor);
        }
    }
    if (_info.has_vulkan) {
        printf("Vulkan version:\t\t\t %d.%d.%d [GPUs:%d", _info.vulkan_major, _info.vulkan_minor, _info.vulkan_patch,
                                                            _info.vulkan_gpu_count);
        for (uint32_t c = 0; c < _info.vulkan_gpu_count; c++) {
            printf(", (%s: %dMb)", _info.vulkan_gpu_names[c], _info.vulkan_gpu_rams[c]);
            free(_info.vulkan_gpu_names[c]);
        }
        if (_info.vulkan_gpu_count > 0) {
            free(_info.vulkan_gpu_names);
            free(_info.vulkan_gpu_rams);
        }
        puts("]");
    }

    // clean
    wl_compositor_destroy(_info.compositor);
    wl_registry_destroy(registry);
    wl_display_flush(display);
    wl_display_disconnect(display);

    return EXIT_SUCCESS;
}


// WL_REGISTRY CALLBACKS

void wl_interface_available(void* data, struct wl_registry* registry, uint32_t serial, const char* name, uint32_t version)
{
    printf("Interface available: name:'%s' - version:'%d'. ", name, version);

    InterfaceInfo* _info = data;

         if (!strcmp(name, "wl_shm"))                  { printf("\t\t\t [Software rendering]");                               }
    else if (!strcmp(name, "wl_seat"))                 { printf("\t\t\t [Input devices (keyboard, mouse, touch)]");           }
    else if (!strcmp(name, "wl_output"))               { printf("\t\t\t [Output devices (screens)]");                         }
    else if (!strcmp(name, "wl_data_device_manager"))  { printf("    \t [Clibpoard (copy-paste, drag-drop)]");                }
    else if (!strcmp(name, "wp_viewporter"))           { printf("  \t\t [Surface scaling]");                                  }
    else if (!strcmp(name, "wp_presentation"))         { printf("  \t\t [Precise video synchronization]");                    }
    else if (strstr(name,  "wp_idle_inhibit_manager")) { printf("\b\b\t [Screensaver inhibiter]");                            }
    else if (strstr(name,  "wp_text_input_manager"))   { printf("\b\b\t [Virtual keyboard]");                                 }
    else if (strstr(name,  "wp_pointer_constraints"))  { printf("\b\b\t [Pointer lock]");                                     }
    else if (strstr(name,  "wp_linux_dmabuf"))         { printf("    \t [DRM kernel GPU channel]");                           }
    else if (!strcmp(name, "wl_drm"))                  { printf("\t\t\t [DRM kernel GPU channel -deprecated]");               }
    else if (!strcmp(name, "wl_shell"))                { printf("\t\t\t [Standard window manager -deprecated]");              }
    else if (!strcmp(name, "xdg_wm_base"))             { printf("\t\t\t [Standard window manager]");                          }
    else if (strstr(name,  "xdg_shell"))               { printf("  \t\t [Standard window manager -unstable]");                }
    else if (strstr(name,  "gtk_shell"))               { printf("  \t\t [GNOME window manager]");      _info->id = E_GNOME;   }
    else if (strstr(name,  "plasma_shell"))            { printf("  \t\t [KDE Plasma window manager]"); _info->id = E_KDE;     }
    else if (strstr(name,  "wlr_layer_shell"))         { printf("    \t [wlroots window manager]");    _info->id = E_WLROOTS; }
    else if (strstr(name,  "weston"))                  {                                               _info->id = E_WESTON;  }
    else if (!strcmp(name, "wl_subcompositor"))        { printf("  \t\t [Sub-surfaces]");                                     }
    else if (!strcmp(name, "wl_compositor"))           { printf("  \t\t [Compositor]");                _info->compositor =
                                                     wl_registry_bind(registry, serial, &wl_compositor_interface, version);   }

    putchar('\n');
}

void wl_interface_removed(void* data, struct wl_registry* registry, uint32_t serial)
{ }


// EGL CHECK (OPENGL, OPENGL ES)

void check_egl(InterfaceInfo* _info, struct wl_display* display)
{
    EGLDisplay egl_display = eglGetDisplay((EGLNativeDisplayType) display);
    if (!egl_display) {
        return; }

    if (!eglInitialize(egl_display, &_info->egl_major, &_info->egl_minor)) {
        return; }

    _info->has_egl      = true;
    _info->egl_vendor   = (char*) eglQueryString(egl_display, EGL_VENDOR);
    _info->has_opengl   = eglBindAPI(EGL_OPENGL_API);
    _info->has_opengles = eglBindAPI(EGL_OPENGL_ES_API);

    if (!_info->has_opengl && !_info->has_opengles) {
        return; }

    if (_info->has_opengl) {
        check_egl_api(_info, egl_display, EGL_OPENGL_API); }
    
    if (_info->has_opengles) {
        check_egl_api(_info, egl_display, EGL_OPENGL_ES_API); }

    eglTerminate(egl_display);
}

void check_egl_api(InterfaceInfo* _info, EGLDisplay egl_display, EGLenum api)
{
    eglBindAPI(api);

    EGLint cfg_count = 0;
    eglGetConfigs(egl_display, NULL, 0, &cfg_count);
    if (cfg_count == 0) {
        return; }

    EGLConfig cfg, *cfgs = calloc(cfg_count, sizeof(EGLConfig));
    EGLint valid_cfg_count, valid_cfg_attrs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1, EGL_GREEN_SIZE, 1, EGL_BLUE_SIZE, 1, EGL_DEPTH_SIZE, 1,
        EGL_RENDERABLE_TYPE, (api == EGL_OPENGL_ES_API ? EGL_OPENGL_ES_BIT : EGL_OPENGL_BIT),
        EGL_NONE
    };
    eglChooseConfig(egl_display, valid_cfg_attrs, cfgs, cfg_count, &valid_cfg_count);
    if (valid_cfg_count == 0) {
        free(cfgs);
        return;
    }
    cfg = cfgs[0];
    free(cfgs);

    EGLint ctx_attrs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 1,
        EGL_NONE
    };
    EGLContext egl_ctx = eglCreateContext(egl_display, cfg, EGL_NO_CONTEXT, ctx_attrs);
    if (!egl_ctx) {
        return; }

    struct wl_surface* surface       = wl_compositor_create_surface(_info->compositor);
    struct wl_egl_window* egl_window = wl_egl_window_create(surface, 320, 240);
    EGLSurface egl_srf               = eglCreateWindowSurface(egl_display, cfg, (EGLNativeWindowType) egl_window, NULL);

    if (eglMakeCurrent(egl_display, egl_srf, egl_srf, egl_ctx)) {
        if (api == EGL_OPENGL_ES_API) {
            _info->opengles_vendor  = strdup(glGetString(GL_VENDOR));
            _info->opengles_version = strdup(glGetString(GL_VERSION));
        } else {
            _info->opengl_vendor  = strdup(glGetString(GL_VENDOR));
            _info->opengl_version = strdup(glGetString(GL_VERSION));
        }
    }

    eglDestroySurface(egl_display, egl_srf);
    wl_egl_window_destroy(egl_window);
    wl_surface_destroy(surface);
    eglDestroyContext(egl_display, egl_ctx);
}


// VULKAN CHECK

void check_vulkan(InterfaceInfo* _info)
{
    uint32_t vkext_count = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &vkext_count, NULL)
          || vkext_count == 0) {
        return; }

    VkExtensionProperties* vkexts = calloc(vkext_count, sizeof(VkExtensionProperties));
    vkEnumerateInstanceExtensionProperties(NULL, &vkext_count, vkexts);
    for (uint32_t c = 0; c < vkext_count; c++) {
        if (!strcmp(vkexts[c].extensionName, "VK_KHR_wayland_surface")) {
            _info->has_vulkan = true;
            break;
        }
    }
    free(vkexts);

    if (!_info->has_vulkan) {
        return; }

    uint32_t version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&version);
    _info->vulkan_major = VK_VERSION_MAJOR(version);
    _info->vulkan_minor = VK_VERSION_MINOR(version);
    _info->vulkan_patch = VK_VERSION_PATCH(version);

    VkInstance vkinstance = {0};
    VkInstanceCreateInfo vkinfo = {0};
    vkinfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    if (vkCreateInstance(&vkinfo, NULL, &vkinstance)) {
        return; }

    vkEnumeratePhysicalDevices(vkinstance, &_info->vulkan_gpu_count, NULL);
    if (_info->vulkan_gpu_count == 0) {
       vkDestroyInstance(vkinstance, NULL);
       return;
    }

    _info->vulkan_gpu_names = calloc(_info->vulkan_gpu_count, sizeof(char*));
    _info->vulkan_gpu_rams  = calloc(_info->vulkan_gpu_count, sizeof(uint32_t));

    VkPhysicalDevice* vkgpus = calloc(_info->vulkan_gpu_count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vkinstance, &_info->vulkan_gpu_count, vkgpus);
    for (uint32_t c = 0; c < _info->vulkan_gpu_count; c++) {
        VkPhysicalDeviceProperties vkgpu_props = {0};
        VkPhysicalDeviceMemoryProperties vkgpu_memprops = {0};

        vkGetPhysicalDeviceProperties(vkgpus[c], &vkgpu_props);
        vkGetPhysicalDeviceMemoryProperties(vkgpus[c], &vkgpu_memprops);
        _info->vulkan_gpu_names[c] = strdup(vkgpu_props.deviceName);
        _info->vulkan_gpu_rams[c]  = vkgpu_memprops.memoryHeaps[0].size / 1000000;
    }
    free(vkgpus);

    vkDestroyInstance(vkinstance, NULL);
}
