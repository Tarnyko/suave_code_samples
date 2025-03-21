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
    char*                 name;
    VkPhysicalDevice      vk_gpu;          // pre-uninitialized device
    VkDevice              vk_device;       // main Vulkan device

    VkQueue               vk_queue;
    VkCommandPool         vk_cmdpool;      // Draw commands go through...
    VkCommandBuffer       vk_cmdbuffer;    // ...1-n Command Buffers.

    VkSwapchainKHR        vk_swapchain;
    VkRenderPass          vk_renderpass;
    VkImageView*          vk_views;        // Framebuffers own Views...
    VkFramebuffer*        vk_framebuffers; // ...and there are 1-n...
    uint32_t              swap_imgcount;   // ...as by this counter.

    VkDescriptorPool      vk_descpool;     // All this state used at
    VkDescriptorSetLayout vk_desclayout;   // draw time, with 'vk_desc'
    VkDescriptorSet       vk_desc;         // as the main entry point.
    VkPipelineLayout      vk_layout;
    VkPipeline            vk_pipeline;     // Shaders live there

    VkFormat              vk_chosen_format;
    VkColorSpaceKHR       vk_chosen_colorspace;
} Gpu;

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
    Gpu                   gpu;           //         - GPU
    bool has_vulkan;
} InterfaceInfo;


char* elect_shell(InterfaceInfo*);
void* create_shell_surface(InterfaceInfo*, struct wl_surface*, char*);
void destroy_shell_surface(InterfaceInfo*, void*);

void initialize_vulkan(InterfaceInfo*, char*);
bool elect_vulkan_gpu(InterfaceInfo*, VkInstance, VkPhysicalDevice);
bool initialize_vulkan_gpu(InterfaceInfo*, VkDevice, uint32_t);

bool initialize_vulkan_renderpass(InterfaceInfo*, VkSurfaceKHR, int*, int*);
bool initialize_vulkan_pipeline(InterfaceInfo*, VkRenderPass);

Window* create_window(InterfaceInfo*, char*, int, int);
bool redraw_window(InterfaceInfo*, Window*);
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
    initialize_vulkan(&_info, argv[0]);
    if (!_info.has_vulkan) {
        fprintf(stderr, "No compatible Vulkan implementation found! Exiting...\n");
        goto error;
    }

    // MAIN LOOP!
    {
        Window* window = create_window(&_info, argv[0], 320, 240);

        printf("\nLooping...\n\n");

        while (loop_result != -1) {
            loop_result = wl_display_dispatch_pending(display);
            loop_result = redraw_window(&_info, window) ? loop_result : -1;
        }

        destroy_window(&_info, window);
    }

    goto end;


  error:
    result = EXIT_FAILURE;
  end:
    if (_info.has_vulkan) {
        Gpu* gpu = &_info.gpu;
        for (uint32_t c = 0; c < gpu->swap_imgcount; c++) {
            vkDestroyFramebuffer(gpu->vk_device, gpu->vk_framebuffers[c], NULL);
            vkDestroyImageView(gpu->vk_device, gpu->vk_views[c], NULL);
        }
        free(gpu->vk_framebuffers);
        free(gpu->vk_views);
        vkDestroyPipeline(gpu->vk_device, gpu->vk_pipeline, NULL);
        vkDestroyPipelineLayout(gpu->vk_device, gpu->vk_layout, NULL);
        vkDestroyDescriptorPool(gpu->vk_device, gpu->vk_descpool, NULL);
        vkDestroyDescriptorSetLayout(gpu->vk_device, gpu->vk_desclayout, NULL);
        vkDestroyRenderPass(gpu->vk_device, gpu->vk_renderpass, NULL);
        vkDestroySwapchainKHR(gpu->vk_device, gpu->vk_swapchain, NULL);
        vkFreeCommandBuffers(gpu->vk_device, gpu->vk_cmdpool, 1, &gpu->vk_cmdbuffer);
        vkDestroyCommandPool(gpu->vk_device, gpu->vk_cmdpool, NULL);
        vkDestroyDevice(gpu->vk_device, NULL);
        free(gpu->name);
        vkDestroyInstance(_info.vk_instance, NULL);
    }
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


