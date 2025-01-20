/*
* memset_explicit.c
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

//  Compile with either:
// gcc -std=c23 -O2 (-DNO_EXPLICIT=1) ...
// gcc -std=c11 -O2 (-DNO_EXPLICIT=1) ...

#if __STDC_VERSION__ >= 201112L
#  define __STDC_WANT_LIB_EXT1__ 1
#endif

#define _GNU_SOURCE  // for "explicit_bzero()" in Glibc's <string.h> if needed later
#include <stdio.h>
#include <string.h>  // for "memset()", "__STDC_LIB_EXT1__/memset_s()/memset_explicit()" if present

#ifdef __STDC_LIB_EXT1__
#  if (__STDC_VERSION__ < 202311L) || defined(__APPLE__)
     // Before C23 with C11, or under macOS, we have 'memset_s()'
#    define memset_explicit(X,Y,Z) memset_s(X,Z,Y,Z)
#  endif
#else
#  warning "Not C23/C11, or no 'memset_explicit()' support. Falling back to:"
#  if defined(_WIN32) && (_WIN32_WINNT >= 0x0501)
#    warning "Win32's 'SecureZeroMemory()'"
#    include <windows.h>
#    ifdef  _MSC_VER
#      define memset_explicit(X,Y,Z) SecureZeroMemory(X,Z)
#    else
#      warning "Enforcing non-MSVC implementations"
       static PVOID (__attribute__((stdcall)) *volatile MGW_Szm)(PVOID, SIZE_T) = SecureZeroMemory;
#      define memset_explicit(X,Y,Z) MGW_Szm(X,Z)
#    endif
#  elif defined(__unix__)
#    include <sys/param.h>
#    if (__FreeBSD__ >= 11) || ((__OpenBSD__) && (OpenBSD >= 201405))
#      warning "FreeBSD/OpenBSD's 'explicit_bzero()'"
#      include <strings.h>
#      define memset_explicit(X,Y,Z) explicit_bzero(X,Z)
#    elif (__GLIBC__ > 2) || ((__GLIBC__ == 2) && (__GLIBC_MINOR__ >= 25))
#      warning "Glibc's 'explicit_bzero()'"
#      define memset_explicit(X,Y,Z) explicit_bzero(X,Z)
#    else
#      warning "Custom implementation"
#      include "memset_explicit.h"
#    endif
#  else
#    warning "Custom implementation"
#    include "memset_explicit.h"
#  endif
#endif


void flush_stdin()
{
    int c;
    while ((c = getc(stdin)) != EOF && c != '\n') {
        continue; }
}

int main (int argc, char* argv[])
{
    char password[17] = {0};

    printf("Please enter your password [max length: 16]: ");
    int _ = scanf("%16s", password);
    flush_stdin();

    printf("You entered: %s\n", password);

    puts("Your password is still in memory... Inspect it now! (press any key...)");
    getchar();

# ifdef NO_EXPLICIT
#  warning "We now use the non-explicit version of memset()"
    memset(password, 0, 16);
# else
    memset_explicit(password, 0, 16);   // C23
# endif

    return 0;  
}
