/*
* c_analyzer.c
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

//  Compile with:
// * Linux:   gcc -std=c23 ...
// * Windows: gcc -std=c23 -I_deps _deps/getline.c ...

#define _GNU_SOURCE   // for "popen()/pclose()","asprintf()","getline()"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>

#include "plugins/plugin.h"

#ifdef _WIN32
#  include <windows.h>
#  define popen  _popen
#  define pclose _pclose
   extern ssize_t getline(char **restrict l, size_t *restrict n, FILE *restrict s);
   constexpr char DIR_SEP[] = {'\\','/'};
   constexpr char PLG_EXT[] = ".dll";
   constexpr char CLEAR_COMMAND[] = "cls";
#else
#  define MAX_PATH 260
   constexpr char DIR_SEP[] = {'/'};
   constexpr char PLG_EXT[] = ".so";
   constexpr char CLEAR_COMMAND[] = "clear";
#endif
constexpr char PLG_DIR[] = "plugins/";

#if __has_attribute(__counted_by__)
#  define __counted_by(member)  __attribute__((__counted_by__(member)))
#else
#  define __counted_by(member)
#endif

size_t __;
#define GETLINE(X,Y)        getline((X),&__,(Y))
#define ASPRINTF(X,Y,Z,...) assert(asprintf((X),(Y),(Z) __VA_OPT__(,) __VA_ARGS__) != -1)
#define FOR_EACH(X,Y)       for (typeof(Y) (X) = 0; (X) < (Y); (X)++)


typedef struct {
    char* path;
    FILE* file;
} File;

typedef struct {
    size_t count;
    File* files[] __counted_by(count);
} Files;

typedef struct {
    size_t count;
    Plugin* plugins[] __counted_by(count);
} Plugins;


// FILE PARSING

typedef enum : signed char {
    E_UNTESTED = -1, E_FALSE = false, E_TRUE = true
} Etest;


bool file_is_c_source(const char* path)
{
    constexpr char FILE_COMMAND[] = "file %s";

    bool res = false;
    static Etest has_file_tool = E_UNTESTED;

    if (has_file_tool == E_UNTESTED) {
        auto f = popen(FILE_COMMAND, "r");
        has_file_tool = (f && abs(pclose(f)) != 1) ? true : false;
        system(CLEAR_COMMAND);
    }

    if (has_file_tool) {
        char *cmd, *out = nullptr;
        FILE* fout = nullptr;

        ASPRINTF(&cmd, FILE_COMMAND, path);
        if ((fout = popen(cmd, "r")) &&
              GETLINE(&out, fout) != -1 &&
                abs(pclose(fout)) != 1) {
            res = strstr(out, "C source");
            free(out);
        }
        free(cmd);
    } else {
        char* ext = nullptr;

        if ((ext = strrchr(path, '.')) &&
              (!strcmp(ext, ".c") || !strcmp(ext, ".h"))) {
            res = true; }
    }

    return res;
}

bool open_files(Files** out, const char** paths, const int count)
{
    auto files = *out;

    FOR_EACH(c, (int) count)
    {
        FILE* f = nullptr;
        if (!(f = fopen(paths[c], "r"))) {
            fprintf(stderr, "File '%s' not found: ignored.\n", paths[c]);
            continue;
        }
        if (!file_is_c_source(paths[c])) {
            fprintf(stderr, "File '%s' is not C: ignored.\n", paths[c]);
            fclose(f);
            continue;
        }

        File* file = calloc(1, sizeof(File));
        file->path = (char*) paths[c];
        file->file = f;

        files->count++;
        files = realloc(files, sizeof(Files) + files->count*sizeof(File*));
        files->files[files->count-1] = file;
    }

    *out = files;
    return (files->count > 0);
}

void analyze_file(File* file, Plugins* plugins)
{
    FOR_EACH(c, plugins->count)
    {
        bool res = false;
        auto plugin = plugins->plugins[c];

        rewind(file->file);

        switch (plugin->method)
        {
          case E_BOTH: [[fallthrough]];
          case E_LINE:
              char* line = nullptr;
              size_t num = 0;

              while (GETLINE(&line, file->file) != -1) {
                  res |= plugin->analyze_line(plugin, line, ++num); }
              if (line) {
                  free(line); }
              if (res) {
                  goto err; }
              [[fallthrough]];
          case E_BLOCK:
              // TODO: handle this
              [[fallthrough]];
          default:
              continue;
          err:
              fprintf(stderr, "[File] '%s':\n", file->path);
              report_err(&plugin->err);
        }
    }
}

void analyze_files(Files* files, Plugins* plugins)
{
    FOR_EACH(c, files->count) {
        analyze_file(files->files[c], plugins); }
}

void close_files(Files* files)
{
    FOR_EACH(c, files->count) {
        fclose(files->files[c]->file);
        free(files->files[c]);
    }
}


// PLUGIN LOADING

[[nodiscard("Leaking unused string")]]
char* get_parent_path(const char* path)
{
    char* parent = nullptr;

    FOR_EACH(s, sizeof(DIR_SEP)) {
        char* pos = nullptr;
        if (!(pos = strrchr(path, DIR_SEP[s]))) {
            continue; }

        parent = calloc(pos - path + 1, sizeof(char));
        strncpy(parent, path, pos - path);
        break;
    }

    return parent;
}

char* get_executable_path([[maybe_unused]] const char* arg)
{
    constexpr char WHICH_COMMAND[] = "which %s";
    constexpr char err[] = "%s: could not retrieve executable directory.\n";

    char path[MAX_PATH] = {};

# ifdef _WIN32
    auto res = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (!res || res == sizeof(path)) {
        fprintf(stderr, err, "Windows");
        return nullptr;
    }
# else
#  ifdef __linux__
    if (readlink("/proc/self/exe", path, sizeof(path)) == -1) {
        fprintf(stderr, err, "Linux");
#  endif
    auto parent = get_parent_path(arg);
    if (parent) {
        return parent; }

    char* cmd = nullptr;
    FILE* fout = nullptr;

    ASPRINTF(&cmd, WHICH_COMMAND, arg);
    if (fout = popen(cmd, "r")) {
        fgets(path, sizeof(path)-1, fout); }
    free(cmd);
    if (!strlen(path) || pclose(fout) == -1) {
        fprintf(stderr, err, "UNIX");
        return nullptr;
    }
#  ifdef __linux__
    }
#  endif
# endif

    return get_parent_path(path);
}

[[nodiscard("Leaking unused Plugin")]]
Plugin* load_plugin(const char* path, const char* name)
{
    constexpr char err[] = "Plugin '%s' %s: ignored.\n";

    char* full_path = nullptr;
    ASPRINTF(&full_path, "%s%c%s", path, DIR_SEP[0], name);

    auto plg = PLG_OPEN(full_path);
    free(full_path);
    if (!plg) {
        fprintf(stderr, err, name, "not a valid binary");
        return nullptr;
    }
    PLG_ERR();

    PLG_LOAD plg_load = (PLG_LOAD) PLG_SYM(plg, "load");
    if (!plg_load || PLG_ERR()) {
        fprintf(stderr, err, name, "has no 'load' symbol'");
        return nullptr;
    }

    return plg_load(plg, name);
}

bool load_plugins(Plugins** out, const char* arg)
{
    auto plugins = *out;

    auto exe_path = get_executable_path(arg);
    if (!exe_path) {
        return false; }

    char* dir_path     = nullptr;
    DIR* dir           = nullptr;
    struct dirent* ent = nullptr;

    asprintf(&dir_path, "%s%c%s", exe_path, DIR_SEP[0], PLG_DIR);
    free(exe_path);

    if (!(dir = opendir(dir_path))) {
        fprintf(stderr, "Directory '%s' not found.\n", dir_path);
        free(dir_path);
        return false;
    }
    while (ent = readdir(dir)) {
        char* ext = nullptr;
        if (!(ext = strrchr(ent->d_name, '.')) || strcmp(ext, PLG_EXT)) {
            continue; }

        auto plugin = load_plugin(dir_path, ent->d_name);
        if (!plugin) {
            continue; }

        plugins->count++;
        plugins = realloc(plugins, sizeof(Plugins) + plugins->count*sizeof(Plugin*));
        plugins->plugins[plugins->count-1] = plugin;

        printf("[Plugin loaded] Name: '%s', Title: '%s', Version: '%d', Comment: '%s'\n",
                 plugin->name, plugin->title, plugin->version, plugin->comment);
    }
    closedir(dir);
    free(dir_path);
    putchar('\n');

    *out = plugins;
    return (plugins->count > 0);
}

void unload_plugins(Plugins* plugins)
{
    FOR_EACH(c, plugins->count) {
        auto plugin = plugins->plugins[c];
        plugin->unload(plugin);
        PLG_CLOSE(plugin->handle);
        free(plugin);
    }
}


int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s <file1>.c <file2>.c ...\n\n", argv[0]);
        return EXIT_SUCCESS;
    }

    Files* files = calloc(1, sizeof(Files));

    if (!open_files(&files, (const char**) argv+1, argc-1)) {
        fprintf(stderr, "[ERROR] No valid source file found! Exiting...\n");
        free(files);
        return EXIT_FAILURE;
    }

    Plugins* plugins = calloc(1, sizeof(Plugins));

    if (!load_plugins(&plugins, (const char*) argv[0])) {
        fprintf(stderr, "[ERROR] No valid plugin found! Exiting...\n");
        free(plugins); free(files);
        return EXIT_FAILURE;
    }

    analyze_files(files, plugins);

    unload_plugins(plugins);
    free(plugins);

    close_files(files);
    free(files);

    return EXIT_SUCCESS;
}
