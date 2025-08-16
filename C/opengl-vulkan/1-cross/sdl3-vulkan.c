/*
* sdl3-vulkan.c
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

//  Compile with:
// gcc -std=c11 ... `pkg-config --cflags --libs sdl3 vulkan`

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#define _IMAGE_RGBA8888 VK_FORMAT_R8G8B8A8_UNORM


#define LINES       2
#define INIT_WIDTH  800
#define INIT_HEIGHT 600


static unsigned int _width  = INIT_WIDTH;
static unsigned int _height = INIT_HEIGHT;

 /* WORKING AREA : 
  * [ -1.0;+1.0    +1.0;+1.0 ]
  * [ -1.0;-1.0    +1.0;-1.0 ]
  */
static const float vertex_arr[LINES * 4] = {   // 1 line: 2 points * 2 coordinates ([-1.0f;1.0f])
    -0.8f,  0.8f,    0.8f, -0.8f,                // - line 1 (\)
    -0.8f, -0.8f,    0.8f,  0.8f,                // - line 2 (/)
};

static const unsigned char color_arr[LINES * 8] = {    // 1 line: 2 points * 4 colors ([R,G,B,A])
    255, 0,   0, 255,      0, 255,   0, 255,     // - color 1 (Red->Green)
      0, 0, 255, 255,    255, 255, 255, 255,     // - color 2 (Blue->White)
};

static const unsigned int index_arr[LINES * 2] = {     // 1 line: 2 points ON 2 selected colors
    0, 1,
    2, 3,
};

static const char *vertex_shader =
    "#version 300 es                        \n"
    "                                       \n"
    "layout(location=0) in vec4 p_position; \n" // 1st attribute: ID 0
    "layout(location=1) in vec4 p_color;    \n" // 2nd attribute: ID 1
    "out vec4 v_color;                      \n"
    "                                       \n"
    "void main()                            \n"
    "{                                      \n"
    "  v_color = p_color;                   \n"
    "  gl_Position = p_position;            \n"  // (builtin)
    "}                                      \n";

static const char *color_shader =
    "#version 300 es                  \n"
    "precision mediump float;         \n"
    "                                 \n"
    "in vec4 v_color;                 \n"
    "out vec4 frag_color;             \n"
    "                                 \n"
    "void main()                      \n"
    "{                                \n"
    "  frag_color = v_color;          \n"
    "}                                \n";



