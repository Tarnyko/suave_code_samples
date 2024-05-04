/*
* backtrace.cpp
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
 Unix:  g++ -g -rdynamic ...
 Win32: g++ -g ... -ldbghelp
*/

#include <iostream>
#include <cstdlib>
#include <cstring>             /* for "std::strchr()" */
#include <csignal>             /* for "std::signal()" */

#ifdef __unix__
#  include <fcntl.h>           /* for "open()"        */
#  include <unistd.h>          /* for "close()"       */
#  ifdef __linux__ 
#    include <execinfo.h>      /* for "backtrace()" */
#  endif

#elif _WIN32
#  include <windows.h>         /* for "CaptureStackBackTrace()"   */
#  include <dbghelp.h>         /* for "SymInitialize()"           */
#  if _WIN64
#    define DWORDCAST DWORD64
#  else
#    define DWORDCAST DWORD
#  endif
#endif

#define BACKTRACE_FILE   "backtrace.txt"
#define MAX_ADDRESSES    20


extern "C" void catch_crash(int signal)
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


class Parent
{
  public:
    Parent(std::string s) { str = s; }
    ~Parent() = default;
  
    void fn1 (std::string txt, bool crash)
    {
        std::cout << "fn1: "<< txt <<",str="<< str << ", in class: " << typeid(*this).name()+1 << std::endl << std::flush;
        if (crash) {
            *(int*)0 = 0; }
    }

    int fn2 (int a, int b, bool crash)
    {
        std::cout << "fn2: "<< a<<"-"<<b<<",str="<< str << ", in class: " << typeid(*this).name()+1 << std::endl << std::flush;
        if (crash) {
            *(int*)0 = 0; }
        return a + b;
    }

    void* fn3 (const char *ptr, bool crash)
    {
        std::cout << "fn3: "<< ptr <<",str="<< str << ", in class: " << typeid(*this).name()+1 << std::endl << std::flush;
        if (crash) {
            *(int*)0 = 0; }
        return (void*) ptr;
    }
    
    virtual void fn4 (bool crash)
    {
        std::cout << "fn4: str="<< str << ", in class: " << typeid(*this).name()+1 << std::endl << std::flush;
        if (crash) {
            *(int*)0 = 0; }
    }
    
    static void fn5 (bool crash)
    {
        std::cout << "fn5_Parent-Child" << std::endl << std::flush;
        if (crash) {
            *(int*)0 = 0; }
    }
  protected:
    std::string str;
};

class Child : public Parent
{
  public:
	// this is C++11
	using Parent::Parent;

	void fn4 (bool crash) override
	{
	    std::cout << "fn4_override: str=" << str << ", in class: " << typeid(*this).name()+1 << std::endl;
	    if (crash) {
                *(int*)0 = 0; }
	}
};

int main (int argc, char *argv[])
{
    if (argc < 2) {
        printf(" Usage:\n%s 1 2 3 4 5\t\t[OK]\n%s 1 2 3c 4 5\t\t[3:crash]\n%s 1 2 3p 4pc 5\t[3:parent;4:parent+crash]\n(Manuel Bachmann <tarnyko.tarnyko.net>)\n\n", argv[0],argv[0],argv[0]);
        return EXIT_SUCCESS; }

    std::signal(SIGSEGV, catch_crash);

    auto p = Parent("MyParent");
    auto c = Child("MyChild");

    for(int i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        bool parent = (std::strchr(arg, 'p') != NULL) ? true : false;
        bool crash = (std::strchr(arg, 'c') != NULL) ? true : false;
        switch (std::atoi(arg))
        {
            case 1:  parent ? p.fn1(arg, crash) : c.fn1(arg, crash);              break;
            case 2:  parent ? p.fn2(argc,argc, crash) : c.fn2(argc,argc, crash);  break;
            case 3:  parent ? p.fn3(arg, crash) : c.fn3(arg, crash);              break;
            case 4:  parent ? p.fn4(crash) : c.fn4(crash);                        break;
            case 5:  parent ? Parent::fn5(crash) : Child::fn5(crash);             break;
            default:                                                              break;
        }
    }

    return EXIT_SUCCESS;
}
