/*
* backtrace.c
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

/* Compile with:
 Unix:  gcc -g ...
 Win32: gcc -g ... -ldbghelp      
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __unix__
#  include <signal.h>          /* for "signal()" */
#  include <fcntl.h>           /* for "open()"   */
#  include <unistd.h>          /* for "close()"  */
#  ifdef __linux__ 
#    include <execinfo.h>      /* for "backtrace()" */
#  endif

#elif _WIN32
#  if (__MINGW64_VERSION_MAJOR < 11)
#      error "Requires MinGW 11.x (GCC 13.x) for 'signal.h' !"
#  endif
#  if _WIN64
#      define DWORDCAST DWORD64
#  else
#      define DWORDCAST DWORD
#  endif
#  include <signal.h>          /* for "signal()" [MinGW>=11]    */
#  include <windows.h>         /* for "CaptureStackBackTrace()" */
#  include <dbghelp.h>         /* for "SymInitialize()"         */
#endif

#define BACKTRACE_FILE   "backtrace.txt"
#define MAX_ADDRESSES    20


static void catch_crash(int signal)
{
    void *bt[MAX_ADDRESSES];
    int bt_size;

    printf(" [SIGSEGV intercepted... ");

#    ifdef __linux__
        int fd = open(BACKTRACE_FILE, O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR);
        if (fd == -1) {
            printf(" [ERROR: could not create file '%s', dumping to console]\n", BACKTRACE_FILE);
            fd = STDOUT_FILENO;
        }
        printf("dumping backtrace file '%s' under Linux]\n", BACKTRACE_FILE);
        bt_size = backtrace(bt, MAX_ADDRESSES);
        backtrace_symbols_fd(bt, bt_size, fd);
        close(fd);
        raise(SIGABRT);

#    elif __unix__
        printf("doing nothing on generic UNIX]\n");
        raise(SIGABRT);

#    elif _WIN32
        FILE *file = fopen(BACKTRACE_FILE, "w");
        if (file == NULL) {
            printf(" [ERROR: could not create file '%s', dumping to console]\n", BACKTRACE_FILE);
            file = stdout;
        }
        printf("dumping backtrace file '%s' under Windows]\n", BACKTRACE_FILE);
        /**/
        HANDLE process = GetCurrentProcess();
        SymInitialize(process, NULL, TRUE);
        SYMBOL_INFO *symbol  = (SYMBOL_INFO*) calloc(sizeof(SYMBOL_INFO) + 256*sizeof(char), 1);
        symbol->MaxNameLen   = 255;
        symbol->SizeOfStruct = sizeof( SYMBOL_INFO );
        DWORD offset;
        IMAGEHLP_LINE line;
        /**/
        bt_size = (int) CaptureStackBackTrace(0, MAX_ADDRESSES, bt, NULL);
        for(int i = 0; i < bt_size; i++)
        {
            SymFromAddr(process, (DWORDCAST)(bt[i]), 0, symbol);
            if (SymGetLineFromAddr(process, (DWORDCAST)(bt[i]), &offset, &line))
                fprintf(file, "%i: %s - %s %lu - 0x%0X\n", bt_size-i-1, symbol->Name, line.FileName, line.LineNumber, symbol->Address);
            else
                fprintf(file, "%i: %s - 0x%0X\n", bt_size-i-1, symbol->Name, symbol->Address);
        }
        free(symbol);
        fclose(file);
#    endif
}


void fn1 (const char *txt, bool crash)
{
    printf("fn1: %s\n", txt); fflush(stdout);
    if (crash) {
        *(int*)0 = 0; }
}

int fn2 (int a, int b, bool crash)
{
    printf("fn2: %d-%d\n", a, b); fflush(stdout);
    if (crash) {
        *(int*)0 = 0; }
}

void* fn3 (void *ptr, bool crash)
{
    printf("fn3: %s\n", (const char*)ptr); fflush(stdout);
    if (crash) {
        *(int*)0 = 0; }
}

int main (int argc, char *argv[])
{
    if (argc < 2) {
        printf(" Usage:\n%s 1 2 3 \t[OK]\n%s 1 2 3c \t[3:crash]\n(Manuel Bachmann <tarnyko.tarnyko.net>)\n\n", argv[0], argv[0]);
        return EXIT_SUCCESS; }

    signal(SIGSEGV, catch_crash);

    for(int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        bool crash = (arg[strlen(arg)-1] == 'c') ? true : false;
        switch (atoi(arg))
        {
            case 1: fn1(arg, crash);        break;
            case 2: fn2(argc,argc, crash);  break;
            case 3: fn3((void*)arg, crash); break;
            default:                        break;
        }
    }

    return EXIT_SUCCESS;
}
