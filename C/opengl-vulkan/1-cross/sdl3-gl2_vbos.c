/*
* sdl3-gl2_vbos.c
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

/*  Compile with:
 * - UNIX   : gcc -std=c11 ... `pkg-config --cflags --libs sdl3` -lGL
 * - Windows: gcc -std=c11 ... `pkg-config --cflags --libs sdl3` -lopengl32
 */

#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES    // for OpenGL>1.1 imports
#include <SDL3/SDL_opengl.h>

#ifdef _WIN32
# define IMPORT_GL2_VBO_EXTS() \
    PFNGLGENBUFFERSPROC glGenBuffers =\
        (PFNGLGENBUFFERSPROC) SDL_GL_GetProcAddress("glGenBuffers");\
    PFNGLDELETEBUFFERSPROC glDeleteBuffers =\
        (PFNGLDELETEBUFFERSPROC) SDL_GL_GetProcAddress("glDeleteBuffers");\
    PFNGLBINDBUFFERPROC glBindBuffer =\
        (PFNGLBINDBUFFERPROC) SDL_GL_GetProcAddress("glBindBuffer");\
    PFNGLBUFFERDATAPROC glBufferData =\
        (PFNGLBUFFERDATAPROC) SDL_GL_GetProcAddress("glBufferData");
#else
#  define IMPORT_GL2_VBO_EXTS()     {}
#endif


#define LINES       2
#define INIT_WIDTH  800
#define INIT_HEIGHT 600


static unsigned int _width  = INIT_WIDTH;
static unsigned int _height = INIT_HEIGHT;

 /* WORKING AREA : 
  * [ -1.0;+1.0    +1.0;+1.0 ]
  * [ -1.0;-1.0    +1.0;-1.0 ]
  */
static const GLfloat vertex_arr[LINES * 4] = {   // 1 line: 2 points * 2 coordinates ([-1.0f;1.0f])
    -0.8f,  0.8f,    0.8f, -0.8f,                // - line 1 (\)
    -0.8f, -0.8f,    0.8f,  0.8f,                // - line 2 (/)
};

static const GLubyte color_arr[LINES * 8] = {    // 1 line: 2 points * 4 colors ([R,G,B,A])
    255, 0,   0, 255,      0, 255,   0, 255,     // - color 1 (Red->Green)
      0, 0, 255, 255,    255, 255, 255, 255,     // - color 2 (Blue->White)
};

static const GLuint index_arr[LINES * 2] = {     // 1 line: 2 points ON 2 selected colors
    0, 1,
    2, 3,
};



void redraw(SDL_Window* window, GLuint* vbos)
{
    glViewport(0, 0, _width, _height);                         // size

    glClearColor(0, 0, 0, 255);                                // background (Black)
    glClear(GL_COLOR_BUFFER_BIT);

    IMPORT_GL2_VBO_EXTS();

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glVertexPointer(2, GL_FLOAT, 0, 0);                        // 2 lines

    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, 0);                 // 4 colors

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[2]);
    glDrawElements(GL_LINES, 4, GL_UNSIGNED_INT, 0);           // match 4 points on 4 colors

    SDL_GL_SwapWindow(window);
}


int main (int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

    SDL_Window* window = SDL_CreateWindow(argv[0], INIT_WIDTH, INIT_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(window);

    SDL_SetWindowResizable(window, true);

    glEnableClientState(GL_VERTEX_ARRAY);    // to use "glVertexPointer()"
    glEnableClientState(GL_COLOR_ARRAY);     // to use "glColorPointer()"

    IMPORT_GL2_VBO_EXTS();

    GLuint vbos[3];
    glGenBuffers(3, vbos);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_arr), vertex_arr, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_arr), color_arr, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_arr), index_arr, GL_STATIC_DRAW);

    while (true) {
        SDL_Event e;

        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT  : { goto end; }
                case SDL_EVENT_KEY_UP: { if (e.key.scancode == SDL_SCANCODE_ESCAPE) {
                                           goto end; }
                                       }
                case SDL_EVENT_WINDOW_RESIZED: { _width  = e.window.data1;
                                                 _height = e.window.data2; }
            }
        }

        redraw(window, vbos);
    }

  end:
    glDeleteBuffers(3, vbos);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
