// Copyright 2021 Daniel Mensinger
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

/* Exit status to use when launching an AppImage fails.
 * For applications that assign meanings to exit status codes (e.g. rsync),
 * we avoid "cluttering" pre-defined exit status codes by using 127 which
 * is known to alias an application exit status and also known as launcher
 * error, see SYSTEM(3POSIX).
 */
#define EXIT_EXECERROR 127 /* Execution error exit status.  */

typedef struct appimage_context {
    ssize_t fs_offset; // The offset at which a filesystem image is expected = end of this ELF
    char *  appimage_path;
    char *  argv0_path;
} appimage_context_t;

bool appimage_detect_context(appimage_context_t *context, int argc, char *argv[]);

bool appimage_is_writable_directory(char *str);
bool appimage_starts_with(const char *pre, const char *str);
bool appimage_mkdir_p(const char *const path);
bool appimage_rm_recursive(const char *const path);

bool appimage_self_extract(appimage_context_t *const context,
                           const char *const         _prefix,
                           const char *const         _pattern,
                           const bool                overwrite,
                           const bool                verbose);

void appimage_execute_apprun(appimage_context_t *const context,
                             const char *              prefix,
                             int                       argc,
                             char *                    argv[],
                             const char *const         argument_skip_prefix,
                             bool                      enable_portable_support);
