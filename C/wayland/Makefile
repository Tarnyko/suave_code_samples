NAME := wayland-c-suave-samples

CFLAGS := -std=gnu99 -fPIC

SANITIZE_FLAGS := -fsanitize=undefined,address,bounds

PKGCONFIG_CMD         := pkg-config --cflags --libs wayland-client
PKGCONFIG_LIB_OPENGL  := wayland-egl egl glesv2
PKGCONFIG_LIB_VULKAN  := vulkan
PKGCONFIG_LIB_DECOR   := libdecor-0

# set VULKAN=1 if undefined
ifeq ($(origin VULKAN), undefined)
        VULKAN = 1
endif

ifeq ($(VULKAN),1)
        CFLAGS := $(CFLAGS) -DWITH_VULKAN
endif

ifeq ($(DEBUG),1)
        CFLAGS := $(CFLAGS) -g $(SANITIZE_FLAGS)
endif


filenames := wayland-01-list_interfaces \
             wayland-02-list_interfaces-opengl_vulkan \
             wayland-03-shm_software_rendering \
             wayland-04-shm_software_rendering-redraw \
             wayland-05-accelerated_rendering-opengl \
             wayland-07-input \
             wayland-08-input_shell \
             wayland-09-drag_and_drop

ifeq ($(VULKAN),1)
             filenames := $(filenames)\
             wayland-06-accelerated_rendering-vulkan
endif

ifeq ($(DECOR),1)
             filenames := $(filenames)\
             wayland-08bis-input_shell-libdecor
endif

files := $(strip $(foreach f,$(filenames),$(wildcard $(f))))

source_dir := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
protocol_deps := $(source_dir)_deps/*.c

###


all: $(filenames)

clean:
ifneq ($(files),)
	rm -f $(files)
endif


wayland-01-list_interfaces: wayland-01-list_interfaces.c
	${CC} $(CFLAGS) $< -o $@ `$(PKGCONFIG_CMD)`

wayland-02-list_interfaces-opengl_vulkan: wayland-02-list_interfaces-opengl_vulkan.c
	${CC} $(CFLAGS) $< -o $@ `$(PKGCONFIG_CMD) $(PKGCONFIG_LIB_OPENGL) ${PKGCONFIG_LIB_VULKAN}`

wayland-03-shm_software_rendering: wayland-03-shm_software_rendering.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD)` -lrt

wayland-04-shm_software_rendering-redraw: wayland-04-shm_software_rendering-redraw.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD)` -lrt

wayland-05-accelerated_rendering-opengl: wayland-05-accelerated_rendering-opengl.c
	${CC} $(CFLAGS) $(protocol_deps)  $< -o $@ `$(PKGCONFIG_CMD) $(PKGCONFIG_LIB_OPENGL)`

wayland-06-accelerated_rendering-vulkan: wayland-06-accelerated_rendering-vulkan.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD) $(PKGCONFIG_LIB_VULKAN)`

wayland-07-input: wayland-07-input.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD)` -lrt

wayland-08-input_shell: wayland-08-input_shell.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD)` -lrt

wayland-08bis-input_shell-libdecor: wayland-08bis-input_shell-libdecor.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD) $(PKGCONFIG_LIB_DECOR)` -lrt

wayland-09-drag_and_drop: wayland-09-drag_and_drop.c
	${CC} $(CFLAGS) $(protocol_deps) $< -o $@ `$(PKGCONFIG_CMD)` -lrt

