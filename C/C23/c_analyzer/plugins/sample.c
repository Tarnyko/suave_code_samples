/*
* plugins/sample.c
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

#define _GNU_SOURCE   // for "asprintf()"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin.h"
static constexpr char PLUGIN_TITLE[]   = "Sample";
static constexpr int  PLUGIN_VERSION   = 1;
static constexpr char PLUGIN_COMMENT[] = "Sample plugin that detects a string.";

constexpr char BAD_STRING[] = "ERROR";


PRIVATE
void my_err_message(char** out, const char* line, const char* found)
{
    asprintf(out, "'%s' at position %d (%-42.42s)", BAD_STRING, found-line, found);
}


PUBLIC
void unload(Plugin* p)
{
    clear_err(&p->err);
    free(p->name);
}

PUBLIC
Plugin* load(PLG_HANDLE handle, const char* name)
{
    Plugin* p = calloc(1, sizeof(Plugin));
    p->handle  = handle;
    p->name    = strdup(name);

    p->title   = (char*) PLUGIN_TITLE;
    p->version = PLUGIN_VERSION;
    p->comment = (char*) PLUGIN_COMMENT;

    p->method       = E_LINE;
    p->analyze_line = (PLG_ANALYZE_LINE) PLG_SYM(p->handle, "analyze_line");

    p->unload       = (PLG_UNLOAD) PLG_SYM(p->handle, "unload");

    if (PLG_ERR()) {
        unload(p);
        free(p);
    }
    return p;
}

PUBLIC
bool analyze_line(Plugin* p, const char* line, size_t line_num)
{
    char *found;
    if (!(found = strstr(line, BAD_STRING))) {
        return false; }

    clear_err(&p->err);
    p->err.num = 2;
    p->err.line_num = line_num;
    my_err_message(&p->err.msg, line, found);

    return true;
}
