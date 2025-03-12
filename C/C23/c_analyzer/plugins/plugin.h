/*
* plugins/plugin.h
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

#pragma once

#ifdef _WIN32
#  include <windows.h>
#  define PLG_OPEN(X)  LoadLibraryA(X)
#  define PLG_CLOSE(X) FreeLibrary(X)
#  define PLG_SYM(X,Y) GetProcAddress(X,Y)
#  define PLG_ERR()    false
#  define PLG_HANDLE   HMODULE

#  define PUBLIC  __declspec(dllexport) 
#  define PRIVATE
#else
#  include <dlfcn.h>
#  define PLG_OPEN(X)  dlopen(X,RTLD_NOW)
#  define PLG_CLOSE(X) dlclose(X)
#  define PLG_SYM(X,Y) dlsym(X,Y)
#  define PLG_ERR()    dlerror()
#  define PLG_HANDLE   void*

#  define PUBLIC  __attribute__ ((visibility ("default")))
#  define PRIVATE __attribute__ ((visibility ("hidden")))
#endif

#include <stdio.h>


#ifndef _ERRCODE_DEFINED
  typedef int errno_t;
#endif

typedef struct {
    errno_t num;
    size_t line_num;
    char* msg;
} Err;

void report_err(Err* err)
{
    fprintf(stderr, "Line %d (error %d): %s.\n", err->line_num, err->num, err->msg);
}

void clear_err(Err* err)
{
    err->num = err->line_num = 0;
    if (err->msg) {
        free(err->msg); }
    err->msg = nullptr;
}


typedef enum : unsigned char {
    E_LINE = 0, E_BLOCK = 1, E_BOTH = 2
} Emethod;


typedef struct Plugin Plugin;

typedef Plugin* (*PLG_LOAD)(PLG_HANDLE, const char*);
typedef void    (*PLG_UNLOAD)(Plugin*);
typedef bool    (*PLG_ANALYZE_LINE)(Plugin*, const char*, size_t);
typedef bool    (*PLG_ANALYZE_BLOCK)(Plugin*, const char*, size_t);


struct Plugin
{
    PLG_HANDLE handle;
    char* name;
    char* title;
    int version;
    char* comment;

    // Mandatory symbols
    Plugin* (*load)(PLG_HANDLE handle, const char* name);
    void    (*unload)(Plugin* plugin);

    // ('method' enum says if we implement LINE,BLOCK... or BOTH)
    Emethod method;
    bool (*analyze_line)(Plugin* plugin, const char* line, size_t line_num);
    bool (*analyze_block)(Plugin* plugin, const char* block, size_t first_line_num);

    Err err;
};
