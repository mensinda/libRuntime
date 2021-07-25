// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#define _XOPEN_SOURCE 500

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <ftw.h>
#include <libgen.h>

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

int _appimage_rm_recursive_callback(const char *path, const struct stat *stat, const int type, struct FTW *ftw) {
    (void)stat;
    (void)ftw;

    switch (type) {
        case FTW_NS:
        case FTW_DNR: fprintf(stderr, "%s: ftw error: %s\n", path, strerror(errno)); return 1;

        case FTW_D:
            // ignore directories at first, will be handled by FTW_DP
            break;

        case FTW_F:
        case FTW_SL:
        case FTW_SLN:
            if (remove(path) != 0) {
                fprintf(stderr, "Failed to remove %s: %s\n", path, strerror(errno));
                return false;
            }
            break;

        case FTW_DP:
            if (rmdir(path) != 0) {
                fprintf(stderr, "Failed to remove directory %s: %s\n", path, strerror(errno));
                return false;
            }
            break;

        default: fprintf(stderr, "Unexpected fts_info\n"); return 1;
    }

    return 0;
}

bool appimage_rm_recursive(const char *const path) {
    // FTW_DEPTH: perform depth-first search to make sure files are deleted before the containing directories
    // FTW_MOUNT: prevent deletion of files on other mounted filesystems
    // FTW_PHYS: do not follow symlinks, but report symlinks as such; this way, the symlink targets, which might point
    //           to locations outside path will not be deleted accidentally (attackers might abuse this)
    int rv = nftw(path, &_appimage_rm_recursive_callback, 0, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);

    return rv == 0;
}

char *appimage_generate_mount_path(appimage_context_t *const context, const char *const prefix) {
    const size_t maxnamelen = 6;

    // when running for another AppImage, we should use that for building the mountpoint name instead
    char *target_appimage = getenv("TARGET_APPIMAGE");

    char *path_basename;
    if (target_appimage != NULL) {
        path_basename = basename(target_appimage);
    } else {
        path_basename = basename(context->appimage_path);
    }

    size_t namelen = strlen(path_basename);
    // limit length of tempdir name
    if (namelen > maxnamelen) {
        namelen = maxnamelen;
    }

    const char * temp_base = prefix ? prefix : context->temp_base;
    size_t templen   = strlen(temp_base);
    char * mount_dir = malloc(templen + 8 + namelen + 6 + 1);

    strcpy(mount_dir, temp_base);
    strcat(mount_dir, "/.mount_");
    strncat(mount_dir, path_basename, namelen);
    strcat(mount_dir, "XXXXXX");
    mount_dir[templen + 8 + namelen + 6] = 0; // null terminate destination

    if (mkdtemp(mount_dir) == NULL) {
        perror("create mount dir error");
        return NULL;
    }
    return mount_dir;
}
