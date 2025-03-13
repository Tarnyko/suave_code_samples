/*
* wayland-01-list_interfaces.c
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
// $ gcc .. `pkg-config --cflags --libs wayland-client`

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>


typedef enum {
    E_UNKNOWN = 0, E_WESTON = 1, E_GNOME = 2, E_KDE = 3
} CompositorId;

typedef struct {
    CompositorId id;
} InterfaceInfo;


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

    // now this should have been filled by the callbacks
    printf("\nCompositor is: ");
    switch (_info.id)
    {
        case E_WESTON: printf("Weston.\n\n");     break;
        case E_GNOME : printf("GNOME.\n\n");      break;
        case E_KDE   : printf("KDE Plasma.\n\n"); break;
        default      : printf("Unknown...\n\n");
    }

    // clean
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

         if (!strcmp(name, "wl_shm"))                 { printf("\t\t\t [Software rendering]");                              }
    else if (!strcmp(name, "wl_seat"))                { printf("\t\t\t [Input devices (keyboard, mouse, touch)]");          }
    else if (!strcmp(name, "wl_output"))              { printf("\t\t\t [Output devices (screens)]");                        }
    else if (!strcmp(name, "wl_data_device_manager")) { printf("    \t [Clibpoard (copy-paste, drag-drop)]");               }
    else if (strstr(name,  "wp_text_input_manager"))  { printf("\b\b\t [Virtual keyboard]");                                }
    else if (strstr(name,  "wp_pointer_constraints")) { printf("\b\b\t [Pointer lock]");                                    }
    else if (strstr(name,  "wp_linux_dmabuf"))        { printf("    \t [DRM kernel driver]");                               }
    else if (!strcmp(name, "wl_drm"))                 { printf("\t\t\t [DRM kernel driver -legacy]");                       }
    else if (!strcmp(name, "wl_shell"))               { printf("\t\t\t [Stable window manager]");                           }
    else if (strstr(name,  "xdg_shell"))              { printf("  \t\t [Unstable window manager]");                         }
    else if (strstr(name,  "gtk_shell"))              { printf("  \t\t [GNOME window manager]");      _info->id = E_GNOME;  }
    else if (strstr(name,  "plasma_shell"))           { printf("  \t\t [KDE Plasma window manager]"); _info->id = E_KDE;    }
    else if (strstr(name,  "weston"))                 {                                               _info->id = E_WESTON; }
    else if (!strcmp(name, "wl_subcompositor"))       { printf("  \t\t [Subcompositor]");                                   }
    else if (!strcmp(name, "wl_compositor"))          { printf("  \t\t [Compositor]");                                      }

    putchar('\n');
}

void wl_interface_removed(void* data, struct wl_registry* registry, uint32_t serial)
{ }
