// Copyright (C) 2024 Manuel Bachmann <tarnyko.tarnyko.net>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#![no_std]
#![no_main]

extern crate libc;  // required to interact with the terminal


#[panic_handler]
fn panic(_panic: &core::panic::PanicInfo<'_>) -> ! {
    loop {}
}

// "no_mangle" allows the symbol to still be called "main" in the binary
#[no_mangle]
pub extern "C" fn main(_argc: isize, _argv: *const *const u8) -> isize {
    unsafe {
        // we are passing a C string, the final null character is mandatory
        libc::printf(b"Hello, without Standard Library!\n\0".as_ptr() as *const _);
    }
    0
}

