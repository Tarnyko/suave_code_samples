/*
* safety_first.cpp
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

/*  Compile with:
 *
 *  # UNSAFE
 *  g++ -std=c++17 -O2 ...
 *
 *  # SAFE
 *  circle --std=c++17 --O2 -DSAFE -I<path-to-std2-h> ...
 */


// Circle w/ SafeCPP
#ifdef SAFE
#  feature on safety
#  include <std2.h>
   using namespace std2;
#  define SAFE_IF_POSSIBLE safe
#  define MUT mut
#  define CPY cpy
#  define REL rel
#  define REF ^
#  define REF_ARG

// GCC
#else
#  include <iostream>
#  include <vector>
#  include <string>
#  include <string_view>
   using namespace std;
#  define println(X) cout << X << endl
#  define SAFE_IF_POSSIBLE
#  define MUT
#  define CPY
#  define REL
#  define REF
#  define REF_ARG &
#endif


void display(const vector<string_view> REF_ARG v)
 SAFE_IF_POSSIBLE
{
    string d("");

    for (string_view sv : v)
    {
        d = d + string(sv);
    }

    println(d);
}


int main()
 SAFE_IF_POSSIBLE
{
    vector<string_view> v
# ifdef SAFE
    { }       // /!\ SAFE mode requires initialization
# endif
    ;

    // 1] OK: add "sv1" to "v" (both have same scope/lifetime)

    string s1("Hello");
    string_view sv1(s1);

    MUT v.push_back(sv1);  // /!\ SAFE mode requires "mut" here

    display(REF v);      // /!\ SAFE wants "^param", not "&arg"


    // 2] KO: same, but here "sv2" has a shorter lifetime...
    {
        string s2(" cruel world!");
        string_view sv2(s2);

        MUT v.push_back(sv2);

        display(REF v);

        // ...end of scope: "sv2" is destroyed here
    }


    // 3] allocate more things to add a pinch of chaos

    string s3("secret: #9yh7u!");
    string_view sv3(s3);


    display(REF v);  // Undefined Behavior ☠ (rejected if SAFE)

    return 0;
}
