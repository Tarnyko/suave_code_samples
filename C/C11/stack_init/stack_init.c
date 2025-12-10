/*
* stack_init.c
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

//  Compile with:
// * UNIX   : gcc -std=c11 -O0 ...
// * Windows: gcc -std=c11 -O0 ... -lntdll

#define _GNU_SOURCE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <winternl.h> // for "NtQueryInformationProcess()"

#  ifndef __STDC_LIB_EXT1__ // no builtin "memset_s()"
     static PVOID (__attribute__((stdcall)) *volatile SZM)
       (PVOID, SIZE_T) = SecureZeroMemory;
#    define memset_s(W,X,Y,Z) SZM(W,X)
#  endif

#elif defined(__unix__)
#  include <sys/resource.h> // for "getrlimit()"

#  ifndef __STDC_LIB_EXT1__ // no builtin "memset_s()"
#    if defined(__FreeBSD__) || defined(__OpenBSD__)
#      include <strings.h>
#    endif
#    define memset_s(W,X,Y,Z) explicit_bzero(W,X)
#  endif

#else
#  error "Unhandled platform!"
#endif


static size_t get_stack_size(bool show)
{
# ifdef _WIN32
    QUOTA_LIMITS limits = {0};
    NtQueryInformationProcess(GetCurrentProcess(), ProcessQuotaLimits,
                              &limits, sizeof(limits), NULL);
    if (show) {
        printf(" Minimium stack size: %zu bytes\n"
               " Maximum stack size: %zu bytes\n",
               limits.MinimumWorkingSetSize, limits.MaximumWorkingSetSize);
    }
    // alterable with 'editbin' tool
    return limits.MinimumWorkingSetSize;
# else
    struct rlimit rl = {0};
    getrlimit(RLIMIT_STACK, &rl);
    if (show) {
        printf(" Current stack size: %zu bytes\n"
               " Maximum stack size: %zu bytes.\n",
               (size_t) rl.rlim_cur, (size_t) rl.rlim_max);
    }
    // alterable with 'ulimit -s' command
    return rl.rlim_cur;
# endif
}

static bool non_optimizable_condition()
{
    return !strcmp(tmpnam(NULL), "precise");
}

static int test_stack_init(void*)
{
    printf("Thread ID: %lu\n", thrd_current());

    get_stack_size(true);

    if (!non_optimizable_condition()) {
      goto error; } // will always happen

    // we never reach this...
    char* str = strdup("Test");
    puts(str);

    goto end;

  error:
    // ... but we sometimes reach this (str!=NULL)
    if (str != NULL) {
        puts(" 'str' is non-NULL.");
        //free(str);  // would end badly...!
    } else {
        puts(" 'str' is NULL.");
    }

  end:
    return EXIT_SUCCESS;
}


int main(int argc, char* argv[])
{
    // a new thread's stack is guaranteed to be zero-initialized
    thrd_t thread;
    thrd_create(&thread, test_stack_init, NULL);
    thrd_join(thread, &(int){true});

    // not zero-initialized
    test_stack_init(NULL);

    {
        // overwrite the whole stack manually with zeros
        size_t stack_size = get_stack_size(false);
        char filler[stack_size-32768]; // 32k is a secure margin
        memset_s(filler, sizeof(filler), 0, sizeof(filler));
    }

    // 99% zero-initialized now
    test_stack_init(NULL);


    puts("Press [Enter] to continue...");
    getchar();

    return EXIT_SUCCESS;
}
