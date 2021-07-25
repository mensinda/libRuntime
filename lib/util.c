// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "libruntime.h"

/* Check whether directory is writable */
bool appimage_is_writable_directory(char *str) {
    if (access(str, W_OK) == 0) {
        return true;
    } else {
        return false;
    }
}

bool appimage_starts_with(const char *pre, const char *str) {
    size_t lenpre = strlen(pre), lenstr = strlen(str);
    return lenstr < lenpre ? false : strncmp(pre, str, lenpre) == 0;
}

/* mkdir -p implemented in C, needed for https://github.com/AppImage/AppImageKit/issues/333
 * https://gist.github.com/JonathonReinhart/8c0d90191c38af2dcadb102c4e202950
 *
 * Modified to get rid of PATH_MAX
 */
bool appimage_mkdir_p(const char *const path) {
    /* Adapted from http://stackoverflow.com/a/2336245/119527 */
    const size_t len   = strlen(path);
    char *       _path = malloc(sizeof(char) * (len + 1));
    char *       p;

    errno = 0;
    strncpy(_path, path, len);
    _path[len] = '\0';

    /* Iterate the string */
    for (p = _path + 1; *p; p++) {
        if (*p == '/') {
            /* Temporarily truncate */
            *p = '\0';

            if (mkdir(_path, 0755) != 0) {
                if (errno != EEXIST) {
                    goto cleanup;
                }
            }

            *p = '/';
        }
    }

    if (mkdir(_path, 0755) != 0) {
        if (errno != EEXIST) {
            goto cleanup;
        }
    }

    errno = errno == EEXIST ? 0 : errno;

cleanup:
    free(_path);
    return errno == 0 ? true : false;
}
