#ifdef __GNUC__ && defined _GNU_SOURCE
#  warning "Your compiler should already have 'asprintf()'."
#endif

#include <stdarg.h>

int vasprintf(char **sptr, char *fmt, va_list argv)
{
    int wanted = vsnprintf(*sptr = NULL, 0, fmt, argv);
    if((wanted > 0) && ((*sptr = malloc(1 + wanted)) != NULL))
        return vsprintf(*sptr, fmt, argv);

    return wanted;
}

int asprintf(char **sptr, char *fmt, ...)
{
    int retval;
    va_list argv;
    va_start(argv, fmt);
    retval = vasprintf(sptr, fmt, argv);
    va_end(argv);
    return retval;
}