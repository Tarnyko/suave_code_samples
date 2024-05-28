//  Compile with:
// gcc -std=c23 -O2 ... ("-O2" is important !)

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h> // for "offsetof()"
#include <string.h> // for "memcmp()"


typedef struct {    // add "__attribute__((__packed__))" in between
    char c;
    int i;
    double f;
    char s[5];
} MyStruct;


void print_memory(MyStruct* s)
{
    char* it = (char*) s; // 1 byte

    while (it < (char*) s + sizeof(MyStruct)) {
        printf("%02x ", *it);
        it++;
    }
    putchar('\n');
}

void compare_memory(MyStruct* a, MyStruct* b)
{
    switch (memcmp(a, b, sizeof(MyStruct))) {
        case 0:  printf("Structures are equal.\n\n"); break;
        default: printf("Structures are NOT equal!\n\n");
    }
}

int main (int argc, char* argv[])
{
    printf("Offsets: c=%zd, i=%zd, f=%zd, s=%zd (size: %zd).\n\n",
        offsetof(MyStruct, c),
        offsetof(MyStruct, i),
        offsetof(MyStruct, f),
        offsetof(MyStruct, s),
        sizeof(MyStruct));

    // 1) compare zero-initialized structs [GCC: OK]

    MyStruct s1 = {0};
    MyStruct s2 = {};  // C23 (always initializes padding to 0)

    printf("s1: ");
    print_memory(&s1);

    printf("s2: ");
    print_memory(&s2);

    compare_memory(&s1, &s2);

    // 2) compare filled structs with(out) zero-init. [GCC: KO if "-O2"]

    MyStruct s3 = { .c = '!', .i = 1, .f = 3.14, .s = "test" };
    MyStruct s4 = {}; s4 = (MyStruct){ .c = '!', .i = 1, .f = 3.14, .s = "test" };

    printf("s3: ");
    print_memory(&s3);

    printf("s4: ");
    print_memory(&s4);

    compare_memory(&s3, &s4);


    printf("\n Press key to continue...\n");
    getchar();

    return EXIT_SUCCESS;
}
