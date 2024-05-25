/*
* variant_list.h [library header]
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

#ifdef  __cplusplus
extern "C" {
#endif

#include <stdbool.h>      // for "bool","_Bool"
#include <errno.h>        // for "errno","errno_t"-C11


// PLUMBERY

#ifndef _ERRCODE_DEFINED
  typedef int errno_t;   // C11 (but only MinGW provides it now)
#else
#  include <limits.h>
  _Static_assert( INT_MAX == ((errno_t)0)+INT_MAX, "errno_t invalid"); // C11
#endif

 // required so that "true/false" get recognized as "bool" by C11's _Generic
#if __STDC_VERSION__ < 202311L
#  undef  true
#  undef  false
#  define true  ((_Bool)+1)
#  define false ((_Bool)+0)
#endif


// TYPES

typedef struct List List;

#define EUNDEF   200
#define EINTEGER (EUNDEF + 1)
#define EBOOLEAN (EUNDEF + 2)
#define EFLOAT   (EUNDEF + 3)
#define ESTRING  (EUNDEF + 4)


// PUBLIC FUNCTION PROTOTYPES

List* list_create(unsigned int timeout);

errno_t list_add_int(List* list, int i);
errno_t list_add_bool(List* list, bool b);
errno_t list_add_float(List* list, double f);
errno_t list_add_string(List* list, char* s);

errno_t list_insert_int(List* list, size_t idx, int i);
errno_t list_insert_bool(List* list, size_t idx, bool b);
errno_t list_insert_float(List* list, size_t idx, double f);
errno_t list_insert_string(List* list, size_t idx, char* s);

errno_t list_get_int(List* list, size_t idx, int* i);
errno_t list_get_bool(List* list, size_t idx, bool* b);
errno_t list_get_float(List* list, size_t idx, double* f);
errno_t list_get_string(List* list, size_t idx, char** s);
errno_t list_get_Type(List* list, size_t idx, void* n);

// C11: these generic macros will make our life easier

#define list_add(L, V) _Generic((V), \
    int:    list_add_int, \
    bool:   list_add_bool, \
    double: list_add_float, \
    char*:  list_add_string)(L, V)

#define list_insert(L, I, V) _Generic((V), \
    int:    list_insert_int, \
    bool:   list_insert_bool, \
    double: list_insert_float, \
    char*:  list_insert_string)(L, I, V)

#define list_get(L, I, V) _Generic((V), \
    int*:    list_get_int, \
    bool*:   list_get_bool, \
    double*: list_get_float, \
    char**:  list_get_string, \
    void*:   list_get_Type)(L, I, V)

errno_t list_del(List* list, size_t idx);
errno_t list_del_last(List* list);
errno_t list_del_first(List* list);

errno_t list_destroy(List* list);

errno_t list_dump(List* list);

size_t list_length(List* list);


#ifdef  __cplusplus
}
#endif
