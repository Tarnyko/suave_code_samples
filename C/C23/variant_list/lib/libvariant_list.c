/*
* libvariant_list.c [library source]
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

#define _GNU_SOURCE  // for "asprintf()"-stdio.h
#include <stdio.h>   // for "(f)printf()"...
#include <stdlib.h>  // for "atoi()","strtod()"...
#include <string.h>  // for "strcmp()","memcpy()"...
#include <math.h>    // for "lround()"
#include <time.h>    // for "timespec_*"-C11,C23
#include <threads.h> // for "mutex_*"-C11,C23

#include "variant_list.h"


// PLUMBERY

#ifdef _WIN32
#  ifdef STATIC
#    define PUBLIC
#  elif SHARED
#    define PUBLIC  __declspec(dllexport) 
#  else
#    define PUBLIC  __declspec(dllimport) 
#  endif
#  define PRIVATE
#else
#  define PUBLIC  __attribute__ ((visibility ("default")))
#  define PRIVATE __attribute__ ((visibility ("hidden")))
#endif

 // C23
static_assert(sizeof(NULL) == sizeof(void(*)()), "NULL non-castable");
static_assert(sizeof(nullptr) == sizeof(nullptr_t), "nullptr non-castable");


// PRIVATE TYPES

typedef enum { T_UNDEF, T_INTEGER, T_BOOLEAN, T_FLOAT, T_STRING } ValueType;

typedef struct Value Value;

struct Value
{
    size_t idx;

    ValueType t;
    union { int i; bool b; double f; char* s; }; /* C11,C23: anonymous, do
                                                    "val->i", "val->b"... */
    Value* next;
};

struct List
{
    size_t length;

    unsigned int timeout;
    mtx_t locked;         // C11,C23

    Value* first;
    Value* last;
};


// PRIVATE FUNCTIONS

#define LIST_INSERT_CHECK(L, V) list_insert(L, (L != nullptr) ? L->length : 0, V)

#define LIST_INSERT_CHECK_IMPL(L, I, T) \
    if (!L || L->length < I) {          \
        return errno = EINVAL; }        \
    Value* v = _value_create(I);        \
    _value_set(v, T);                   \
    return _list_add_value(L, v);

#define LIST_GET_CHECK_IMPL(L, I, T)         \
    if (!L || L->length <= I) {              \
        return errno = EINVAL; }             \
    Value* v = _value_create(I);             \
    if (errno = _list_get_value(L, I, &v)) { \
        return errno; }                      \
    auto e = _value_get(v, T); free(v);      \
    return errno = e;

#define LIST_DEL_CHECK_IMPL(L, I) \
    if (!L || L->length <= I) {   \
        return errno = EINVAL; }  \
    return _list_del_value(L, I);


PRIVATE
Value* _value_create(size_t idx)
{
    auto v = (Value*) calloc(1, sizeof(Value)); // C23
    v->idx = idx;
    v->next = nullptr;

    return v;
}

#define _value_set(V, T) _Generic((T), \
    int:    _value_set_int, \
    bool:   _value_set_bool, \
    double: _value_set_float, \
    char*:  _value_set_string)(V, T)

PRIVATE
void _value_set_int(Value* v, int i)
{
    v->t = T_INTEGER;
    v->i = i;         // C11,C23 (anonymous union)
}

PRIVATE
void _value_set_bool(Value* v, bool b)
{
    v->t = T_BOOLEAN;
    v->b = b;         // C11,C23 (anonymous union)
}

PRIVATE
void _value_set_float(Value* v, double f)
{
    v->t = T_FLOAT;
    v->f = f;         // C11,C23 (anonymous union)
}

PRIVATE
void _value_set_string(Value* v, char* s)
{
    v->t = T_STRING;
    v->s = s;         // C11,C23 (anonymous union)
}

#define _value_get(V, T) _Generic((T), \
    int*:    _value_get_int, \
    bool*:   _value_get_bool, \
    double*: _value_get_float, \
    char**:  _value_get_string, \
    void*:   _value_get_Type)(V, T)

PRIVATE
errno_t _value_get_int(Value* v, int* i)
{
    switch (v->t) {
      case T_INTEGER: *i = v->i;              break;
      case T_BOOLEAN: *i = v->b;              return EBOOLEAN;
      case T_FLOAT  : *i = (int)lround(v->f); return EFLOAT;
      case T_STRING : *i = atoi(v->s);        return ESTRING;
      default       :                         return EUNDEF;
    }
    return EXIT_SUCCESS;
}

