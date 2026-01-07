use std::env;
use std::os::fd::{AsFd, AsRawFd};
use std::error::Error;

use nix::sys::mman::{shm_open, shm_unlink};
use nix::fcntl::OFlag;
use nix::sys::stat::Mode;
use nix::unistd::ftruncate;
use nix::unistd::write;

use wayland_client::{
    Connection, EventQueue, QueueHandle, Dispatch,
    protocol::wl_registry, protocol::wl_compositor, protocol::wl_surface,
    protocol::wl_shell, protocol::wl_shell_surface, protocol::wl_buffer,
    protocol::wl_shm, protocol::wl_shm_pool, delegate_noop
};
use wayland_protocols::xdg::shell::client::{xdg_wm_base, xdg_surface, xdg_toplevel};


enum CompositorId { Unknown, Weston, GNOME, KDE, WlRoots }

enum ShellId { Unknown, WlShell, XdgWmBase }

enum ShellSurface { WlShellSurface, XdgShellSurface }


struct InterfaceInfo {
    queue_h:       QueueHandle<Self>,                   // event queue

    compositor_id: CompositorId,
    compositor:    Option<wl_compositor::WlCompositor>, // surface manager

    shell_id:      ShellId,
    wl_shell:      Option<wl_shell::WlShell>,           // window manager (deprecated)
    xdg_wm_base:   Option<xdg_wm_base::XdgWmBase>,      // window manager (stable)

    shm:           Option<wl_shm::WlShm>,               // software renderer
}


struct Buffer {
    shm_id: String,                        // UNIX shared memory namespace 
    _buffer: wl_buffer::WlBuffer,          // Wayland buffer object
}

struct Window {
    buffer:        Buffer,                 // OS/Wayland buffer mapped to...
    _surface:       wl_surface::WlSurface, // ...a Wayland surface object...
    _shell_surface: ShellSurface,          // ...handled by a window manager.

    _width:        i32,
    _height:       i32,
}


fn main() -> Result<(), Box<dyn Error>>
{
    let args: Vec<String> = env::args().collect();

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
    let mut info = InterfaceInfo::new(queue_h);
    queue.roundtrip(&mut info).unwrap();

    // .. and 'info' has now been filled by 'Dispatch<> -> event()'
    print!("\nCompositor is: ");
    match info.compositor_id
    {
        CompositorId::Weston  => println!("Weston.\n"),
        CompositorId::GNOME   => println!("GNOME.\n"),
        CompositorId::KDE     => println!("KDE Plasma.\n"),
        CompositorId::WlRoots => println!("wlroots.\n"),
        _                     => println!("Unknown...\n")
    }

    // no need to bother if 'wl_shm' (software rendering) is not available
    if let None = info.shm {
        return Err("No software rendering 'wl_shm' interface found! Exiting...".into()); }

    // choose a shell/window manager protocol
    match elect_shell(&mut info) {
        Err(_)         => return Err("No compatible shell found! Exiting....".into()),
        Ok(shell_name) => println!("Shell: '{}'\n", shell_name),
    }

    match create_window(&mut info, &args[0], 320, 240) {
        Err(e)         => {
            let msg = format!("Could not create window: {}! Exiting...", e); 
            return Err(msg.into());
        },
        Ok(mut window) => {
            // MAIN LOOP
            println!("Looping...\n");
            loop {
                let Ok(_) = queue.blocking_dispatch(&mut info) else {
                    break; };
            }
            destroy_window(&mut window);
        }
    }

    Ok(())
}

fn elect_shell(info: &mut InterfaceInfo) -> Result<String, ()>
{
    // 'xdg_wm_base': stable
    if let Some(_) = &info.xdg_wm_base {
        info.shell_id = ShellId::XdgWmBase;
        Ok("xdg_wm_base".to_string())
    // 'wl_shell': deperecated (for old compositors)
    } else if let Some(_) = &info.wl_shell {
        info.shell_id = ShellId::WlShell;
        Ok("wl_shell".to_string())
    } else {
        Err(())
    }
}

fn create_shell_surface(info: &InterfaceInfo,
                        surface: &wl_surface::WlSurface,
                        title: &str) -> Result<ShellSurface, ()>
{
    match info.shell_id
    {
      ShellId::WlShell => {
          let wl_shell = info.wl_shell.as_ref().unwrap();
          let wl_shell_surface = wl_shell.get_shell_surface(surface, &info.queue_h, ());
          wl_shell_surface.set_title(title.into());
          wl_shell_surface.set_toplevel();
          Ok(ShellSurface::WlShellSurface)
      },
      ShellId::XdgWmBase => {
          let xdg_wm_base = info.xdg_wm_base.as_ref().unwrap();
          let xdg_surface = xdg_wm_base.get_xdg_surface(surface, &info.queue_h, ());
          let xdg_shell_surface = xdg_surface.get_toplevel(&info.queue_h, ());
          xdg_shell_surface.set_title(title.into());
          Ok(ShellSurface::XdgShellSurface)
      },
      _ => { Err(()) }
    }
}