void redraw(SDL_Window* window, VkDevice gpu, VkQueue queue, VkSwapchainKHR swapchain, VkRenderPass renderpass,
		                VkCommandBuffer cmdbuffer, VkPipelineLayout layout, VkDescriptorSet desc_set,
				VkFramebuffer buffer)
{
    static VkClearValue clear = {0};
    VkSemaphore sem;
    VkFence fence;
    VkResult ready;

    // increment R,G,B by 0.01 each iteration
    float* float32 = clear.color.float32;
    for (int c = 0; c < 3; c++) {
        float32[c] = (float32[c] < 1.0) ? float32[c] + 0.01 : 0.0; }

    // Semaphore: create
    VkSemaphoreCreateInfo seminfo = {0};
    seminfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCreateSemaphore(gpu, &seminfo, NULL, &sem);
    // Fence: create
    VkFenceCreateInfo fenceinfo = {0};
    fenceinfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCreateFence(gpu, &fenceinfo, NULL, &fence);

    // 1) get next image
    uint32_t image_idx;
    vkAcquireNextImageKHR(gpu, swapchain, 1000000000, sem,
                          NULL, &image_idx);

    // 2) put its corresponding ImageView/FrameBuffer into the CommandBuffer
    VkRenderPassBeginInfo begininfo = {0};
    begininfo = (VkRenderPassBeginInfo) {
        .sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass               = renderpass,
        .framebuffer              = buffer,
        .renderArea.extent.width  = _width,
        .renderArea.extent.height = _height,
        .clearValueCount          = 1,   // only 'color'
        .pClearValues             = &clear
    };
    vkCmdBeginRenderPass(cmdbuffer, &begininfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindDescriptorSets(cmdbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                            0, 1, &desc_set, 0, NULL);
    vkCmdDraw(cmdbuffer, 0, 1, 0, 0);
    vkCmdEndRenderPass(cmdbuffer);
    vkEndCommandBuffer(cmdbuffer);   // lock the command buffer

    // 3) ask the CommandBuffer to process it
    VkSubmitInfo submitinfo = {0};
    submitinfo = (VkSubmitInfo) {
        .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pWaitDstStageMask  = (VkPipelineStageFlags[]) {
             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT },
        .waitSemaphoreCount = 1,
        .pWaitSemaphores    = &sem,
        .commandBufferCount = 1,
        .pCommandBuffers    = &cmdbuffer
    };
    vkQueueSubmit(queue, 1, &submitinfo, fence);

    // 4) Wait until the state is ready to Present it
    VkPresentInfoKHR presentinfo = {0};
    presentinfo = (VkPresentInfoKHR) {
        .sType          = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .swapchainCount = 1,
        .pSwapchains    = &swapchain,
        .pImageIndices  = &image_idx
    };
    do { ready = vkWaitForFences(gpu, 1, &fence, VK_TRUE, 1000000000);
    } while (ready == VK_TIMEOUT);
    vkQueuePresentKHR(queue, &presentinfo);

    // Fence: end
    // Semaphore: end
    vkDestroyFence(gpu, fence, NULL);
    vkDestroySemaphore(gpu, sem, NULL);
}


bool initialize_vulkan(const char* const* ext_names, uint32_t ext_count, VkInstance* inst)
{
    VkInstanceCreateInfo instinfo = {0};
    instinfo = (VkInstanceCreateInfo) {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .enabledExtensionCount   = ext_count,
        .ppEnabledExtensionNames = ext_names,
    };
    return !vkCreateInstance(&instinfo, NULL, inst);
}

bool prepare_vulkan_gpu(VkPhysicalDevice dev, uint32_t queue_idx, VkDevice *gpu,
                                                                  VkQueue *queue,
                                                                  VkCommandPool *pool)
{
    VkDeviceCreateInfo devinfo = {0};
    devinfo = (VkDeviceCreateInfo) {
        .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount   = 1,
        .ppEnabledExtensionNames = (const char*[]) { "VK_KHR_swapchain" },
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = (VkDeviceQueueCreateInfo[]) {
                                       { .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                         .queueFamilyIndex = queue_idx,
                                         .queueCount       = 1,
                                         .pQueuePriorities = (float[]) { 0.0 } }
                                   }
    };
    assert(!vkCreateDevice(dev, &devinfo, NULL, gpu));

    VkCommandPoolCreateInfo poolinfo = {0};
    poolinfo = (VkCommandPoolCreateInfo) {
        .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = queue_idx,
        .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };
    assert (!vkCreateCommandPool(*gpu, &poolinfo, NULL, pool));

    vkGetDeviceQueue(*gpu, queue_idx, 0, queue);

    return true;
}

bool initialize_vulkan_gpu(VkInstance inst, VkDevice* gpu, VkQueue* queue, VkCommandPool* pool)
{
    bool found = false;

    uint32_t dev_count = 0;
    assert(!vkEnumeratePhysicalDevices(inst, &dev_count, NULL));
    assert(dev_count > 0);

    VkPhysicalDevice* devs = calloc(dev_count, sizeof(VkPhysicalDevice));
    assert(!vkEnumeratePhysicalDevices(inst, &dev_count, devs));

    for (uint32_t idx = 0; idx < dev_count; idx++)
    {
        uint32_t queue_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[idx], &queue_count, NULL);

        for (uint32_t qidx = 0; qidx < queue_count; qidx++) {
            if (SDL_Vulkan_GetPresentationSupport(inst, devs[idx], qidx) &&
                ((found = prepare_vulkan_gpu(devs[idx], qidx, gpu, queue, pool)))) {
                break;
            }
	}
    }

    free(devs);

    return found;
}

