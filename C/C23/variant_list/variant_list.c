/*
* variant_list.c
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

//  Compile with:
// gcc -std=c23 -Wall ... -lm

#define _GNU_SOURCE  // for "asprintf()"-stdio.h,"M_PI"-math.h
#include <math.h>    // for "lround()"
#include <stdio.h>   // for "printf()"...
#include <stdlib.h>  // for "atoi()","strtod()"...
#include <string.h>  // for "strcmp()","memcpy()"...
#include <errno.h>   // for "errno","errno_t"-C11,C23
#include <time.h>    // for "timespec_*"-C11,C23
#include <threads.h> // for "mutex_*"-C11,C23

#if !__has_c_attribute(fallthrough)
#  warning "No [fallthrough] support, compile with '-Wno-implicit-fallthrough'"
#endif

#ifndef _ERRCODE_DEFINED
  typedef int errno_t;   // C11,C23 (but only MinGW provides it now)
#else
# include <limits.h>
  static_assert(INT_MAX == ((errno_t)0+INT_MAX), "errno_t invalid"); // C23
#endif

 // convenience macro
#define TYPEOF(F, ...) typeof(F(nullptr __VA_OPT__(,) __VA_ARGS__)) // C23

 // C23
static_assert(sizeof(NULL) == sizeof(void(*)()), "NULL non-castable");
static_assert(sizeof(nullptr) == sizeof(nullptr_t), "nullptr non-castable");


// 1) TYPES

#define EUNDEF   200
#define EINTEGER (EUNDEF + 1)
#define EBOOLEAN (EUNDEF + 2)
#define EFLOAT   (EUNDEF + 3)
#define ESTRING  (EUNDEF + 4)

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

typedef struct
{
    size_t length;

    unsigned int timeout;
    mtx_t locked;         // C11,C23

    Value* first;
    Value* last;
}
List;


// 2.a) PUBLIC FUNCTION PROTOTYPES

[[nodiscard("Leaking unused List")]]       // C23
List* list_create(unsigned int timeout);

errno_t list_add_int(List* list, int i);
errno_t list_add_bool(List* list, bool b); // C23 ("bool" without "stdbool.h")
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

// C11,C23: these generic macros will make our life easier

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
    int*:      list_get_int, \
    bool*:     list_get_bool, \
    double*:   list_get_float, \
    char**:    list_get_string, \
    void*:     list_get_Type, \
    nullptr_t: list_get_Type)(L, I, V)  // C23

errno_t list_del(List* list, size_t idx);
errno_t list_del_last(List* list);
errno_t list_del_first(List* list);

errno_t list_destroy(List* list);

errno_t list_dump(List* list);

[[nodiscard]]
size_t list_length(List* list);


// 3) PRIVATE FUNCTIONS

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

void _value_set_int(Value* v, int i)
{
    v->t = T_INTEGER;
    v->i = i;         // C11,C23 (anonymous union)
}

void _value_set_bool(Value* v, bool b)
{
    v->t = T_BOOLEAN;
    v->b = b;         // C11,C23 (anonymous union)
}

void _value_set_float(Value* v, double f)
{
    v->t = T_FLOAT;
    v->f = f;         // C11,C23 (anonymous union)
}

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

bool _list_lock(List* list)
{
    struct timespec ts;                 // C11,C23
    timespec_get(&ts, TIME_UTC);
    ts.tv_nsec += list->timeout * 1000;

    return (mtx_timedlock(&list->locked, &ts) == thrd_success);
}

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


// 2.b) PUBLIC FUNCTION IMPLEMENTATIONS

List* list_create(unsigned int timeout)
{
    auto l = (List*) calloc(1, sizeof(List)); // C23
    l->timeout = timeout;
    mtx_init(&l->locked, mtx_recursive | mtx_timed);

    return l;
}

errno_t list_add_int(List* l, int v) {
    return LIST_INSERT_CHECK(l, v); }

errno_t list_add_bool(List* l, bool v) {
    return LIST_INSERT_CHECK(l, v); }
 
errno_t list_add_float(List* l, double v) {
    return LIST_INSERT_CHECK(l, v); }

errno_t list_add_string(List* l, char* v) {
    return LIST_INSERT_CHECK(l, v); }

errno_t list_insert_int(List* list, size_t idx, int i) {
    LIST_INSERT_CHECK_IMPL(list, idx, i); }

errno_t list_insert_bool(List* list, size_t idx, bool b) {
    LIST_INSERT_CHECK_IMPL(list, idx, b); }

errno_t list_insert_float(List* list, size_t idx, double f) {
    LIST_INSERT_CHECK_IMPL(list, idx, f); }

errno_t list_insert_string(List* list, size_t idx, char* s) {
    LIST_INSERT_CHECK_IMPL(list, idx, s); }

errno_t list_get_int(List* list, size_t idx, int* i) {
    LIST_GET_CHECK_IMPL(list, idx, i); }

errno_t list_get_bool(List* list, size_t idx, bool* b) {
    LIST_GET_CHECK_IMPL(list, idx, b); }

errno_t list_get_float(List* list, size_t idx, double* f) {
    LIST_GET_CHECK_IMPL(list, idx, f); }

errno_t list_get_string(List* list, size_t idx, char** s) {
    LIST_GET_CHECK_IMPL(list, idx, s); }

errno_t list_get_Type(List* list, size_t idx, void* n) {
    LIST_GET_CHECK_IMPL(list, idx, n); }

errno_t list_del(List* list, size_t idx) {
    LIST_DEL_CHECK_IMPL(list, idx); }

errno_t list_del_last(List* list) {
    LIST_DEL_CHECK_IMPL(list, list->length-1); }

errno_t list_del_first(List* list) {
    LIST_DEL_CHECK_IMPL(list, 0); }

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

size_t list_length(List* list)
{
    return list ? list->length : 0;
}


/* -----------------------------------------------------*/