PRIVATE
errno_t _value_get_bool(Value* v, bool* b)
{
    switch (v->t) {
      case T_INTEGER: *b = v->i?true:false;      return EINTEGER;
      case T_BOOLEAN: *b = v->b;                 break;
      case T_FLOAT  : *b = (int)v->f?true:false; return EFLOAT;
      case T_STRING : *b = !strcmp(v->s,"true"); return ESTRING;
      default       :                            return EUNDEF;
    }
    return EXIT_SUCCESS;
}

PRIVATE
errno_t _value_get_float(Value* v, double* f)
{
    switch (v->t) {
      case T_INTEGER: *f = v->i;                  return EINTEGER;
      case T_BOOLEAN: *f = v->b;                  return EBOOLEAN;
      case T_FLOAT  : *f = v->f;                  break;
      case T_STRING : *f = strtod(v->s, nullptr); return ESTRING;
      default       :                             return EUNDEF;
    }
    return EXIT_SUCCESS;
}

PRIVATE
errno_t _value_get_string(Value* v, char** s)
{
    switch (v->t) {
      case T_INTEGER: asprintf(s,"%d",v->i);   return EINTEGER;
      case T_BOOLEAN: asprintf(s,"%s",v->b?"true":"false"); return EBOOLEAN;
      case T_FLOAT  : asprintf(s,"%.6f",v->f); return EFLOAT;
      case T_STRING : *s = v->s;               break;
      default       :                          return EUNDEF;
    }
    return EXIT_SUCCESS;
}

PRIVATE
errno_t _value_get_Type(Value* v, void*)
{
    switch (v->t) {
      case T_INTEGER: return EINTEGER;
      case T_BOOLEAN: return EBOOLEAN;
      case T_FLOAT  : return EFLOAT;
      case T_STRING : return ESTRING;
      default       : return EUNDEF;
    }
}

PRIVATE
void _value_dump(Value* v)
{
    switch (v->t) {
      case T_INTEGER: printf("%d",   v->i);                 break;
      case T_BOOLEAN: printf("%s",   v->b ?"true":"false"); break;
      case T_FLOAT  : printf("%.6f", v->f);                 break;
      case T_STRING : printf("%s",   v->s);                 break;
      default       : fprintf(stderr, "Unknown value type");
    }
}

PRIVATE
bool _list_lock(List* list)
{
    struct timespec ts;                 // C11,C23
    timespec_get(&ts, TIME_UTC);
    ts.tv_nsec += list->timeout * 1000;

    return (mtx_timedlock(&list->locked, &ts) == thrd_success);
}

PRIVATE
errno_t _list_add_value(List* list, Value* val)
{
    if (!_list_lock(list))
    {   free(val);
        return errno = EAGAIN; }

    if (val->idx == list->length)
    { switch (list->last == nullptr) {
        case true:  list->first = val; break;
        case false: list->last->next = val;
      }
      list->last = val;
    } else {
      Value* c = list->first;
      for (typeof(val->idx) i = 0; i < val->idx - 1; i++) { // C23
        c = c->next; }
      // emplace our value
      Value* n = c->next;
      c->next = val;
      val->next = n;
      // re-index following ones
      while (n != nullptr) {
        n->idx++;
        n = n->next;
      }
    }

    list->length++;
    mtx_unlock(&list->locked);

    return EXIT_SUCCESS;
}

PRIVATE
errno_t _list_del_value(List* list, size_t idx)
{
    if (!_list_lock(list)) {
        return errno = EAGAIN; }

    Value* c = list->first;
    for (int i = 0; i < ((int)idx - 1); i++) {
      c = c->next; }
    // this is the target
    Value* n = (idx == 0) ? c : c->next;
    if (idx == 0) {
      list->first = c->next;
    } else if (idx == list->length -1) {
      list->last = c;
      c->next = nullptr;
    } else {
      c->next = n->next;
    }
    c = n->next;
    free(n);
    // re-index following ones
    while (c != nullptr) {
      c->idx--;
      c = c->next;
    }

    list->length--;
    mtx_unlock(&list->locked);

    return EXIT_SUCCESS;
}

