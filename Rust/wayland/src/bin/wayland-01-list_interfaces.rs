use std::error::Error;

use wayland_client::{
    Connection, EventQueue, QueueHandle, Dispatch,
    protocol::wl_registry,
};


const BS: char = 8u8 as char;

enum CompositorId { Unknown, Weston, GNOME, KDE, WlRoots }

struct InterfaceInfo { id: CompositorId }


fn main() -> Result<(), Box<dyn Error>>
{
    let Ok(connection) = Connection::connect_to_env() else {
        return Err("No Wayland compositor found! Do you have a '$XDG_RUNTIME_DIR/wayland-0' socket?\n
                    If not, start it, and set environment variables:\n
                    $ export XDG_RUNTIME_DIR=/run/user/$UID\n
                    $ export WAYLAND_DISPLAY=wayland-0\n\n".into());
    };

    let display = connection.display();

    // listen for asynchronous callbacks with an 'InterfaceInfo' struct attached
    let mut queue: EventQueue<InterfaceInfo> = connection.new_event_queue();
    let queue_h = queue.handle();
    display.get_registry(&queue_h, ());

    // wait for a compositor roundtrip, so all callbacks are fired...
    let mut info = InterfaceInfo { id: CompositorId::Unknown };
    queue.roundtrip(&mut info).unwrap();

    // .. and 'info' has now been filled by 'Dispatch<> -> event()'
    print!("\nCompositor is: ");
    match info.id
    {
        CompositorId::Weston  => println!("Weston.\n"),
        CompositorId::GNOME   => println!("GNOME.\n"),
        CompositorId::KDE     => println!("KDE Plasma.\n"),
        CompositorId::WlRoots => println!("wlroots.\n"),
        _                     => println!("Unknown...\n")
    }

    Ok(())
}


// WL_REGISTRY_CALLBACKS

impl Dispatch<wl_registry::WlRegistry, ()> for InterfaceInfo {
  fn event(this: &mut Self,
           _: &wl_registry::WlRegistry, event: wl_registry::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let wl_registry::Event::Global { interface, version, .. } = event {
          print!("Interface available: name:'{}' - version:'{}'.", interface, version);

          match interface {
            _ if interface == "wl_shm"                            => print!(    "\t\t\t [Software rendering]"),
            _ if interface == "wl_seat"                           => print!(    "\t\t\t [Input devices (keyboard, mouse, touch)]"),
            _ if interface == "wl_output"                         => print!(    "\t\t\t [Output devices (screens)]"),
            _ if interface == "wl_data_device_manager"            => print!(    "    \t [Clipboard (copy-paste, drag-drop)]"),
            _ if interface == "wp_viewporter"                     => print!(    "  \t\t [Surface scaling]"),
            _ if interface == "wp_presentation"                   => print!(    "  \t\t [Precise video synchronization -old]"),
            _ if interface.contains("wp_fifo_manager")            => print!(  "   {0}\t [Precise video synchronization]", BS),
            _ if interface.contains("wp_tearing_control_manager") => print!(  "{0}{0}\t [Tearing control]", BS),
            _ if interface.contains("wp_idle_inhibit_manager")    => print!(  "{0}{0}\t [Screensaver inhibiter]", BS),
            _ if interface.contains("wp_tablet_manager")          => print!(    "    \t [Graphics tablet input]"),
            _ if interface.contains("wp_text_input_manager")      => print!(  "{0}{0}\t [Virtual keyboard]", BS),
            _ if interface.contains("wp_pointer_gestures")        => print!(  "{0}{0}\t [Touchpad gestures]", BS),
            _ if interface.contains("wp_pointer_constraints")     => print!(  "{0}{0}\t [Pointer lock]", BS),
            _ if interface.contains("wp_relative_pointer_manager")=> print!(" {0}{0}{0} [Pointer lock movement]", BS),
            _ if interface.contains("wp_linux_dmabuf")            => print!(   "     \t [DRM Kernel GPU channel]"),
            _ if interface == "wl_drm"                            => print!(    "\t\t\t [DRM Kernel GPU channel -deprecated]"),
            _ if interface == "wl_shell"                          => print!(    "\t\t\t [Standard window manager -deprecated]"),
            _ if interface == "xdg_wm_base"                       => print!(    "\t\t\t [Standard window manager]"),
            _ if interface.contains("xdg_shell")                  => print!(    "  \t\t [Standard window manager -unstable]"),
            _ if interface.contains("gtk_shell")                  => { print!(  "  \t\t [GNOME window manager]");
                                                                       this.id = CompositorId::GNOME }
            _ if interface.contains("plasma_shell")               => { print!(  "  \t\t [KDE Plasma window manager]");
                                                                       this.id = CompositorId::KDE },
            _ if interface.contains("wlr_layer_shell")            => { print!(  "  \t\t [wlroots window manager]");
                                                                       this.id = CompositorId::WlRoots },
            _ if interface.contains("weston")                     => { this.id = CompositorId::Weston },
            _ if interface.contains("zxdg_exporter_v2")           => print!(   "     \t [Foreign client surface export]"),
            _ if interface.contains("zxdg_importer_v2")           => print!(   "     \t [Foreign client surface import]"),
            _ if interface.contains("zxdg_exporter_v1")           => print!(   "     \t [Foreign client surface export -old]"),
            _ if interface.contains("zxdg_importer_v1")           => print!(   "     \t [Foreign client surface import -old]"),
            _ if interface.contains("xdg_activation")             => print!(   "     \t [Window focus switcher]"),
            _ if interface.contains("xdg_output_manager")         => print!(  "{0}{0}\t [Output devices (screens) dimensions]", BS),
            _ if interface.contains("xdg_decoration_manager")     => print!(  "{0}{0}\t [Server-side window decorations]", BS),
            _ if interface.contains("ext_idle_notifier")          => print!(   "     \t [Client-side idle notifier]"),
            _ if interface.contains("ext_workspace_manager")      => print!(  "{0}{0}\t [Virtual desktops]", BS),
            _ if interface == "wl_subcompositor"                  => print!(    "  \t\t [Sub-surfaces]"),
            _ if interface == "wl_compositor"                     => print!(    "  \t\t [Compositor]"),
            _ => ()
          }

          println!("");
      }
    }
}

