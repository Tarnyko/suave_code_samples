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

/* Compile with:
 gcc -std=gnu11 ...  # "c11" would suffice, if only "usleep()" and M_PI
*/

#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#ifdef _WIN32
#  include <windows.h>
#  define SLEEP(T) Sleep(T)
#else
#  include <unistd.h>
#  define SLEEP(T) usleep(T*1000)
#endif

// required so that "true/false" get recognized as "bool" and not "int"
#undef true
#undef false
#define true ((_Bool)+1)
#define false ((_Bool)+0)


// 1) TYPES

typedef enum { T_UNDEF, T_INTEGER, T_BOOLEAN, T_FLOAT, T_STRING } ValueType;

typedef struct Value Value;

struct Value
{
    size_t idx;

    ValueType t;
    union
    {
        int    i;
        bool   b;
        double f;
        char*  s;
    };   // C11: if anonymous, we can do "value->i", "value->b"....

    Value* next;
};

typedef struct
{
    size_t length;

    volatile bool locked;

    Value* first;
    Value* last;
}
List;


// 2.a) PUBLIC FUNCTION PROTOTYPES

List* list_create();

void list_add_int(List* list, int i);
void list_add_bool(List* list, bool b);
void list_add_float(List* list, double f);
void list_add_string(List* list, char* s);

void list_add_idx_int(List* list, size_t idx, int i);
void list_add_idx_bool(List* list, size_t idx, bool b);
void list_add_idx_float(List* list, size_t idx, double f);
void list_add_idx_string(List* list, size_t idx, char* s);

// C11: these generic macros will make our life easier

#define list_add(L, V) _Generic((V), \
    int: list_add_int, \
    bool: list_add_bool, \
    double: list_add_float, \
    char *: list_add_string)(L, V)

#define list_add_idx(L, I, V) _Generic((V), \
    int: list_add_idx_int, \
    bool: list_add_idx_bool, \
    double: list_add_idx_float, \
    char *: list_add_idx_string)(L, I, V)

void list_del_idx(List* list, size_t idx);

void list_del_last(List* list);

void list_destroy(List* list);

void list_dump(List* list);

size_t list_length(List* list);


// 3) PRIVATE FUNCTIONS

#define LIST_ADD_IDX_CHECK(L, V) list_add_idx(L, (L != NULL) ? L->length : 0, V)

#define LIST_ADD_IDX_CHECK_IMPL(L, I, T) \
    assert(L != NULL);           \
    assert(L->length >= I);      \
    Value* v = _value_create(I); \
    _value_set(v, T);            \
    _list_add_value(L, v);

#define LIST_DEL_IDX_CHECK_IMPL(L, I) \
    assert(L != NULL);           \
    assert(L->length > I);       \
    _list_del_value(L, I);


Value* _value_create(size_t idx)
{
    Value* v = (Value *) calloc(1, sizeof(Value));
    v->idx = idx;
    v->next = NULL;
    return v;
}

#define _value_set(V, T) _Generic((T), \
    int: _value_set_int, \
    bool:  _value_set_bool, \
    double: _value_set_float, \
    char *: _value_set_string)(V, T)

void _value_set_int(Value* v, int i)
{
    v->t = T_INTEGER;
    v->i = i;         // C11
}

void _value_set_bool(Value* v, bool b)
{
    v->t = T_BOOLEAN;
    v->b = b;         // C11
}

void _value_set_float(Value* v, double f)
{
    v->t = T_FLOAT;
    v->f = f;         // C11
}

void _value_set_string(Value* v, char* s)
{
    v->t = T_STRING;
    v->s = s;         // C11
}

void _value_dump(Value* v)
{
    switch (v->t) {
      case T_INTEGER: printf("%d",   v->i);                 break;
      case T_BOOLEAN: printf("%s",   v->b ?"true":"false"); break;
      case T_FLOAT  : printf("%.6f", v->f);                 break;
      case T_STRING : printf("%s",   v->s);                 break;
      default:        fprintf(stderr, "Unknown value type");
    }
}