bool initialize_vulkan_pipeline(VkDevice gpu, VkCommandPool pool, VkSurfaceKHR surface,
                                VkSwapchainKHR* swapchain, VkRenderPass* renderpass,
                                VkCommandBuffer* cmdbuffer)
{
    VkExtent2D extent = {0};
    extent.width = INIT_WIDTH;
    extent.height = INIT_HEIGHT;

    VkSwapchainCreateInfoKHR swapinfo = {0};
    swapinfo = (VkSwapchainCreateInfoKHR) {
        .sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface          = surface,
        .imageFormat      = _IMAGE_RGBA8888,
        .imageColorSpace  = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        .imageExtent      = extent,
        .minImageCount    = 1,
        .preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .imageArrayLayers = 1,
        .presentMode      = VK_PRESENT_MODE_FIFO_KHR,
        .clipped          = true
    };
    assert(!vkCreateSwapchainKHR(gpu, &swapinfo, NULL, swapchain));

    VkCommandBufferAllocateInfo cmdbufferinfo = {0};
    cmdbufferinfo = (VkCommandBufferAllocateInfo) {
        .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool        = pool,
        .commandBufferCount = 1,
        .level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY
    };
    assert(!vkAllocateCommandBuffers(gpu, &cmdbufferinfo, cmdbuffer));

    VkRenderPassCreateInfo renderinfo = {0};
    renderinfo = (VkRenderPassCreateInfo) {
        .sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,    // only 1 'color' attachment
        .pAttachments    = (VkAttachmentDescription[]) {
                               { .format         = _IMAGE_RGBA8888,
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
    assert(!vkCreateRenderPass(gpu, &renderinfo, NULL, renderpass));

    return true;
}

bool initialize_vulkan_shaders(VkDevice gpu, VkRenderPass renderpass, VkImageView view,
                               VkPipelineLayout* layout, VkDescriptorSet* desc_set)
{
    VkPipelineCache cache;
    VkSampler sampler;
    VkDescriptorSetLayout desc_layout;
    VkPipeline pipeline;
    VkDescriptorPool desc_pool;
    //VkDescriptorSet desc_set;

    VkPipelineCacheCreateInfo cacheinfo = {0};
    cacheinfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    assert(!vkCreatePipelineCache(gpu, &cacheinfo, NULL, &cache));

    VkSamplerCreateInfo samplerinfo = {0};
    samplerinfo = (VkSamplerCreateInfo) {
        .sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter    = VK_FILTER_NEAREST,
        .minFilter    = VK_FILTER_NEAREST,
        .mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
    };
    assert(!vkCreateSampler(gpu, &samplerinfo, NULL, &sampler));

    VkDescriptorSetLayoutCreateInfo desc_layoutinfo = {0};
    desc_layoutinfo = (VkDescriptorSetLayoutCreateInfo) {
        .sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings    = (VkDescriptorSetLayoutBinding[]) {
                            { .binding            = 0,
                              .descriptorType     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              .descriptorCount    = 1,
                              .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                              .pImmutableSamplers = &sampler }
                        },
    };
    assert(!vkCreateDescriptorSetLayout(gpu, &desc_layoutinfo, NULL, &desc_layout));

    VkPipelineLayoutCreateInfo layoutinfo = {0};
    layoutinfo = (VkPipelineLayoutCreateInfo) {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts    = &desc_layout
    };
    assert(!vkCreatePipelineLayout(gpu, &layoutinfo, NULL, layout)); // Here

    VkGraphicsPipelineCreateInfo pipelineinfo = {0};
    pipelineinfo = (VkGraphicsPipelineCreateInfo) {
        .sType      = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .renderPass = renderpass,
        .layout     = *layout,
        .stageCount = 1,   // (0-2 stages: vertex shader, fragment shader)
        .pStages    = (VkPipelineShaderStageCreateInfo[]) { // REQUIRED EVEN WITHOUT SHADER!
                          { .sType    = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            .stage    = VK_SHADER_STAGE_VERTEX_BIT }
                      }
    };
    assert(!vkCreateGraphicsPipelines(gpu, cache, 0, &pipelineinfo, NULL, &pipeline));

    // cache is not needed anymore
    vkDestroyPipelineCache(gpu, cache, NULL);

    VkDescriptorPoolCreateInfo desc_poolinfo = {0};
    desc_poolinfo = (VkDescriptorPoolCreateInfo) {
        .sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets       = 1,
        .poolSizeCount = 1,
        .pPoolSizes = (VkDescriptorPoolSize[]) {
                          { .type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            .descriptorCount = 1 }
                      }
    };
    assert(!vkCreateDescriptorPool(gpu, &desc_poolinfo, NULL, &desc_pool));

    VkDescriptorSetAllocateInfo desc_setinfo = {0};
    desc_setinfo = (VkDescriptorSetAllocateInfo) {
        .sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool     = desc_pool,
        .descriptorSetCount = 1,
        .pSetLayouts        = &desc_layout
    };
    vkAllocateDescriptorSets(gpu, &desc_setinfo, desc_set); // ICI

    VkWriteDescriptorSet desc_writeinfo = {0};
    desc_writeinfo = (VkWriteDescriptorSet) {
        .sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet          = *desc_set,
        .descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,   // 1 image
        .pImageInfo      = (VkDescriptorImageInfo[]) {
                               { .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                 .imageView   = view }
                           }
    };
    vkUpdateDescriptorSets(gpu, 0, &desc_writeinfo, 0, NULL);

    return true;
}

bool create_vulkan_buffers(VkDevice gpu, VkSwapchainKHR swapchain, VkRenderPass renderpass,
                           VkImageView* view, VkFramebuffer* buffer)
{
    VkImage image;
    uint32_t image_count = 1;
    vkGetSwapchainImagesKHR(gpu, swapchain, &image_count, &image);

    VkImageViewCreateInfo viewinfo = {0};
    viewinfo = (VkImageViewCreateInfo) {
        .sType      = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .viewType   = VK_IMAGE_VIEW_TYPE_2D,
        .format     = _IMAGE_RGBA8888,
        .image      = image,
        .components = (VkComponentMapping) { .r = VK_COMPONENT_SWIZZLE_R,
                                             .g = VK_COMPONENT_SWIZZLE_G,
                                             .b = VK_COMPONENT_SWIZZLE_B,
                                             .a = VK_COMPONENT_SWIZZLE_A },
        .subresourceRange = (VkImageSubresourceRange) { .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                        .levelCount = 1,
                                                        .layerCount = 1 }
    };
    assert(!vkCreateImageView(gpu, &viewinfo, NULL, view));

    VkFramebufferCreateInfo bufferinfo = {0};
    bufferinfo = (VkFramebufferCreateInfo) {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = renderpass,
        .attachmentCount = 1,      // only 'color' for now
        .pAttachments    = view,
        .width           = INIT_WIDTH,
        .height          = INIT_HEIGHT,
        .layers          = 1
    };
    assert(!vkCreateFramebuffer(gpu, &bufferinfo, NULL, buffer));

    return true;
}


int main (int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Vulkan_LoadLibrary(NULL);

    SDL_Window* window = SDL_CreateWindow(argv[0], INIT_WIDTH, INIT_HEIGHT, SDL_WINDOW_VULKAN);

    SDL_SetWindowResizable(window, true);

    uint32_t ext_count = 0;
    const char* const* ext_names = SDL_Vulkan_GetInstanceExtensions(&ext_count);
    assert(ext_count > 0);

    VkInstance inst;
    assert(initialize_vulkan(ext_names, ext_count, &inst));

    VkDevice gpu;
    VkQueue queue;
    VkCommandPool pool;
    assert(initialize_vulkan_gpu(inst, &gpu, &queue, &pool));

    VkSurfaceKHR surface;
    assert(SDL_Vulkan_CreateSurface(window, inst, NULL, &surface));

    VkSwapchainKHR swapchain;
    VkRenderPass renderpass;
    VkCommandBuffer cmdbuffer;
    assert(initialize_vulkan_pipeline(gpu, pool, surface,
			              &swapchain, &renderpass, &cmdbuffer));

    VkImageView view;
    VkFramebuffer buffer;
    assert(create_vulkan_buffers(gpu, swapchain, renderpass, &view, &buffer));
    
    VkPipelineLayout layout;
    VkDescriptorSet desc_set;
    assert(initialize_vulkan_shaders(gpu, renderpass, view, &layout, &desc_set));

    while (true) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT  : { goto end; }
                case SDL_EVENT_KEY_UP: { if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                                           goto end; }
                                       }
                case SDL_EVENT_WINDOW_RESIZED: { _width  = e.window.data1;
                                                 _height = e.window.data2; }
            }
        }

        redraw(window, gpu, queue, swapchain, renderpass, cmdbuffer, layout, desc_set, buffer);
    }

  end:
    vkDestroyPipelineLayout(gpu, layout, NULL);
    vkDestroyFramebuffer(gpu, buffer, NULL);
    vkDestroyImageView(gpu, view, NULL);
    vkFreeCommandBuffers(gpu, pool, 1, &cmdbuffer);
    vkDestroyRenderPass(gpu, renderpass, NULL);
    vkDestroySwapchainKHR(gpu, swapchain, NULL);
    SDL_Vulkan_DestroySurface(inst, surface, NULL);
    vkDestroyCommandPool(gpu, pool, NULL);
    vkDestroyDevice(gpu, NULL);
    vkDestroyInstance(inst, NULL);
    SDL_DestroyWindow(window);
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();

    return 0;
}
