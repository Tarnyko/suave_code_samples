//  Compile with either:
// gcc -std=c23 -O3 ...
// gcc -std=c11 -O3 ...

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
#    define memset_explicit(X,Y,Z) SecureZeroMemory(X,Z)
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
    scanf("%16s", password);
    flush_stdin();

    printf("You entered: %s\n", password);

    puts("Your password is still in memory... Inspect it now! (press any key...)");
    getchar();

    memset_explicit(password, 0, 16);   // C23

    puts("Password securely deleted from memory... Inspect it now! (press any key...)");
    getchar();

    return 0;  
}
