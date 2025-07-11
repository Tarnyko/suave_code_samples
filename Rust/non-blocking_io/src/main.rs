use std::fs;
use std::thread;
use std::time;
use std::io::{self, Write};   // for "std::fs::File::write()"

use getch_rs::{Getch, Key};

#[cfg(unix)]
use std::os::fd::AsRawFd;

#[cfg(windows)]
use winapi::{
    shared::minwindef::DWORD,
    um::winbase::STD_INPUT_HANDLE,
    um::processenv::GetStdHandle,
    um::wincon::ENABLE_LINE_INPUT,
    um::consoleapi::{GetConsoleMode, SetConsoleMode}
};


#[cfg(unix)]
pub fn os_stdin_block(blocking: bool)
{
    let fd = io::stdin().as_raw_fd();
    unsafe {
        let flags = libc::fcntl(fd, libc::F_GETFL);
        libc::fcntl(fd, libc::F_SETFL,
            if blocking {
                flags & !libc::O_NONBLOCK
            } else {
                flags | libc::O_NONBLOCK
            },
        );
    }
}

#[cfg(windows)]
pub fn os_stdin_block(blocking: bool)
{
    let mut flags: DWORD = 0;
    unsafe {
        let handle = GetStdHandle(STD_INPUT_HANDLE);
        if GetConsoleMode(handle, &mut flags) != 0 {
            SetConsoleMode(handle,
                if blocking {
                    flags | ENABLE_LINE_INPUT
                } else {
                    flags & !ENABLE_LINE_INPUT
                }
            );
        }
    }
}


fn main()
{
    let mut f = fs::File::create("log.txt")
        .expect("Could not create file 'log.txt'! Exiting...");

    let g = Getch::new();

    os_stdin_block(false);

    loop {
        match g.getch() {
            Ok(Key::Char('\r')) => break,
            Ok(Key::Char(c)) => {
                print!("{}", c); io::stdout().flush().unwrap();
                f.write(&[c as u8])
                    .expect("Could not write to file! Exiting...");   
            },
            Err(_) => { print!(" -*-*- "); io::stdout().flush().unwrap(); }
            _ => {},
        }

        thread::sleep(time::Duration::from_millis(100));
    }

    os_stdin_block(true);

    println!("All input written to 'log.txt'.");

    println!("Press any key to continue...");
    g.getch().unwrap();
}
