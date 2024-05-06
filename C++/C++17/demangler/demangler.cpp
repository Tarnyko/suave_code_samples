/*
* demangler.cpp
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
// g++ -std=c++17 ...

#if defined(__MINGW32__) && (__GNUC__ < 9)
#  error "MinGW only supports 'std::filesystem' from version 7.x (GCC 9.x)."
#endif
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <cxxabi.h>

int main (int argc, char *argv[])
{
    if (argc < 2) {
        std::cout << " Usage:\n" << argv[0] << " backtrace.txt" << std::endl;
        std::cout << " (Manuel Bachmann (<tarnyko.tarnyko.net>)\n" << std::endl;
        return EXIT_SUCCESS; }

    std::filesystem::path f(argv[1]);
    if (!std::filesystem::exists(f) || !std::filesystem::is_regular_file(f)) {
        std::cerr << "File '" << f <<  "' not found! Exiting..." << std::endl;
        return EXIT_FAILURE; }

    std::ifstream fs(f.c_str());
    if (!fs.is_open()) {
        std::cerr << "Access to file '" << f <<  "' denied! Exiting..." << std::endl;
        return EXIT_FAILURE; }

    int status;
    char *sym, *res;
    size_t bpos, epos;
    std::string line;

    while (std::getline(fs, line)) {
        bpos = line.find("_Z");
        if (bpos == std::string::npos) {
            goto print_line; }

        epos = line.find('+', bpos);
        if (epos == std::string::npos) {
            goto print_line; }
        epos -= bpos;

        sym = &(line.front()) + bpos;
        sym[epos] = '\0';
        res = abi::__cxa_demangle(sym, 0, 0, &status);
        sym[epos] = '+';

        if (status == 0) {
            line.replace(bpos,epos, res); }

      print_line:
          std::cout << line << std::endl;
    }

    fs.close();

    return EXIT_SUCCESS;
}
