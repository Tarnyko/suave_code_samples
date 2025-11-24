/*
* sdl3-gles32.c
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

#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#  include <windows.h>
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengles2.h>


#define LINES       2
#define INIT_WIDTH  800
#define INIT_HEIGHT 600

typedef enum { _SHADER, _PROGRAM } _Type;

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
    "#version 320 es                        \n"
    "                                       \n"
    "layout(location=0) in vec4 p_position; \n" // 1st attribute: ID 0
    "layout(location=1) in vec4 p_color1;   \n" // 2nd attribute: ID 1
    "layout(location=2) in vec4 p_color2;   \n" // 3rd attribute: ID 2
    "                                       \n"
    "out VS_OUT {                           \n" // geometry shader needs...
    "  vec4 color1;                         \n" // ... an array as input
    "  vec4 color2;                         \n"
    "} v_color;                             \n"
    "                                       \n"
    "void main()                            \n"
    "{                                      \n"
    "  v_color.color1 = p_color1;           \n"
    "  v_color.color2 = p_color2;           \n"
    "  gl_Position = p_position;            \n"  // (builtin)
    "}                                      \n";

static const GLchar *geometry_shader =
    "#version 320 es                         \n"
    "                                        \n"
    "layout(lines) in;                       \n"
    "layout(line_strip, max_vertices=2) out; \n" // 1 line, 2 points
    "                                        \n"
    "in VS_OUT {                             \n" // from vertex shader...
    "  vec4 color1;                          \n"
    "  vec4 color2;                          \n"
    "} v_color[];                            \n"
    "                                        \n"
    "out vec4 g_color;                       \n" // ...to fragment shader
    "                                        \n"
    "void main()                             \n"
    "{                                       \n"
    "  g_color = v_color[0].color1;          \n"  // color 1
    "  gl_Position = gl_in[0].gl_Position;   \n"  // (builtin) point 1
    "  EmitVertex();                         \n"  // (builtin) stack!
    "                                        \n"
    "  g_color = v_color[0].color2;          \n"  // color 2
    "  gl_Position = gl_in[1].gl_Position;   \n"  // (builtin) point 2
    "  EmitVertex();                         \n"  // (builtin) stack!
    "                                        \n"
    "  EndPrimitive();                       \n"  // (builtin) send!
    "}                                       \n";

static const GLchar *color_shader =
    "#version 320 es                  \n"
    "precision mediump float;         \n"
    "                                 \n"
    "in vec4 g_color;                 \n"
    "out vec4 frag_color;             \n"
    "                                 \n"
    "void main()                      \n"
    "{                                \n"
    "  frag_color = g_color;          \n"
    "}                                \n";



void check_shader_or_program(const char* type_name, _Type type, GLuint id)
{
    GLint res = 0;

    switch(type) {
      case _SHADER : glGetShaderiv(id, GL_COMPILE_STATUS, &res); break;
      case _PROGRAM: glGetProgramiv(id, GL_LINK_STATUS, &res); break;
    }
    if (res == GL_TRUE) {
        return; }

    switch(type) {
      case _SHADER : glGetShaderiv(id, GL_INFO_LOG_LENGTH, &res); break;
      case _PROGRAM: glGetProgramiv(id, GL_INFO_LOG_LENGTH, &res); break;
    }
    if (res > 0) {
        GLchar err[res];
        memset(err, 0, res);
        switch(type) {
          case _SHADER : glGetShaderInfoLog(id, res, &res, err); break;
          case _PROGRAM: glGetProgramInfoLog(id, res, &res, err); break;
        }
        fprintf(stderr, "[%s (ID=%d)] %s", type_name, id, err);
    }
}

void redraw(SDL_Window* window, GLuint* vbos)
{
    glViewport(0, 0, _width, _height);                         // size

    glClearColor(0, 0, 0, 255);                                // background (Black)
    glClear(GL_COLOR_BUFFER_BIT);

    // ID 0: 'p_position' in shader
    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);        // 1 point: 2 floats
    glEnableVertexAttribArray(0);

    // ID 1: 'p_color1' in shader (GL_TRUE to normalize: 0 -> 0.0f, 255 -> 1.0f)
    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0); // 1 color: 4 bytes
    glEnableVertexAttribArray(1);
    // ID 2: 'p_color2' in shader
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2]);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, 0); // 1 color, 2-shifted
    glEnableVertexAttribArray(2);

    // ID 3
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[3]);
    glDrawElements(GL_LINES, 4, GL_UNSIGNED_INT, 0);          // draw 4 points as lines

    SDL_GL_SwapWindow(window);
}


int main (int argc, char* argv[])
{
    SDL_Init(SDL_INIT_VIDEO);

# if defined(_WIN32) && defined(DEBUG)
    AllocConsole();
    freopen("conout$","w",stdout);
    freopen("conout$","w",stderr);
# endif

    SDL_Window* window = SDL_CreateWindow(argv[0], INIT_WIDTH, INIT_HEIGHT, SDL_WINDOW_OPENGL);
    SDL_GLContext context = SDL_GL_CreateContext(window);

    SDL_SetWindowResizable(window, true);

    // 1) Shaders

    GLuint vert_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vert_shader, 1, &vertex_shader, NULL);
    glCompileShader(vert_shader);

    GLuint geom_shader = glCreateShader(GL_GEOMETRY_SHADER_OES);
    glShaderSource(geom_shader, 1, &geometry_shader, NULL);
    glCompileShader(geom_shader);

    GLuint frag_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(frag_shader, 1, &color_shader, NULL);
    glCompileShader(frag_shader);

# ifdef DEBUG
    check_shader_or_program("vertex shader", _SHADER, vert_shader);
    check_shader_or_program("geometry shader", _SHADER, geom_shader);
    check_shader_or_program("fragment shader", _SHADER, frag_shader);
# endif

    GLuint program = glCreateProgram();
    glAttachShader(program, vert_shader);
    glAttachShader(program, geom_shader);
    glAttachShader(program, frag_shader);
    glLinkProgram(program);
# ifdef DEBUG
    check_shader_or_program("program", _PROGRAM, program);
# endif
    glUseProgram(program);

    // 2) VBOs

    GLuint vbos[4];
    glGenBuffers(4, vbos);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_arr), vertex_arr, GL_STATIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, vbos[1]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_arr), color_arr, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, vbos[2]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(color_arr), color_arr+2, GL_STATIC_DRAW); // 2-shifted

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vbos[3]);
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
    glDeleteBuffers(4, vbos);
    glDeleteProgram(program);
    glDeleteShader(geom_shader);
    glDeleteShader(frag_shader);
    glDeleteShader(vert_shader);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
