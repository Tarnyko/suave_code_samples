use std::env;
use std::ffi::CStr;
use std::error::Error;

use wayland_client::{
    Connection, EventQueue, QueueHandle, Dispatch, Proxy,
    protocol::wl_display, protocol::wl_registry, protocol::wl_compositor,
    protocol::wl_surface, protocol::wl_shell, protocol::wl_shell_surface,
    delegate_noop
};
use wayland_protocols::xdg::shell::client::{xdg_wm_base, xdg_surface, xdg_toplevel};
use wayland_egl::WlEglSurface;


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

    egl_display:   Option<egli::Display>,               // EGL: display
    egl_context:   Option<egli::Context>,               //      context
    egl_config:    Option<egli::FrameBufferConfigRef>,  //      config

    has_opengl:    bool,
    has_opengles:  bool,

    window: Option<Window>,
    shell_surface_is_configured: bool,
}


struct Window {
    egl_surface:    egli::Surface,                   // EGL surface attached to...
    _egl_window:    WlEglSurface,                    // ...a Wayland-EGL glue to...
    _surface:       wl_surface::WlSurface,           // ...a Wayland surface...
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

    let mut display = connection.display();

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

    // choose a shell/window manager protocol
    match elect_shell(&mut info) {
        Err(_)         => return Err("No compatible shell found! Exiting....".into()),
        Ok(shell_name) => println!("Shell: '{}'\n", shell_name),
    }

    // check & initialize EGL/OpenGL
    match initialize_egl(&mut info, &mut display) {
        Err(e) => {
            let msg = format!("Could not initialize EGL: {}! Exiting...", e);
            return Err(msg.into());
        },
        Ok(_)  => {
            if !info.has_opengl && !info.has_opengles {
                return Err("No valid OpenGL(ES) implementation found! Exiting...".into()); }
        },
    };

    match create_window(&mut info, queue, &args[0], 320, 240) {
        Err(e)         => {
            let msg = format!("Could not create window: {}! Exiting...", e); 
            return Err(msg.into());
        },
        Ok((window, mut queue)) => {
            info.window = Some(window);

            // MAIN LOOP
            println!("Looping...\n");
            loop {
                let Ok(_) = queue.blocking_dispatch(&mut info) else {
                    break; };
                redraw_window(&mut info);
            }
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
          info.shell_surface_is_configured = true;
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

fn initialize_egl(info: &mut InterfaceInfo, display: &mut wl_display::WlDisplay) -> Result<(), String>
{
    let Ok(egl_display) = egli::Display::from_display_id(display.id().as_ptr() as *mut _) else {
        return Err("error getting EGL display from Wayland display".to_string()); };

    let Ok(version) = egl_display.initialize_and_get_version() else {
        return Err("display initialization error".to_string()); };

    let vendor = egl_display.query_vendor().unwrap();

    println!("EGL version:\t {}.{} [{}]\n", version.major, version.minor, vendor);

    let apis = egl_display.query_client_apis().unwrap().split_whitespace();
    let v_apis: Vec<&str> = apis.collect();
    info.has_opengl = v_apis.contains(&"OpenGL");
    info.has_opengles = v_apis.contains(&"OpenGL_ES");

    let configs = egl_display.config_filter()
        .with_red_size(8).with_green_size(8).with_blue_size(8)
        .with_depth_size(24)
        .with_surface_type(egli::SurfaceType::WINDOW)
        .with_renderable_type(if info.has_opengles
                              { egli::RenderableType::OPENGL_ES } else
                              { egli::RenderableType::OPENGL })
        .choose_configs().unwrap();
    if configs.len() == 0 {
        return Err("no suitable configuration found".to_string()); }
    let egl_config = configs[0];

    let Ok(egl_context) = egl_display.create_context(egl_config) else {
        return Err("context creation error".to_string()); };

    info.egl_display = Some(egl_display);
    info.egl_context = Some(egl_context);
    info.egl_config  = Some(egl_config);

    Ok(())
}

fn create_window(info: &mut InterfaceInfo, mut queue: EventQueue<InterfaceInfo>, title: &str,
                 width: i32, height:i32) -> Result<(Window, EventQueue<InterfaceInfo>), String>
{
    let Some(compositor) = &info.compositor else {
        return Err("compositor interface not found".to_string());};
    let Some(egl_display) = &info.egl_display else {
        return Err("EGL display not found".to_string());};
    let Some(egl_context) = &info.egl_context else {
        return Err("EGL context not found".to_string());};

    let surface = compositor.create_surface(&info.queue_h, ());

    let Ok(egl_window) = WlEglSurface::new(surface.id(), width, height) else {
        return Err("could not create EGL window".to_string()); };

    let Ok(egl_surface) = egl_display.create_window_surface(info.egl_config.unwrap(),
                                          egl_window.ptr() as *mut _) else {
        return Err("could not create EGL surface".to_string()); };

    egl_display.make_current(&egl_surface, &egl_surface, egl_context).unwrap();

    // Bind all future "gl::[fct]()" calls
    gl::load_with(|fct| egli::egl::get_proc_address(fct) as *const _);

    unsafe {
        // SAFETY: OpenGL cannot be proven safe by Rust
        let version = CStr::from_ptr(gl::GetString(gl::VERSION) as *const i8);
        let vendor = CStr::from_ptr(gl::GetString(gl::VENDOR) as *const i8);
        println!("OpenGL version:\t {} [{}]", version.to_string_lossy(),
                                              vendor.to_string_lossy());
    }

    let Ok(shell_surface) = create_shell_surface(info, &surface, title) else {
        return Err("could not create shell surface".to_string()); };

    // [/!\ 'xdg-wm-base' expects a commit before attaching a buffer (/!\)]
    surface.commit();

    // [/!\ 'xdg-wm-base' expects a configure event before attaching a buffer (/!\)]
    while !&info.shell_surface_is_configured {
        queue.roundtrip(info).unwrap(); }

    let window = Window {
        egl_surface, _egl_window: egl_window,
        _surface: surface, _shell_surface: shell_surface,
        width, height,
    };

    Ok((window, queue))
}

fn redraw_window(info: &mut InterfaceInfo)
{
    let window = &mut info.window.as_mut().unwrap();
    let egl_display = &info.egl_display.as_ref().unwrap();

    unsafe {
        // SAFETY: OpenGL cannot be proven safe by Rust
        // these drawing functions are common to OpenGL & OpenGLES
        gl::Viewport(0, 0, window.width, window.height);
        gl::ClearColor(1.0, 1.0, 1.0, 0.0);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        let _ = egl_display.swap_buffers(&window.egl_surface);
    }
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
          egl_display:   None,
          egl_context:   None,
          egl_config:    None,
          has_opengl:    false,
          has_opengles:  false,
          window: None,
          shell_surface_is_configured: false,
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
  fn event(this: &mut Self,
           shell_surface: &xdg_surface::XdgSurface, event: xdg_surface::Event,
           _: &(), _: &Connection, _: &QueueHandle<InterfaceInfo>)
  {
      if let xdg_surface::Event::Configure { serial } = event {
          shell_surface.ack_configure(serial);
          this.shell_surface_is_configured = true
      }
  }
}

// OTHER WAYLAND CALLBACKS (no listeners or not useful yet: avoid implementing their Dispatch)
delegate_noop!(InterfaceInfo: ignore wl_compositor::WlCompositor);
delegate_noop!(InterfaceInfo: ignore wl_surface::WlSurface);
delegate_noop!(InterfaceInfo: ignore wl_shell::WlShell);
delegate_noop!(InterfaceInfo: ignore xdg_toplevel::XdgToplevel);

