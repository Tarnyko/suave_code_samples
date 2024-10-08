/*
* variant_list.c [test executable]
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

#define _GNU_SOURCE
#include <math.h>         // for "M_PI"
#include <stdio.h>        // for "(f)printf()","getchar()"
#include <stdlib.h>       // for "EXIT_SUCCESS"

#include "variant_list.h"


int main (int argc, char *argv[])
{
    List* l = list_create(0);
    list_dump(l);

    list_add(l, 42);
    list_dump(l);

    list_add(l, true);
    list_add(l, M_PI); // 3.14...
    list_add(l, "Tarnyko does C11");
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

      for (size_t idx = 0; idx < list_length(l); idx++)
      {
        errno_t res = list_get(l, idx, &str);

        printf("Element %zd: ", idx);
        switch (res)
        {
          case EINVAL:  fprintf(stderr, "[BADIDX]\n"); continue;
          case EAGAIN:  fprintf(stderr, "[LOCKED]\n"); continue;
          case EUNDEF:  fprintf(stderr, "[UNDEF]\n");  continue;

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
