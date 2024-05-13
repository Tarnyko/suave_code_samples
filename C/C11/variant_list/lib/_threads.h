/*
* _threads.h
* Copyright (C) 2024  Manuel Bachmann <tarnyko.tarnyko.net>
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