PRIVATE
errno_t _list_get_value(List* list, size_t idx, Value** val)
{
    if (!_list_lock(list))
    {   free(*val);
        return errno = EAGAIN; }

    Value* c = list->first;
    for (typeof(idx) i = 0; i < idx; i++) { // C23
      c = c->next; }
    memcpy((void*)*val, (void*)c, sizeof(Value));

    mtx_unlock(&list->locked);

    return EXIT_SUCCESS;
}


// PUBLIC FUNCTION IMPLEMENTATIONS

PUBLIC
List* list_create(unsigned int timeout)
{
    auto l = (List*) calloc(1, sizeof(List)); // C23
    l->timeout = timeout;
    mtx_init(&l->locked, mtx_recursive | mtx_timed);

    return l;
}

PUBLIC
errno_t list_add_int(List* l, int v) {
    return LIST_INSERT_CHECK(l, v); }

PUBLIC
errno_t list_add_bool(List* l, bool v) {
    return LIST_INSERT_CHECK(l, v); }
 
PUBLIC
errno_t list_add_float(List* l, double v) {
    return LIST_INSERT_CHECK(l, v); }

PUBLIC
errno_t list_add_string(List* l, char* v) {
    return LIST_INSERT_CHECK(l, v); }

PUBLIC
errno_t list_insert_int(List* list, size_t idx, int i) {
    LIST_INSERT_CHECK_IMPL(list, idx, i); }

PUBLIC
errno_t list_insert_bool(List* list, size_t idx, bool b) {
    LIST_INSERT_CHECK_IMPL(list, idx, b); }

PUBLIC
errno_t list_insert_float(List* list, size_t idx, double f) {
    LIST_INSERT_CHECK_IMPL(list, idx, f); }

PUBLIC
errno_t list_insert_string(List* list, size_t idx, char* s) {
    LIST_INSERT_CHECK_IMPL(list, idx, s); }

PUBLIC
errno_t list_get_int(List* list, size_t idx, int* i) {
    LIST_GET_CHECK_IMPL(list, idx, i); }

PUBLIC
errno_t list_get_bool(List* list, size_t idx, bool* b) {
    LIST_GET_CHECK_IMPL(list, idx, b); }

PUBLIC
errno_t list_get_float(List* list, size_t idx, double* f) {
    LIST_GET_CHECK_IMPL(list, idx, f); }

PUBLIC
errno_t list_get_string(List* list, size_t idx, char** s) {
    LIST_GET_CHECK_IMPL(list, idx, s); }

PUBLIC
errno_t list_get_Type(List* list, size_t idx, void* n) {
    LIST_GET_CHECK_IMPL(list, idx, n); }

PUBLIC
errno_t list_del(List* list, size_t idx) {
    LIST_DEL_CHECK_IMPL(list, idx); }

PUBLIC
errno_t list_del_last(List* list) {
    LIST_DEL_CHECK_IMPL(list, list->length-1); }

PUBLIC
errno_t list_del_first(List* list) {
    LIST_DEL_CHECK_IMPL(list, 0); }

PUBLIC
errno_t list_destroy(List* list)
{
    if (!list) {
        return errno = EINVAL; }
    if (!_list_lock(list)) {
        return errno = EAGAIN; }

    while (list->length > 0) {
      Value* c = list->first;
      Value* n = c->next;
      free(c);
      list->first = n;
      list->length--;
    }
    mtx_destroy(&list->locked);
    free(list);
    list = nullptr;

    return EXIT_SUCCESS;
}

PUBLIC
errno_t list_dump(List* list)
{
    if (!list) {
        return errno = EINVAL; }
    if (!_list_lock(list)) {
        return errno = EAGAIN; }

    printf("List length: %zd\n-----------\n%s", list->length, (list->length == 0)?"<empty>\n":"");

    Value *c = list->first;
    for (typeof(list->length) i = 0; i < list->length; i++) { // C23
      { 
        printf("[%zd]: ", c->idx);
        switch (c->t) {
          case T_INTEGER: printf("(INTEGER)\t"); break;
          case T_BOOLEAN: printf("(BOOLEAN)\t"); break;
          case T_FLOAT  : printf("(FLOAT)\t");   break;
          case T_STRING : printf("(STRING)\t");  break;
          default: fprintf(stderr, "(ERR: Undefined)\t");
        }
        _value_dump(c);
        putchar('\n');
      }
      c = c->next;
    }
    putchar('\n');

    mtx_unlock(&list->locked);

    return EXIT_SUCCESS;
}

PUBLIC
size_t list_length(List* list)
{
    return list ? list->length : 0;
}

