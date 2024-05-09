/*
* non-blocking_io.c
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

#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <windows.h>
#  include <conio.h>   /* for "kbhit()","_getch()" */
#  define ENDLINE '\r'
  int getkb (char *code) {
    if (kbhit()) {
      return ((*code = (char) _getch()) > 0); }
    return 0; }

#else
#  include <fcntl.h>   /* for "fcntl()" */
#  include <unistd.h>  /* for STDIN_FILENO */
#  define ENDLINE '\n'
  int getkb (char *code) {
    return (read(STDIN_FILENO, code, 1) > 0); }
#endif


int main(int argc, char *argv[])
{
    char c;
    FILE *f;

    if ((f = fopen("log.txt", "w")) == NULL)
    {   fprintf(stderr, "Could not create file 'log.txt'! Exiting...\n");
        return EXIT_FAILURE;
    }

#  ifndef _WIN32
    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
#  endif

    for(;;)
    {
        if (!getkb(&c)) {
            goto print_progress; }

        printf("%c", c); fprintf(f, "%c", c);

        /* we do something (display text) continuously */
      print_progress:
        printf(" -*-*- \b\b\b\b\b\b\b");
        printf(" *-*-* \b\b\b\b\b\b\b");
        
        /* pressing [Return] will end the loop */
        if (c == ENDLINE) {
            break; }
    }

#  ifdef _WIN32
    fputc('\n', f);
#  endif

    fclose(f);
    printf("\n All input written to 'log.txt'.\n");

    printf("\n Press a key to continue... \n"); 
    getchar();
    
    return EXIT_SUCCESS;
}
