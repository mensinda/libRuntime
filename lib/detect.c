// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#include "libruntime.h"

#define _DEFAULT_SOURCE

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

#include "libappimage/appimage_shared.h"

bool appimage_detect_context(appimage_context_t *context, int argc, char *argv[]) {
    (void)argc;

    /* We might want to operate on a target appimage rather than this file itself,
     * e.g., for appimaged which must not run untrusted code from random AppImages.
     * This variable is intended for use by e.g., appimaged and is subject to
     * change any time. Do not rely on it being present. We might even limit this
     * functionality specifically for builds used by appimaged.
     */
    context->appimage_path = getenv("TARGET_APPIMAGE");
    context->argv0_path    = getenv("TARGET_APPIMAGE");

    if (context->appimage_path == NULL) {
        context->appimage_path = realpath("/proc/self/exe", NULL);
        context->argv0_path    = argv[0];
    }

    if (context->appimage_path == NULL) {
        fprintf(stderr, "Failed to get the path to the current AppImage\n");
        return false;
    }

    // Don't handle setproctitle because we don't have dlopen anyway when
    // statically linking

    // ssize_t offset = appimage_get_elf_size(context->appimage_path);
    ssize_t offset = appimage_get_elf_size("/proc/self/exe");
    if (offset < 0) {
        fprintf(stderr, "Failed to get fs offset for %s\n", context->appimage_path);
        return false;
    }

    context->fs_offset = offset;
    return true;
}
