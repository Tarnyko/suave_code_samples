/*
* _threads.h
* Copyright (C) 2024  Manuel Bachmann <tarnyko.tarnyko.net>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 3.0 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA  02110-1301, USA.
*/

#pragma once

#ifdef __STDC_NO_THREADS__
#  error "No threads provided on this platform!"
#endif

#ifdef __unix__
#  if ((__GLIBC__ == 2) && (__GLIBC_MINOR__ < 31))
#    include "_threads_posix.h"  // older Linux
#  else
#    include <threads.h>
#  endif
#else
#  include "_threads_posix.h"    // Win32
#endif
