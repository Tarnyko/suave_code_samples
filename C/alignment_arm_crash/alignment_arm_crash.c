 /*  Compile with:
  * arm-linux-*eabi-gcc --sysroot=$SDK -mfpu=vfp -mfloat-abi=hard -O0 -munaligned-access ...
  *  or:
  * arm-linux-*eabi-gcc -DFIX -std=c11 --sysroot=$SDK -mfpu=vfp -mfloat-abi=hard -O0 -munaligned-access ...
  *
  *  Reproduce with: 
  * echo 2 > /proc/cpu/alignment
  * dmesg | grep alignment
  * ./a.out
  */

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef FIX
#  if __STDC_VERSION__ < 201112L
#    error "Fix with 'alignas()' requires C11!"
#  endif

#  ifndef __alignas_is_defined
#    include <stdalign.h>        // for "alignas()"
#  endif
#endif


#ifdef FIX

  typedef struct {
      alignas(2) unsigned char size;   // offset: 2 bytes [max size:  UCHAR_MAX=255]
      unsigned short idx;              // offset: 2 bytes [max index: USHRT_MAX=65535]
      alignas(2) char text[1];         // offset: 2 bytes (include the final '\0' in the size)
  } MyElem;

  static_assert(offsetof(MyElem, idx) % 2 == 0, "'idx' member misaligned!");
  static_assert(sizeof(MyElem) == 6);  // (will be 8 for performance unless we align "text")

#else

  typedef struct __attribute__((packed)) {
      unsigned char size;   // offset: 1 byte  [max size:  UCHAR_MAX=255]
      unsigned short idx;   // offset: 2 bytes [max index: USHRT_MAX=65535]
      char text[1];         // offset: 1 byte  (include the final '\0' in the size)
  } MyElem;

#  if __STDC_VERSION__ >= 201112L
    static_assert(offsetof(MyElem, idx) % 2 != 0, "'idx' should be misaligned here ;-)");
    static_assert(sizeof(MyElem) == 4);  // (smallest possible, but now "idx" is misaligned)
#  endif

#endif

typedef struct {
    size_t size;
    MyElem elems[USHRT_MAX];   // [max size: UCHAR_MAX * USHRT_MAX]   
} MyList;


void add_element(MyList* list, short idx, const char* text)
{
# ifdef FIX
    // we "-1" because we have a free character, but "+ 1) & ~1" to align to multiples of 2
    size_t elem_size = (sizeof(MyElem) + strlen(text)-1 + 1) & ~1;
# else
    size_t elem_size = sizeof(MyElem) + strlen(text);
# endif
 
    assert(elem_size < UCHAR_MAX);
    assert(idx < USHRT_MAX);
    assert(list->size + elem_size < UCHAR_MAX * USHRT_MAX);

    // cast a pointer to the next position of the list's stack
    MyElem* elem = (MyElem*) ((char*)list->elems + list->size);

    // fill element
    memcpy(elem->text, text, strlen(text));
    elem->idx  = idx;
    elem->size = elem_size;

    list->size += elem_size;    
}

void print_elements(const MyList* list)
{
    size_t cur_size = 0;

    while (cur_size < list->size) {
        MyElem* elem = (MyElem*) ((char*)list->elems + cur_size);

#    ifdef DEBUG
        if ( ((uintptr_t)(elem) % 2 != 0) ||
             ((uintptr_t)(&elem->idx) % 2 != 0) ) {
            fprintf(stderr, "'elem->idx' is misaligned and may cause crashes on ARM!\n"); }
#    endif
        printf("%d: ", elem->idx);   // CRASH HERE! (2 bytes on offset 1)
        printf("%s\n", elem->text);

        cur_size += elem->size;
   }
}


int main(int argc, char *argv[])
{
    MyList list = {0};

    add_element(&list, 0, "Hello");
    add_element(&list, 1, "World");
    add_element(&list, 2, "!");

    print_elements(&list);

    puts("Press [Return] to continue...");
    getchar();

    return 0;
}
