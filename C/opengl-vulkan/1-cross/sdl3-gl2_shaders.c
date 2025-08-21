/*
* sdl3-gl2_shaders.c
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
 * - UNIX:    gcc -std=c11 ... `pkg-config --cflags --libs sdl3` -lGL
 * - Windows: gcc -std=c11 ... `pkg-config --cflags --libs sdl3` -lopengl32
 * - macOS: clang -std=c11 ... `pkg-config --cflags --libs sdl3` -framework OpenGL
 */

#include <SDL3/SDL.h>
#ifdef __APPLE__
#  define GL_SILENCE_DEPRECATION
#endif
#define GL_GLEXT_PROTOTYPES    // for OpenGL>1.1 imports
#include <SDL3/SDL_opengl.h>

#ifdef _WIN32
# define IMPORT_GL2_SHADER_EXTS() \
    PFNGLSHADERSOURCEPROC  glShaderSource  = (PFNGLSHADERSOURCEPROC)\
        SDL_GL_GetProcAddress("glShaderSource");\
    PFNGLCREATESHADERPROC  glCreateShader  = (PFNGLCREATESHADERPROC)\
        SDL_GL_GetProcAddress("glCreateShader");\
    PFNGLDELETESHADERPROC  glDeleteShader  = (PFNGLDELETESHADERPROC)\
        SDL_GL_GetProcAddress("glDeleteShader");\
    PFNGLCOMPILESHADERPROC glCompileShader = (PFNGLCOMPILESHADERPROC)\
        SDL_GL_GetProcAddress("glCompileShader");\
    PFNGLATTACHSHADERPROC  glAttachShader  = (PFNGLATTACHSHADERPROC)\
        SDL_GL_GetProcAddress("glAttachShader");\
    PFNGLCREATEPROGRAMPROC glCreateProgram = (PFNGLCREATEPROGRAMPROC)\
        SDL_GL_GetProcAddress("glCreateProgram");\
    PFNGLDELETEPROGRAMPROC glDeleteProgram = (PFNGLDELETEPROGRAMPROC)\
        SDL_GL_GetProcAddress("glDeleteProgram");\
    PFNGLLINKPROGRAMPROC   glLinkProgram   = (PFNGLLINKPROGRAMPROC)\
        SDL_GL_GetProcAddress("glLinkProgram");\
    PFNGLUSEPROGRAMPROC    glUseProgram    = (PFNGLUSEPROGRAMPROC)\
        SDL_GL_GetProcAddress("glUseProgram");

# define IMPORT_GL2_VERTEX_EXTS() \
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer =\
        (PFNGLVERTEXATTRIBPOINTERPROC) SDL_GL_GetProcAddress("glVertexAttribPointer");\
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray =\
        (PFNGLENABLEVERTEXATTRIBARRAYPROC) SDL_GL_GetProcAddress("glEnableVertexAttribArray");
#else
#  define IMPORT_GL2_SHADER_EXTS() {}
#  define IMPORT_GL2_VERTEX_EXTS() {}
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

static const GLchar *vertex_shader =
    "#version 120                     \n" // = OpenGL 2.1
    "                                 \n"
    "attribute vec4 p_position;       \n" // 1st attribute: ID 0
    "attribute vec4 p_color;          \n" // 2nd attribute: ID 1
    "varying vec4 v_color;            \n" // = "out" to color shader below
    "                                 \n"
    "void main()                      \n"
    "{                                \n"
    "  v_color = p_color;             \n"
    "  gl_Position = p_position;      \n"  // (builtin)
    "}                                \n";

static const GLchar *color_shader =
    "#version 120                     \n" // = OpenGL 2.1
    "                                 \n"
    "varying vec4 v_color;            \n" // = "in" from vertex shader above
    "                                 \n"
    "void main()                      \n"
    "{                                \n"
    "  gl_FragColor= v_color;         \n" // (builtin)
    "}                                \n";



void redraw(SDL_Window* window)
{
    glViewport(0, 0, _width, _height);                         // size

    glClearColor(0, 0, 0, 255);                                // background (Black)
    glClear(GL_COLOR_BUFFER_BIT);

    IMPORT_GL2_VERTEX_EXTS();

    // ID 0: 'p_position' in shader
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertex_arr);        // 2 lines
    glEnableVertexAttribArray(0);

    // ID 1: 'p_color' in shader (GL_TRUE to normalize: 0 -> 0.0f, 255 -> 1.0f)
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, color_arr); // 4 colors
    glEnableVertexAttribArray(1);
    
    glDrawElements(GL_LINES, 4, GL_UNSIGNED_INT, index_arr);  // 4 points on 4 colors

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

    IMPORT_GL2_SHADER_EXTS();

    GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert_shader, 1, &vertex_shader, NULL);
    glCompileShader(vert_shader);

    GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &color_shader, NULL);
    glCompileShader(frag_shader);

    GLuint program = glCreateProgram();
    glAttachShader(program, vert_shader);
    glAttachShader(program, frag_shader);
    glLinkProgram(program);
    glUseProgram(program);

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

        redraw(window);
    }

  end:
    glDeleteProgram(program);
    glDeleteShader(frag_shader);
    glDeleteShader(vert_shader);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
