/*
* landlock.c
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

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,13,0)
#  error "Landlock requires Linux kernel >= 5.13"
#endif

#include <linux/landlock.h>

#define _GNU_SOURCE      // for 'O_PATH'
#include <fcntl.h>       // for 'open()'

#include <sys/syscall.h> // for 'SYS_landlock_*'
#include <unistd.h>      // for 'syscall()'
#include <stdint.h>      // for uint32_t

#include <linux/prctl.h> // for 'PR_*'
#include <sys/prctl.h>   // for 'prctl()'

#include <stdio.h>
#include <stdlib.h>


#ifndef landlock_create_ruleset
static inline int landlock_create_ruleset(const struct landlock_ruleset_attr* attr,
                                          const size_t size, const uint32_t flags)
{
    return syscall(SYS_landlock_create_ruleset, attr, size, flags);
}
#endif

#ifndef landlock_add_rule
static inline int landlock_add_rule(const int fd, const enum landlock_rule_type type,
                                    const void* attr, const uint32_t flags)
{
    return syscall(SYS_landlock_add_rule, fd, type, attr, flags);
}
#endif

#ifndef landlock_restrict_self
static inline int landlock_restrict_self(const int fd, const uint32_t flags)
{
    return syscall(SYS_landlock_restrict_self, fd, flags);
}
#endif



int main(int argc, char* argv[])
{
    if (argc < 2) {
        printf("Usage: %s <filename>\n\n", argv[0]);
        return EXIT_SUCCESS;
    }

    int res = EXIT_SUCCESS;


# ifndef DISABLE_LANDLOCK
    struct landlock_ruleset_attr rules = {
        .handled_access_fs = LANDLOCK_ACCESS_FS_READ_FILE
    };
    struct landlock_path_beneath_attr path_rules = {
        .allowed_access = LANDLOCK_ACCESS_FS_READ_FILE,
        .parent_fd = open("/tmp", O_PATH|O_CLOEXEC)
    };

    int fd = landlock_create_ruleset(&rules, sizeof(rules), 0);

    if (landlock_add_rule(fd, LANDLOCK_RULE_PATH_BENEATH, &path_rules, 0)) {
        fprintf(stderr, "Could not add Landlock path rule, exiting...\n");
        goto error;
    }

    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)) {
        fprintf(stderr, "Could not restrict privileges, exiting...\n");
        goto error;
    }

    if (landlock_restrict_self(fd, 0)) {
        fprintf(stderr, "Could not enforce Landlock policy, exiting...\n");
        goto error;
    }
# endif


    FILE* file = fopen(argv[1], "r");
    if (file) {
        printf("Successfully opened '%s'!\n", argv[1]);
        fclose(file);
    } else {
        printf("Could not open '%s'...\n", argv[1]);
    }

    goto end;

  error:
    res = EXIT_FAILURE;
    
  end:
# ifndef DISABLE_LANDLOCK
    close(path_rules.parent_fd);
    close(fd);
# endif
    return res;
}