int main (int argc, char *argv[])
{
    auto l = list_create(0); // C23
    list_dump(l);

    list_add(l, 42);
    list_dump(l);

    list_add(l, true);
    list_add(l, M_PI); // 3.14...
    list_add(l, "Tarnyko does C23");
    list_dump(l);

    list_insert(l, 1, "Insert this text in 2nd position...");
    list_insert(l, 3, "...and this one in 4th position.");
    list_dump(l);

    { int i;
      list_get(l, 4, &i);
      printf("(5th element fetched as an Integer: %d)\n\n", i);
    }

    { char* str;
      printf("Fetching all elements as Strings :\n");
      printf("--------------------------------  \n");

      for (TYPEOF(list_length) idx = 0; idx < list_length(l); idx++)
      {
        auto res = list_get(l, idx, &str); // C23
        char* err = nullptr;

        printf("Element %zd: ", idx);
        switch (res)
        {
          case EINVAL:  if (!err) err="Invalid index"; [[fallthrough]]; // C23
          case EAGAIN:  if (!err) err="List locked";   [[fallthrough]]; // C23
          case EUNDEF:  if (!err) err="Undefined value";
            fprintf(stderr, "[ERR: %s]\n", err);
            continue;

          case EINTEGER:  printf("(INTEGER"); goto print_free;
          case EBOOLEAN:  printf("(BOOLEAN"); goto print_free;
          case EFLOAT  :  printf("(FLOAT");   goto print_free;
          case EXIT_SUCCESS:  printf("(STRING)\t\t '%s'\n", str); continue;
          print_free:  printf(",converted)\t '%s'\n", str);
                       free(str); // need to free the auto-converted string
        }
      }
      putchar('\n');
    }

    printf("(Deleting 3rd element now)\n\n");
    list_del(l, 2);
    list_dump(l);

    printf("(Trying to delete %zdth element...\n", list_length(l)+1);
    switch (list_del(l, list_length(l)))
    {
      case EAGAIN: fprintf(stderr, "...locked by another thread!)\n\n"); break;
      case EINVAL: fprintf(stderr, "...not found in list!)\n\n"); break;
      default    : printf("...success.\n");
    }

    while (list_length(l) > 0) {
      list_del_last(l);
    }
    list_dump(l);

    list_destroy(l);


    printf("Press key to continue...\n");
    getchar();

    return EXIT_SUCCESS;
}
