/*
* sdl3-gles2_vbos.c
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
 * - UNIX/Windows: gcc -std=c11 ... `pkg-config --cflags --libs sdl3 glesv2`
 * - macOS:      clang -std=c11 ... -I$ANGLE_PATH/include `pkg-config --cflags
 *                     --libs sdl3` -L$ANGLE_PATH -lGLESv2
 */

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>


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
    "#version 100                     \n" // = "#version 100 es"
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
    "#version 100                     \n" // = "#version 100 es"
    "precision mediump float;         \n"
    "                                 \n"
    "varying vec4 v_color;            \n" // = "in" from vertex shader above
    "                                 \n"
    "void main()                      \n"
    "{                                \n"
    "  gl_FragColor= v_color;         \n" // (builtin)
    "}                                \n";



void redraw(SDL_Window* window, GLuint* vbos)
{
    glViewport(0, 0, _width, _height);                         // size

    glClearColor(0, 0, 0, 255);                                // background (Black)
    glClear(GL_COLOR_BUFFER_BIT);

    // ID 0: 'p_position' in shader
    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);        // 2 lines
    glEnableVertexAttribArray(0);

    // ID 1: 'p_color' in shader (GL_TRUE to normalize: 0 -> 0.0f, 255 -> 1.0f)
    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0); // 4 colors
    glEnableVertexAttribArray(1);
    
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[2]);
    glDrawElements(GL_LINES, 4, GL_UNSIGNED_INT, 0);   // 4 points on 4 colors

    SDL_GL_SwapWindow(window);
}


int main (int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window* window = SDL_CreateWindow(argv[0], INIT_WIDTH, INIT_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(window);

    SDL_SetWindowResizable(window, true);

    // 1) Shaders

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

    // 2) VBOs

    GLuint vbos[3];
    glGenBuffers(3, vbos);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_arr), vertex_arr, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_arr), color_arr, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[2]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(index_arr), index_arr, GL_STATIC_DRAW);

    // 3) Main loop

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
    glDeleteProgram(program);
    glDeleteShader(frag_shader);
    glDeleteShader(vert_shader);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