void _list_add_value(List* list, Value* val)
{
    while (list->locked) {
        SLEEP(10); }
    list->locked = true;

    if (val->idx == list->length) {
       switch (list->last == NULL) {
         case true:  list->first = val; break;
         case false: list->last->next = val;
       }
       list->last = val;
    } else {
       Value* c = list->first;
       for (size_t i = 0; i < val->idx - 1; i++) {
           c = c->next; }
       // emplace our value
       Value* n = c->next;
       c->next = val;
       val->next = n;
       // re-index following ones
       while (n != NULL) {
           n->idx++;
           n = n->next;
       }
    }

    list->length++;
    list->locked = false;
}

void _list_del_value(List* list, size_t idx)
{
    while (list->locked) {
        SLEEP(10); }
    list->locked = true;

    Value* c = list->first;
    for (int i = 0; i < ((int)idx - 1); i++) {
        c = c->next; }
    // this is the target
    Value* n = (idx == 0) ? c : c->next;
    if (idx == 0) {
        list->first = c->next;
    } else if (idx == list->length -1) {
        list->last = c;
        c->next = NULL;
    } else {
        c->next = n->next;
    }
    free(n);
    n = n->next;
    // re-index following ones
    while (n != NULL) {
       n->idx--;
       n = n->next;
    }

    list->length--;
    list->locked = false;
}


// 2.b) PUBLIC FUNCTION IMPLEMENTATIONS

List* list_create()
{
    return (List *) calloc(1, sizeof(List));
}

void list_add_int(List* l, int v) {
    LIST_ADD_IDX_CHECK(l, v); }

void list_add_bool(List* l, bool v) {
    LIST_ADD_IDX_CHECK(l, v); }
 
void list_add_float(List* l, double v) {
    LIST_ADD_IDX_CHECK(l, v); }

void list_add_string(List* l, char* v) {
    LIST_ADD_IDX_CHECK(l, v); }

void list_add_idx_int(List* list, size_t idx, int i) {
    LIST_ADD_IDX_CHECK_IMPL(list, idx, i); }

void list_add_idx_bool(List* list, size_t idx, bool b) {
    LIST_ADD_IDX_CHECK_IMPL(list, idx, b); }

void list_add_idx_float(List* list, size_t idx, double f) {
    LIST_ADD_IDX_CHECK_IMPL(list, idx, f); }

void list_add_idx_string(List* list, size_t idx, char* s) {
    LIST_ADD_IDX_CHECK_IMPL(list, idx, s); }

void list_del_idx(List* list, size_t idx) {
    LIST_DEL_IDX_CHECK_IMPL(list, idx); }

void list_del_last(List* list) {
    LIST_DEL_IDX_CHECK_IMPL(list, list->length - 1); }

void list_destroy(List* list)
{
    assert(list != NULL);

    while (list->length > 0) {
        Value* c = list->first;
        Value* n = c->next;
        free(c);
        list->first = n;
        list->length--;
    }

    free(list);
}

void list_dump(List* list)
{
    assert(list != NULL);

    printf("List length: %ld\n-----------\n%s", list->length, (list->length == 0)?"<empty>\n":"");

    Value *c = list->first;
    for (size_t i = 0; i < list->length; i++) {
        {  printf("[%ld]: ", c->idx);
           _value_dump(c);
           putchar('\n');
        }
        c = c->next;
    }

    putchar('\n');
}

size_t list_length(List* list)
{
    return list->length;
}


/* -----------------------------------------------------*/

#define _USE_MATH_DEFINES
#include <math.h>           // for M_PI

int main (int argc, char *argv[])
{
    List* l = list_create();
    list_dump(l);

    list_add(l, 42);
    list_dump(l);

    list_add(l, true);
    list_add(l, M_PI);
    list_add(l, "Tarnyko does C11");
    list_dump(l);

    list_add_idx(l, 1, "Insert this text in 2nd position...");
    list_add_idx(l, 3, "...and this one in 4th position.");
    list_dump(l);

    list_del_idx(l, 2);
    list_dump(l);

    while (list_length(l) > 0) {
        list_del_last(l);
    }
    list_dump(l);

    list_destroy(l);


    printf("Press key to continue...\n");
    getchar();

    return 0;
}