fn create_window(info: &mut InterfaceInfo, title: &str, width: i32, height:i32) -> Result<Window, String>
{
    let shm = info.shm.as_ref().unwrap();
    let Some(compositor) = &info.compositor else {
        return Err("compositor interface not found".to_string());};

    let surface = compositor.create_surface(&info.queue_h, ());

    let Ok(shell_surface) = create_shell_surface(&info, &surface, title) else {
        return Err("could not create shell surface".to_string()); };

    // [/!\ 'xdg-wm-base' expects a commit before attaching a buffer (/!\)]
    surface.commit();

    // [/!\ 'xdg-wm-base' expects a configure event before attaching a buffer (/!\)]
    //queue.roundtrip(&mut info).unwrap();

    // 1) create a POSIX shared memory object (name must contain a single '/')
    let shm_name = "/".to_string() + &title.replace("/", "");
    let Ok(fd) = shm_open(shm_name.as_str(), OFlag::O_CREAT | OFlag::O_RDWR,
                          Mode::from_bits_truncate(0600)) else {
        return Err("could not create shared memory namespace".to_string()); };
    // allocate as much raw bytes as needed pixels in the file descriptor
    let Ok(_) = ftruncate(&fd, (width * height * 4).into()) else {  // *4 = RGBA
        return Err("out of memory".to_string()); };

    // 2) write White pixels (0xFF) to it
    let data = vec![0xFFu8; (width * height * 4).try_into().unwrap()];
    write(fd.as_raw_fd(), &data).unwrap();

    // 3) pass the descriptor to the compositor through its 'wl_shm_pool' interface
    let pool = shm.create_pool(fd.as_fd(), width * height * 4, &info.queue_h, ());
    // and create the final 'wl_buffer' abstraction
    let buf = pool.create_buffer(0, width, height, width * 4,
                                 wl_shm::Format::Xrgb8888, &info.queue_h, ());

    // 4) attach our 'wl_buffer' to our 'wl_surface', and commit it
    surface.attach(Some(&buf), 0, 0);
    surface.damage(0, 0, width, height);
    surface.commit();

    let buffer = Buffer { shm_id: shm_name, _buffer: buf };
    let window = Window { buffer: buffer, _surface: surface,
                          _shell_surface: shell_surface,
                          _width: width, _height: height};
    Ok(window)
}

fn destroy_window(window: &mut Window)
{
    shm_unlink(window.buffer.shm_id.as_str()).unwrap();
}



impl  InterfaceInfo {
  pub fn new(queue_h: QueueHandle<Self>) -> Self {
      Self {
          queue_h,
          compositor_id: CompositorId::Unknown,
          compositor:    None,
          shell_id:      ShellId::Unknown,
          wl_shell:      None,
          xdg_wm_base:   None,
          shm:           None,
      }
  }
}

// WL_REGISTRY_CALLBACKS

impl Dispatch<wl_registry::WlRegistry, ()> for InterfaceInfo {
  fn event(this: &mut Self,
           registry: &wl_registry::WlRegistry, event: wl_registry::Event,
           _: &(), _: &Connection, queue_h: &QueueHandle<InterfaceInfo>)
  {
      if let wl_registry::Event::Global { interface, name, .. } = event {
          match interface {
            _ if interface == "wl_compositor" => this.compositor  = Some(registry.bind::<wl_compositor::WlCompositor,_,_>(name, 1, &queue_h, ())),
            _ if interface == "wl_shm"        => this.shm         = Some(registry.bind::<wl_shm::WlShm,_,_>(name, 1, &queue_h, ())),
            _ if interface == "wl_shell"      => this.wl_shell    = Some(registry.bind::<wl_shell::WlShell,_,_>(name, 1, &queue_h, ())),
            _ if interface == "xdg_wm_base"   => this.xdg_wm_base = Some(registry.bind::<xdg_wm_base::XdgWmBase,_,_>(name, 1, &queue_h, ())),
            _ if interface.contains("gtk_shell")       => this.compositor_id = CompositorId::GNOME,
            _ if interface.contains("plasma_shell")    => this.compositor_id = CompositorId::KDE,
            _ if interface.contains("wlr_layer_shell") => this.compositor_id = CompositorId::WlRoots,
            _ if interface.contains("weston")          => this.compositor_id = CompositorId::Weston,
            _ => ()
          }
      }
    }
}

// WAYLAND SHELL CALLBACKS

impl Dispatch<wl_shell_surface::WlShellSurface, ()> for InterfaceInfo {
  fn event(_: &mut Self,
           shell_surface: &wl_shell_surface::WlShellSurface, event: wl_shell_surface::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let wl_shell_surface::Event::Ping { serial } = event {
          shell_surface.pong(serial);
      }
  }
}

impl Dispatch<xdg_wm_base::XdgWmBase, ()> for InterfaceInfo {
  fn event(_: &mut Self,
           shell: &xdg_wm_base::XdgWmBase, event: xdg_wm_base::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let xdg_wm_base::Event::Ping { serial } = event {
          shell.pong(serial);
      }
  }
}

impl Dispatch<xdg_surface::XdgSurface, ()> for InterfaceInfo {
  fn event(_: &mut Self,
           shell_surface: &xdg_surface::XdgSurface, event: xdg_surface::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let xdg_surface::Event::Configure { serial } = event {
          shell_surface.ack_configure(serial);
      }
  }
}

// OTHER WAYLAND CALLBACKS (no listeners or not useful yet: avoid implementing their Dispatch)
delegate_noop!(InterfaceInfo: ignore wl_compositor::WlCompositor);
delegate_noop!(InterfaceInfo: ignore wl_surface::WlSurface);
delegate_noop!(InterfaceInfo: ignore wl_shm::WlShm);
delegate_noop!(InterfaceInfo: ignore wl_shm_pool::WlShmPool);
delegate_noop!(InterfaceInfo: ignore wl_buffer::WlBuffer);
delegate_noop!(InterfaceInfo: ignore wl_shell::WlShell);
delegate_noop!(InterfaceInfo: ignore xdg_toplevel::XdgToplevel);

