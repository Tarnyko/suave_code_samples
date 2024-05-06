/*
* unicode_japanese.c
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
 gcc -std=c11 ...
*/

#include <stdio.h>
#include <string.h>  /* for "strlen()"             */
#include <uchar.h>   /* for u8"<str>"              */
#include <stdlib.h>  /* for "malloc()","free()"... */
#include <errno.h>   /* for "errno"                */
#include <locale.h>  /* for "setlocale()","LC_ALL" */

#ifdef _WIN32
#  include <windows.h> /* for "SetConsoleOutput()","MultiByteToWideChar()" */
#  define UCHAR wchar_t
#  define UNICODE(str) L ## #str
#  define UFOPEN_S(X,Y,Z) _wfopen_s(X,Y,L ## Z)
#  define UFPRINTF_S(X,Y,Z) fwprintf_s(X,L ## Y,Z)
#  define USNPRINTF_S(X,Y,Z, ...) _snwprintf_s(X,Y,_TRUNCATE, L ## Z, __VA_ARGS__)
#  define USTRLEN_S(X) wcsnlen(X,1024)

#else
#  define UCHAR char
#  define UNICODE(str) u8 ## #str
#  define UFPRINTF_S(X,Y,Z) fprintf(X,u8 ## Y,Z)
#  define USNPRINTF_S(X,Y,Z, ...) snprintf(X,Y, u8 ## Z, __VA_ARGS__)
#  define USTRLEN_S(X) strlen(X)
int UFOPEN_S(FILE **X, const UCHAR *Y, const char *Z) {
  if ((*X = fopen(Y, Z)) == NULL)
    return errno;
  else
    return 0;
}

#endif


int main (int argc, char *argv[])
{
# ifdef _WIN32
    UINT old_cp = GetConsoleOutputCP();
    SetConsoleOutputCP(932);   /* Japanese codepage: requires setup */
    setlocale(LC_ALL, ".932"); /* of regional & console settings.   */
# else
    setlocale(LC_ALL, ".UTF8");
# endif

    UCHAR *file = UNICODE(片恋いの月 詰め合わせ.txt);
    FILE *f;
    long len;

     /* 1) open file */
    if (UFOPEN_S(&f, file, "r") > 0) {
        fprintf(stderr, "Error on opening read-only:%d\n", errno);
        return EXIT_FAILURE;
    }

     /* 2) get content length */
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    rewind(f);
    printf("Size (bytes): %ld.\n\n", len);

     /* 3) read & display content */
    char data[len];
    len = 0;

    while ((data[len] = fgetc(f)) != EOF)
      len++;
    data[len] = '\0';

# ifdef _WIN32
    len = MultiByteToWideChar(CP_UTF8, 0, data, -1, NULL, 0);
    UCHAR wdata[len];
    MultiByteToWideChar(CP_UTF8, 0, data, -1, &wdata[0], len);
    UFPRINTF_S(stdout, "Content:\n %s \n\n", wdata);
# else
    UFPRINTF_S(stdout, "Content:\n %s \n\n", data);
# endif

    fclose(f);

     /* 4) copy content to other file, same name with ".copy" suffix */
    UCHAR *ftcopy;
    UCHAR *suffix = UNICODE(.copy);
    size_t usize = (USTRLEN_S(file)+USTRLEN_S(suffix)+1) * sizeof(UCHAR);
    ftcopy = (UCHAR*) malloc(usize);
    USNPRINTF_S(ftcopy, usize, "%s%s", file,suffix);

    FILE *ft;
    if (UFOPEN_S(&ft, ftcopy, "w") > 0) {
        fprintf(stderr, "Error on opening read-write:%d\n", errno);
        free(ftcopy);
        return EXIT_FAILURE;
    }
    if (fprintf(ft, u8"%s", data) < 0) {
        fprintf(stderr, "Error on writing data:%d\n", errno);
        free(ftcopy);
        return EXIT_FAILURE;
    }
    UFPRINTF_S(stdout, "Successfully copied data to file '%s'.\n", ftcopy);
    free(ftcopy);
    fclose(ft);

    getchar();
# ifdef _WIN32
    SetConsoleOutputCP(old_cp);
# endif

    return EXIT_SUCCESS;
}