void initialize_vulkan(InterfaceInfo* _info, char* arg)
{
    // Check if instance supports Wayland

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

    uint32_t vulkan_version = VK_API_VERSION_1_0;
    vkEnumerateInstanceVersion(&vulkan_version);

    uint32_t vulkan_major = VK_VERSION_MAJOR(vulkan_version),
             vulkan_minor = VK_VERSION_MINOR(vulkan_version),
             vulkan_patch = VK_VERSION_PATCH(vulkan_version);

    printf("Vulkan version: %d.%d.%d ", vulkan_major, vulkan_minor, vulkan_patch);

    // Create instance

    VkInstanceCreateInfo info = {0};
    info = (VkInstanceCreateInfo) {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount   = 2,
        .ppEnabledExtensionNames = (const char*[]) {"VK_KHR_surface", "VK_KHR_wayland_surface" }
    };

    VkInstance vk_instance;
    if (vkCreateInstance(&info, NULL, &vk_instance)) {
        return; }

    // Enumerate GPUs...

    uint32_t vkgpu_count = 0;

    vkEnumeratePhysicalDevices(vk_instance, &vkgpu_count, NULL);
    if (vkgpu_count == 0) {
       vkDestroyInstance(vk_instance, NULL);
       return;
    }

    printf("[GPUs:%d", vkgpu_count);

    // ...and choose a valid one

    VkPhysicalDevice *vkgpus = calloc(vkgpu_count, sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(vk_instance, &vkgpu_count, vkgpus);
    for (uint32_t c = 0; c < vkgpu_count; c++)
    {
        VkPhysicalDeviceProperties vkgpu_props = {0};
        VkPhysicalDeviceMemoryProperties vkgpu_memprops = {0};

        vkGetPhysicalDeviceProperties(vkgpus[c], &vkgpu_props);
        vkGetPhysicalDeviceMemoryProperties(vkgpus[c], &vkgpu_memprops);

        printf(", (%s: %ldMb)", vkgpu_props.deviceName, vkgpu_memprops.memoryHeaps[0].size / 1000000);

        if (elect_vulkan_gpu(_info, vk_instance, vkgpus[c])) {
            _info->gpu.name = strdup(vkgpu_props.deviceName); }
    }
    free(vkgpus);
    printf("]\n\n");

    if (!_info->gpu.name) {
       fprintf(stderr, "Vulkan works, but no valid GPU found!\n");
       vkDestroyInstance(vk_instance, NULL);
       return;
    }
    printf("Chosen GPU: %s\n\n", _info->gpu.name);

    _info->has_vulkan = true;
    _info->vk_instance = vk_instance;
}

bool elect_vulkan_gpu(InterfaceInfo* _info, VkInstance vk_instance, VkPhysicalDevice vk_gpu)
{
    // Check if GPU supports the 'swapchain' extension

    uint32_t vkgpuext_count = 0;

    if (vkEnumerateDeviceExtensionProperties(vk_gpu, NULL, &vkgpuext_count, NULL)
          || vkgpuext_count == 0) {
        return false; }

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
        return false; }

    // Check if the GPU has a queue with both 'graphics' and 'present' capabilities...

    uint32_t vkqueue_count = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(vk_gpu, &vkqueue_count, NULL);
    if (vkqueue_count == 0) {
        return false; }

    // ... for this, we need to create a temporary VkSurface...

    VkWaylandSurfaceCreateInfoKHR createinfo = {0};
    createinfo = (VkWaylandSurfaceCreateInfoKHR) {
        .sType   = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .display = _info->display,
        .surface = NULL
    };

    struct wl_surface* surface = wl_compositor_create_surface(_info->compositor);
    createinfo.surface = surface;

    VkSurfaceKHR vk_surface;
    if (vkCreateWaylandSurfaceKHR (vk_instance, &createinfo, NULL, &vk_surface)) {
       wl_surface_destroy(surface);
       return false;
    }

    // (...while we're at it, use it to elect surface format & colorspace...)

    VkFormat        vk_format;
    VkColorSpaceKHR vk_colorspace;

    uint32_t vkformat_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_gpu, vk_surface, &vkformat_count, NULL);

    VkSurfaceFormatKHR* vkformats = calloc(vkformat_count, sizeof(VkSurfaceFormatKHR));
    vkGetPhysicalDeviceSurfaceFormatsKHR(vk_gpu, vk_surface, &vkformat_count, vkformats);
    for (uint32_t c = 0; c < vkformat_count; c++) {
        vk_format     = vkformats[c].format;
	vk_colorspace = vkformats[c].colorSpace;

	if (vk_format == VK_FORMAT_R8G8B8A8_UNORM) {
            break; }
    }
    free(vkformats);

    _info->gpu.vk_chosen_format     = vk_format;
    _info->gpu.vk_chosen_colorspace = vk_colorspace;

    // ...and then look for a valid queue index

    uint32_t queue_index;
    VkBool32 queue_has_present = false;

    VkQueueFamilyProperties* vkqueue_props = calloc(vkqueue_count, sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties (vk_gpu, &vkqueue_count, vkqueue_props);
    for (uint32_t c = 0; c < vkqueue_count; c++) {
	if ((vkqueue_props[c].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
              !vkGetPhysicalDeviceSurfaceSupportKHR(vk_gpu, c, vk_surface, &queue_has_present) &&
	        queue_has_present) {
	    queue_index = c;
	    break;
        }
    }
    free(vkqueue_props);

    vkDestroySurfaceKHR(vk_instance, vk_surface, NULL);
    wl_surface_destroy(surface);

    if (!queue_has_present) {
        return false; }

    // Finally, create the device with 'swapchain' extension & chosen queue

    VkDeviceCreateInfo vkdeviceinfo = {0};
    vkdeviceinfo = (VkDeviceCreateInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = (const char*[]) { "VK_KHR_swapchain" },
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = (VkDeviceQueueCreateInfo[]) {
                                       { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                         .queueFamilyIndex = queue_index,
                                         .queueCount       = 1,
                                         .pQueuePriorities = (float[]) { 0.0 } }
                                   }
    };

    VkDevice vk_device;
    if (vkCreateDevice(vk_gpu, &vkdeviceinfo, NULL, &vk_device)) {
        return false; }

    if (!initialize_vulkan_gpu(_info, vk_device, queue_index)) {
        return false; }

    _info->gpu.vk_gpu      = vk_gpu;
    _info->gpu.vk_device   = vk_device;

    return true;
}

bool initialize_vulkan_gpu(InterfaceInfo* _info, VkDevice vk_device, uint32_t queue_index)
{
    VkQueue vk_queue;
    vkGetDeviceQueue(vk_device, queue_index, 0, &vk_queue);

    VkCommandPoolCreateInfo cmdpoolinfo = {0};
    cmdpoolinfo = (VkCommandPoolCreateInfo) {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_index,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    VkCommandPool vk_cmdpool;
    if (vkCreateCommandPool(vk_device, &cmdpoolinfo, NULL, &vk_cmdpool)) {
        return false; }

    VkCommandBufferAllocateInfo cmdbufferinfo = {0};
    cmdbufferinfo = (VkCommandBufferAllocateInfo) {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = vk_cmdpool,
        .commandBufferCount = 1,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY
    };

    VkCommandBuffer vk_cmdbuffer;
    if (vkAllocateCommandBuffers(vk_device, &cmdbufferinfo, &vk_cmdbuffer)) {
        vkDestroyCommandPool(vk_device, vk_cmdpool, NULL);
        return false;
    }

    _info->gpu.vk_queue     = vk_queue;
    _info->gpu.vk_cmdpool   = vk_cmdpool;
    _info->gpu.vk_cmdbuffer = vk_cmdbuffer;

    return true;
}

bool initialize_vulkan_renderpass(InterfaceInfo* _info, VkSurfaceKHR vk_surface, int* width, int* height)
{
    VkSurfaceCapabilitiesKHR surfcaps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(_info->gpu.vk_gpu, vk_surface, &surfcaps);

    // If returned width/height are -1, we can force our desired size...
    // ... otherwise, we are required to use the provided one.

    VkExtent2D swap_extent;
    if (surfcaps.currentExtent.width == -1 || surfcaps.currentExtent.width == -1) {
        swap_extent.width  = *width;
	swap_extent.height = *height;
    } else {
        swap_extent = surfcaps.currentExtent;
	printf("We are required to use window size: %dx%d\n", swap_extent.width, swap_extent.height);
    }

    // How many images does the GPU want per swapchain?

    uint32_t swap_imgcount = (surfcaps.maxImageCount > 0) ?
                               surfcaps.maxImageCount : surfcaps.minImageCount + 1;

    // What is its preferred transform mode?

    VkSurfaceTransformFlagsKHR swap_transform
                       = (surfcaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ?
                           VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : surfcaps.currentTransform;

    // Create the swapchain

    VkSwapchainCreateInfoKHR swapinfo = {0};
    swapinfo = (VkSwapchainCreateInfoKHR) {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = vk_surface,
        .imageFormat      = _info->gpu.vk_chosen_format,
        .imageColorSpace  = _info->gpu.vk_chosen_colorspace,
        .imageExtent      = swap_extent,
        .minImageCount    = swap_imgcount,
        .preTransform     = swap_transform,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,   // 'color' base attachment
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .imageArrayLayers = 1,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = true
    };

    VkSwapchainKHR vk_swapchain;
    if (vkCreateSwapchainKHR(_info->gpu.vk_device, &swapinfo, NULL, &vk_swapchain)) {
	    return false; }

    // Pre-create the imageviews that the swapchain will use:

    vkGetSwapchainImagesKHR(_info->gpu.vk_device, vk_swapchain, &swap_imgcount, NULL);
    printf("Number of images per swapchain: %d ", swap_imgcount);

    // 1) create a view for each of the swapchain's 'color' images...

    VkImageViewCreateInfo viewinfo = {0};
    viewinfo = (VkImageViewCreateInfo) {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = swapinfo.imageFormat,
        .image      = NULL,    // we will assign it in the loop below
        .components = (VkComponentMapping) { .r = VK_COMPONENT_SWIZZLE_R,
                                             .g = VK_COMPONENT_SWIZZLE_G,
                                             .b = VK_COMPONENT_SWIZZLE_B,
                                             .a = VK_COMPONENT_SWIZZLE_A },
        .subresourceRange = (VkImageSubresourceRange) { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                        .levelCount = 1,
                                                        .layerCount = 1 }
    };

    VkImageView* vk_views = calloc(swap_imgcount, sizeof(VkImageView));

    VkImage* swapimages = calloc(swap_imgcount, sizeof(VkImage));
    vkGetSwapchainImagesKHR(_info->gpu.vk_device, vk_swapchain, &swap_imgcount, swapimages);

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(_info->gpu.vk_device, swapimages[0], &mem_reqs);
    printf("(*%zd = %zd bytes)\n", mem_reqs.size, swap_imgcount * mem_reqs.size);

    for (uint32_t c = 0; c < swap_imgcount; c++) {
        viewinfo.image = swapimages[c];

        if (vkCreateImageView(_info->gpu.vk_device, &viewinfo, NULL, &vk_views[c])) {
            free(swapimages);
            free(vk_views);
            vkDestroySwapchainKHR(_info->gpu.vk_device, vk_swapchain, NULL);
            return false;
        }
    }
    free(swapimages);

    // Normally we'd then prepare more images/views for 2) depth 3) textures...

    // 4) Create the renderpass

    VkRenderPassCreateInfo renderinfo = {0};
    renderinfo = (VkRenderPassCreateInfo) {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,    // only 1 'color' attachment
        .pAttachments    = (VkAttachmentDescription[]) {
		               { .format         = swapinfo.imageFormat,
                                 .samples        = VK_SAMPLE_COUNT_1_BIT,
                                 .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                 .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                 .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                 .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                 .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
                           },
        .subpassCount = 1,
        .pSubpasses = (VkSubpassDescription[]) {
                          { .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
                            .preserveAttachmentCount = 0,
                            .inputAttachmentCount    = 0,
                            .colorAttachmentCount    = 1,
                            .pColorAttachments
                                = (VkAttachmentReference[]) {
                                      { .attachment = 0,   // index 1
                                        .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL }
                                  },
                            .pDepthStencilAttachment = NULL  } // empty for now

                      },
    };

    VkRenderPass vk_renderpass;
    assert(!vkCreateRenderPass(_info->gpu.vk_device, &renderinfo, NULL, &vk_renderpass));

    // 5) Create a framebuffer for each view, using the renderpass to render them

    VkFramebufferCreateInfo fbinfo = {0};
    fbinfo = (VkFramebufferCreateInfo) {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = vk_renderpass,
        .attachmentCount = 1,      // only 'color' for now
        .pAttachments    = NULL,   // we will assign it in the loop below
        .width           = swap_extent.width,
        .height          = swap_extent.height,
        .layers          = 1
    };

    VkFramebuffer* vk_framebuffers = calloc(swap_imgcount, sizeof(VkFramebuffer));
    for(uint32_t c = 0; c < swap_imgcount; c++) {
        fbinfo.pAttachments = (VkImageView[]) { vk_views[c] };
        assert(!vkCreateFramebuffer(_info->gpu.vk_device, &fbinfo, NULL, &vk_framebuffers[c]));
    }

    _info->gpu.vk_swapchain    = vk_swapchain;
    _info->gpu.vk_renderpass   = vk_renderpass;
    _info->gpu.vk_framebuffers = vk_framebuffers;
    _info->gpu.vk_views        = vk_views;
    _info->gpu.swap_imgcount   = swap_imgcount;

    *width  = swap_extent.width;
    *height = swap_extent.height;

    return true;
 }

bool initialize_vulkan_pipeline(InterfaceInfo* _info, VkRenderPass vk_renderpass)
{
    // 5) Create the pipeline ([cache, (descriptor -> layout), shader -> pipeline])

    VkPipelineCacheCreateInfo cacheinfo = {0};
    cacheinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

    VkPipelineCache vk_cache;
    if (vkCreatePipelineCache(_info->gpu.vk_device, &cacheinfo, NULL, &vk_cache)) {
        return false; }

    // layout with mandatory sampler

    VkSampler vk_sampler;

    VkDescriptorSetLayoutCreateInfo desclayoutinfo = {0};
    desclayoutinfo = (VkDescriptorSetLayoutCreateInfo) {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
	.pBindings    = (VkDescriptorSetLayoutBinding[]) {
                            { .binding            = 0,
                              .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              .descriptorCount    = 1,
                              .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                              .pImmutableSamplers = &vk_sampler }
                        }
    };

    VkDescriptorSetLayout vk_desclayout;
    if (vkCreateDescriptorSetLayout(_info->gpu.vk_device, &desclayoutinfo, NULL, &vk_desclayout)) {
	vkDestroyPipelineCache(_info->gpu.vk_device, vk_cache, NULL);
        return false;
    }

    VkPipelineLayoutCreateInfo layoutinfo = {0};
    layoutinfo = (VkPipelineLayoutCreateInfo) {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &vk_desclayout
    };

    VkPipelineLayout vk_layout;
    if (vkCreatePipelineLayout(_info->gpu.vk_device, &layoutinfo, NULL, &vk_layout)) {
	vkDestroyDescriptorSetLayout(_info->gpu.vk_device, vk_desclayout, NULL);
	vkDestroyPipelineCache(_info->gpu.vk_device, vk_cache, NULL);
        return false;
    }

    // pipeline

    VkGraphicsPipelineCreateInfo pipelineinfo = {0};
    pipelineinfo = (VkGraphicsPipelineCreateInfo) {
        .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .renderPass = vk_renderpass,
        .layout     = vk_layout,
        .stageCount = 1,   // (0-2 stages: vertex shader, fragment shader)
        .pStages    = (VkPipelineShaderStageCreateInfo[]) { // REQUIRED EVEN WITHOUT SHADER!
                          { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            .stage    = VK_SHADER_STAGE_VERTEX_BIT }
                      }
    };

    VkPipeline vk_pipeline;
    assert(!vkCreateGraphicsPipelines(_info->gpu.vk_device, vk_cache, 0, &pipelineinfo,
                                      NULL, &vk_pipeline));

    // cache is not needed anymore
    vkDestroyPipelineCache(_info->gpu.vk_device, vk_cache, NULL);

    // [all this (descriptor set pool) will be required later at runtime]

    VkDescriptorPoolCreateInfo descpoolinfo = {0};
    descpoolinfo = (VkDescriptorPoolCreateInfo) {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes = (VkDescriptorPoolSize[]) {
                          { .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            .descriptorCount = 1 }
                      }
    };

    VkDescriptorPool vk_descpool;
    assert(!vkCreateDescriptorPool(_info->gpu.vk_device, &descpoolinfo, NULL, &vk_descpool));

    VkDescriptorSetAllocateInfo descinfo = {0};
    descinfo = (VkDescriptorSetAllocateInfo) {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = vk_descpool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &vk_desclayout
    };


    VkDescriptorSet vk_desc;
    vkAllocateDescriptorSets(_info->gpu.vk_device, &descinfo, &vk_desc);
    VkWriteDescriptorSet descwrite = {0};
    descwrite = (VkWriteDescriptorSet) {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = vk_desc,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,   // 1 image
        .pImageInfo      = (VkDescriptorImageInfo[]) {
                               { .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                 .imageView   = _info->gpu.vk_views[0] }
                           }
    };
    vkUpdateDescriptorSets(_info->gpu.vk_device, 1, &descwrite, 0, NULL);

    _info->gpu.vk_desclayout = vk_desclayout;
    _info->gpu.vk_descpool   = vk_descpool;
    _info->gpu.vk_desc       = vk_desc;

    _info->gpu.vk_layout   = vk_layout;
    _info->gpu.vk_pipeline = vk_pipeline;

    return true;
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

    if (!initialize_vulkan_renderpass(_info, window->vk_surface, &window->width,
                                                                 &window->height)) {
        fprintf(stderr, "Could not create Vulkan framebuffers");
        return NULL;
    }
    if (!initialize_vulkan_pipeline(_info, _info->gpu.vk_renderpass)) {
        fprintf(stderr, "Could not create Vulkan pipeline!");
        return NULL;
    }

    window->shell_surface = create_shell_surface(_info, window->surface, title);
    assert(window->shell_surface);

    // [/!\ xdg-wm-base/shell expects a commit before redrawing (/!\)]
    wl_surface_commit(window->surface);

    // [/!\ xdg-wm-base expects a configure event before redrawing (/!\)]
    wl_display_roundtrip(_info->display);

    return window;
}

bool redraw_window(InterfaceInfo* _info, Window* window)
{
    Gpu* gpu = &_info->gpu;

    VkClearValue clear[1];             // redraw to White
    clear[0].color.float32[0] = 1.0;   // R
    clear[0].color.float32[1] = 1.0;   // G
    clear[0].color.float32[2] = 1.0;   // B
    clear[0].color.float32[3] = 0.0;   // A

    VkSemaphoreCreateInfo seminfo = {0};
    seminfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkSemaphore sem;
    assert(!vkCreateSemaphore(gpu->vk_device, &seminfo, NULL, &sem));

    // get one the 'swap_imgcount' images waiting into the swapchain
    uint32_t img_index;
    vkAcquireNextImageKHR(gpu->vk_device, gpu->vk_swapchain, 1000000000, sem,
                          NULL, &img_index);

    VkRenderPassBeginInfo begininfo = {0};
    begininfo = (VkRenderPassBeginInfo) {
        .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass               = gpu->vk_renderpass,
        .framebuffer              = gpu->vk_framebuffers[img_index],
        .renderArea.extent.width  = window->width,
        .renderArea.extent.height = window->height,
        .clearValueCount          = 1,   // only 'color'
        .pClearValues             = clear
    };

    // add a draw command to the command buffer
    vkCmdBeginRenderPass(gpu->vk_cmdbuffer, &begininfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(gpu->vk_cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gpu->vk_layout,
                            0, 1, &gpu->vk_desc, 0, NULL);
    vkCmdDraw(gpu->vk_cmdbuffer, 0, 1, 0, 0);
    vkCmdEndRenderPass(gpu->vk_cmdbuffer);

    // lock the command buffer
    vkEndCommandBuffer(gpu->vk_cmdbuffer);

    // use a semaphore + fence to secure the rendering
    VkFenceCreateInfo fenceinfo = {0};
    fenceinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence;
    assert(!vkCreateFence(gpu->vk_device, &fenceinfo, NULL, &fence));

    VkSubmitInfo submitinfo = {0};
    submitinfo = (VkSubmitInfo) {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pWaitDstStageMask  = (VkPipelineStageFlags[]) {
		                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &sem,
	.commandBufferCount = 1,
	.pCommandBuffers    = &gpu->vk_cmdbuffer
    };

    // queue the command buffer content for execution
    assert(!vkQueueSubmit(gpu->vk_queue, 1, &submitinfo, fence));

    VkPresentInfoKHR presentinfo = {0};
    presentinfo = (VkPresentInfoKHR) {
        .sType          = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains    = &gpu->vk_swapchain,
        .pImageIndices  = &img_index
    };

    // wait until the state is ready
    VkResult res;
    do { res = vkWaitForFences(gpu->vk_device, 1, &fence, VK_TRUE, 1000000000);
       } while (res == VK_TIMEOUT);

    // present the image into the window!
    vkQueuePresentKHR(gpu->vk_queue, &presentinfo);

    vkDestroyFence(gpu->vk_device, fence, NULL);
    vkDestroySemaphore(gpu->vk_device, sem, NULL);

    return (img_index < gpu->swap_imgcount);
}

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

