use std::env;
use std::array::from_fn;
use std::os::fd::AsFd;
use std::error::Error;

use nix::sys::mman::{shm_open, shm_unlink};
use nix::fcntl::OFlag;
use nix::sys::stat::Mode;
use nix::unistd::ftruncate;
use memmap2::MmapMut;

use wayland_client::{
    Connection, EventQueue, QueueHandle, Dispatch,
    protocol::wl_registry, protocol::wl_compositor, protocol::wl_surface,
    protocol::wl_shell, protocol::wl_shell_surface, protocol::wl_buffer,
    protocol::wl_shm, protocol::wl_shm_pool, protocol::wl_callback,
    delegate_noop
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

    window: Option<Window>,
    window_shell_surface_is_configured: bool,
}


const BUFFER_MOTIF: &[u8] = &[0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,
                              0xBB,0xBB,0xBB,0xBB,0xCA,0xCA,0xCA,0xCA];
const BUFFER_COUNT: usize = 2;

struct Buffer {
    shm_id:    String,                          // UNIX shared memory namespace...
    data:      MmapMut,                         // mem-mapped to a Raw buffer...
    buffer:    wl_buffer::WlBuffer,             // mapped to a Wayland buffer.
    motif_pos: usize,
}

struct Window {
    buffer_current: usize,
    buffers:        [Buffer; BUFFER_COUNT],          // OS/Wayland buffer waiting for...
    callback:       Option<wl_callback::WlCallback>, // a redraw callback that fires...
    surface:        wl_surface::WlSurface,           // ...on a Wayland surface...
    _shell_surface: ShellSurface,                    // ...handled by a window manager.

    width:        i32,
    height:       i32,
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

    match create_window(&mut info, queue, &args[0], 320, 240) {
        Err(e)         => {
            let msg = format!("Could not create window: {}! Exiting...", e); 
            return Err(msg.into());
        },
        Ok((window, mut queue)) => {
            info.window = Some(window);

            // first 'redraw_window()' will call itself infinitely through 'wl_callback'
            redraw_window(&mut info);

            // MAIN LOOP
            println!("Looping...\n");
            loop {
                let Ok(_) = queue.blocking_dispatch(&mut info) else {
                    break; };
            }

            destroy_window(&mut info);
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

fn create_shell_surface(info: &mut InterfaceInfo,
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
          info.window_shell_surface_is_configured = true;
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

fn create_window(info: &mut InterfaceInfo, mut queue: EventQueue<InterfaceInfo>, title: &str,
                 width: i32, height:i32) -> Result<(Window, EventQueue<InterfaceInfo>), String>
{
    let Some(compositor) = &info.compositor else {
        return Err("compositor interface not found".to_string());};

    let surface = compositor.create_surface(&info.queue_h, ());

    let Ok(shell_surface) = create_shell_surface(info, &surface, title) else {
        return Err("could not create shell surface".to_string()); };

    // [/!\ 'xdg-wm-base' expects a commit before attaching a buffer (/!\)]
    surface.commit();

    // [/!\ 'xdg-wm-base' expects a configure event before attaching a buffer (/!\)]
    while !&info.window_shell_surface_is_configured {
        queue.roundtrip(info).unwrap(); }

    let buffers: [Option<Buffer>; BUFFER_COUNT] = from_fn(|i|
    {
        // 1) create a POSIX shared memory object (name must contain a single '/')
        let shm_id = format!("/{}-{}", &title.replace("/", ""), &i);
        let Ok(fd) = shm_open(shm_id.as_str(), OFlag::O_CREAT | OFlag::O_RDWR,
                              Mode::from_bits_truncate(0600)) else {
            return None };
        // allocate as much raw bytes as needed pixels in the file descriptor
        let Ok(_) = ftruncate(&fd, (width * height * 4).into()) else {  // *4 = RGBA
            return None };

        // 2) write White pixels (0xFF) to it
        let mut data = unsafe {
            // SAFETY: fd checked just before
            MmapMut::map_mut(&fd).unwrap()
        };
        data[..].fill(0xFFu8);

        // 3) pass the descriptor to the compositor through its 'wl_shm_pool' interface
        let shm = info.shm.as_ref().unwrap();
        let pool = shm.create_pool(fd.as_fd(), width * height * 4, &info.queue_h, ());
        // and create the final 'wl_buffer' abstraction
        let buffer = pool.create_buffer(0, width, height, width * 4,
                                        wl_shm::Format::Xrgb8888, &info.queue_h, ());

        Some(Buffer { shm_id, data, buffer, motif_pos: 0 })
    });
    if buffers.iter().any(|b| b.is_none()) {
        return Err("could not create shared memory namespace/out of memory".to_string()); }

    let window = Window {
        buffer_current: 0,
        buffers: buffers.map(|b| b.unwrap()),
        callback: None,
        surface, _shell_surface: shell_surface,
        width, height,
    };

    Ok((window, queue))
}

fn redraw_window(info: &mut InterfaceInfo)
{
    let window = &mut info.window.as_mut().unwrap();
    
    let window_bound: usize = (window.width * window.height * 4)
                                .try_into().unwrap();

    // 1) swap the current buffer
    window.buffer_current =
      if window.buffer_current == BUFFER_COUNT - 1 { 0 }
      else { window.buffer_current + 1 };

    let buffer = &mut window.buffers[window.buffer_current];

    // 2) update the raw data exposed by our 'wl_buffer'
    buffer.motif_pos = 
      if buffer.motif_pos < window_bound - BUFFER_MOTIF.len() {
          buffer.motif_pos + BUFFER_MOTIF.len()
      } else {
          window_bound - buffer.motif_pos
      };
    buffer.data[buffer.motif_pos..buffer.motif_pos+BUFFER_MOTIF.len()]
      .copy_from_slice(BUFFER_MOTIF);

    // 3) attach our 'wl_buffer' to our 'wl_surface'
    window.surface.attach(Some(&buffer.buffer), 0, 0);
    window.surface.damage(0, 0, window.width, window.height);

    // 4) set a redraw callback to be fired by the compositor
    window.callback = Some(window.surface.frame(&info.queue_h, ()));

    // 5) commit our 'wl_surface'!
    window.surface.commit();
}

fn destroy_window(info: &mut InterfaceInfo)
{
    let window = &mut info.window.as_mut().unwrap();

    for buffer in &window.buffers {
        shm_unlink(buffer.shm_id.as_str()).unwrap(); }
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
          window: None,
          window_shell_surface_is_configured: false,
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

// WAYLAND CALLBACK (= 'REDRAW') CALLBACKS
impl Dispatch<wl_callback::WlCallback, ()> for InterfaceInfo {
  fn event(this: &mut Self,
           _: &wl_callback::WlCallback, event: wl_callback::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let wl_callback::Event::Done { .. } = event {
          let window = this.window.as_mut().unwrap();
          window.callback = None;
          redraw_window(this);
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
  fn event(this: &mut Self,
           shell_surface: &xdg_surface::XdgSurface, event: xdg_surface::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let xdg_surface::Event::Configure { serial } = event {
          shell_surface.ack_configure(serial);
          this.window_shell_surface_is_configured = true
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

