/**************************************************************************
 *
 * Copyright (c) 2021 Daniel Mensinger
 * Copyright (c) 2004-18 Simon Peter
 * Portions Copyright (c) 2007 Alexander Larsson
 *
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/

// #ident "AppImage by Simon Peter, http://appimage.org/"

#define _GNU_SOURCE

#ifndef PROJECT_VERSION
#define PROJECT_VERSION "<UNDEFINED>"
#endif

#include <squashfuse.h>
#include <squashfs_fs.h>
#include <nonstd.h>
#include <stdbool.h>

#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <fnmatch.h>

#include "libappimage/appimage_shared.h"
#include "libappimage/md5.h"
#include "libruntime.h"

char *getArg(int argc, char *argv[], char chr) {
    int i;
    for (i = 1; i < argc; ++i) {
        if ((argv[i][0] == '-') && (argv[i][1] == chr)) {
            return &(argv[i][2]);
        }
    }
    return NULL;
}

void print_help(const char *appimage_path) {
    // TODO: "--appimage-list                 List content from embedded filesystem image\n"
    fprintf(stderr,
            "AppImage options:\n\n"
            "  --appimage-extract [<pattern>]  Extract content from embedded filesystem image\n"
            "                                  If pattern is passed, only extract matching files\n"
            "  --appimage-extract-and-run      Extracts the AppImage into a temporary directory\n"
            "                                  and then executes it\n"
            "  --appimage-help                 Print this help\n"
            "  --appimage-mount                Mount embedded filesystem image and print\n"
            "                                  mount point and wait for kill with Ctrl-C\n"
            "  --appimage-offset               Print byte offset to start of embedded\n"
            "                                  filesystem image\n"
            "  --appimage-portable-home        Create a portable home folder to use as $HOME\n"
            "  --appimage-portable-config      Create a portable config folder to use as\n"
            "                                  $XDG_CONFIG_HOME\n"
            "  --appimage-signature            Print digital signature embedded in AppImage\n"
            "  --appimage-updateinfo[rmation]  Print update info embedded in AppImage\n"
            "  --appimage-version              Print the version of the AppImage runtime\n"
            "\n"
            "Portable home:\n"
            "\n"
            "  If you would like the application contained inside this AppImage to store its\n"
            "  data alongside this AppImage rather than in your home directory, then you can\n"
            "  place a directory named\n"
            "\n"
            "  %s.home\n"
            "\n"
            "  Or you can invoke this AppImage with the --appimage-portable-home option,\n"
            "  which will create this directory for you. As long as the directory exists\n"
            "  and is neither moved nor renamed, the application contained inside this\n"
            "  AppImage to store its data in this directory rather than in your home\n"
            "  directory\n",
            appimage_path);
}

void portable_option(const char *arg, const char *appimage_path, const char *name) {
    char option[32];
    sprintf(option, "appimage-portable-%s", name);

    if (arg && strcmp(arg, option) == 0) {
        char portable_dir[PATH_MAX];
        char fullpath[PATH_MAX];

        ssize_t length = readlink(appimage_path, fullpath, sizeof(fullpath));
        if (length < 0) {
            fprintf(stderr, "Error getting realpath for %s\n", appimage_path);
            exit(EXIT_FAILURE);
        }
        fullpath[length] = '\0';

        sprintf(portable_dir, "%s.%s", fullpath, name);
        if (!mkdir(portable_dir, S_IRWXU))
            fprintf(stderr, "Portable %s directory created at %s\n", name, portable_dir);
        else
            fprintf(stderr, "Error creating portable %s directory at %s: %s\n", name, portable_dir, strerror(errno));

        exit(0);
    }
}

typedef struct mount_data {
    char * arg;
    char * mount_dir;
    int    argc;
    char **argv;
} mount_data_t;

void mounted_cb(appimage_context_t *const context, void *data_raw) {
    mount_data_t *data = (mount_data_t *)data_raw;
    if (data->arg && strcmp(data->arg, "appimage-mount") == 0) {
        char real_mount_dir[PATH_MAX];

        if (realpath(data->mount_dir, real_mount_dir) == real_mount_dir) {
            printf("%s\n", real_mount_dir);
        } else {
            printf("%s\n", data->mount_dir);
        }

        // stdout is, by default, buffered (unlike stderr), therefore in order to allow other processes to read
        // the path from stdout, we need to flush the buffers now
        // this is a less-invasive alternative to setbuf(stdout, NULL);
        fflush(stdout);

        for (;;) pause();

        exit(0);
    }

    appimage_execute_apprun(context, data->mount_dir, data->argc, data->argv, "--appimage", true);
}

int main(int argc, char *argv[]) {
    char *             arg;
    appimage_context_t context;

    if (appimage_detect_context(&context, argc, argv) == false) {
        exit(EXIT_EXECERROR);
    }

    arg = getArg(argc, argv, '-');

    /* Print the help and then exit */
    if (arg && strcmp(arg, "appimage-help") == 0) {
        print_help(context.appimage_path);
        exit(0);
    }

    /* Just print the offset and then exit */
    if (arg && strcmp(arg, "appimage-offset") == 0) {
        printf("%lu\n", context.fs_offset);
        exit(0);
    }

    arg = getArg(argc, argv, '-');

    /* extract the AppImage */
    if (arg && strcmp(arg, "appimage-extract") == 0) {
        char *pattern;

        // default use case: use standard prefix
        if (argc == 2) {
            pattern = NULL;
        } else if (argc == 3) {
            pattern = argv[2];
        } else {
            fprintf(stderr, "Unexpected argument count: %d\n", argc - 1);
            fprintf(stderr, "Usage: %s --appimage-extract [<prefix>]\n", context.argv0_path);
            exit(1);
        }

        if (!appimage_self_extract(&context, "squashfs-root/", pattern, true, true)) {
            exit(1);
        }

        exit(0);
    }

    if (getenv("APPIMAGE_EXTRACT_AND_RUN") != NULL || (arg && strcmp(arg, "appimage-extract-and-run") == 0)) {
        char *hexlified_digest = NULL;

        // calculate MD5 hash of file, and use it to make extracted directory name "content-aware"
        // see https://github.com/AppImage/AppImageKit/issues/841 for more information
        {
            FILE *f = fopen(context.appimage_path, "rb");
            if (f == NULL) {
                perror("Failed to open AppImage file");
                exit(EXIT_EXECERROR);
            }

            Md5Context ctx;
            Md5Initialise(&ctx);

            char buf[4096];
            for (size_t bytes_read; (bytes_read = fread(buf, sizeof(char), sizeof(buf), f));) {
                Md5Update(&ctx, buf, (uint32_t)bytes_read);
            }

            MD5_HASH digest;
            Md5Finalise(&ctx, &digest);

            hexlified_digest = appimage_hexlify(digest.bytes, sizeof(digest.bytes));
        }

        char *prefix = malloc(strlen(context.temp_base) + 20 + strlen(hexlified_digest) + 2);
        strcpy(prefix, context.temp_base);
        strcat(prefix, "/appimage_extracted_");
        strcat(prefix, hexlified_digest);
        free(hexlified_digest);

        const bool verbose = (getenv("VERBOSE") != NULL);

        if (!appimage_self_extract(&context, prefix, NULL, false, verbose)) {
            fprintf(stderr, "Failed to extract AppImage\n");
            exit(EXIT_EXECERROR);
        }

        int pid;
        if ((pid = fork()) == -1) {
            int error = errno;
            fprintf(stderr, "fork() failed: %s\n", strerror(error));
            exit(EXIT_EXECERROR);
        } else if (pid == 0) {
            appimage_execute_apprun(&context, prefix, argc, argv, "--appimage", true);
        }

        int status = 0;
        int rv     = waitpid(pid, &status, 0);
        status     = rv > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_EXECERROR;

        if (getenv("NO_CLEANUP") == NULL) {
            if (!appimage_rm_recursive(prefix)) {
                fprintf(stderr, "Failed to clean up cache directory\n");
                if (status == 0) /* avoid messing existing failure exit status */
                    status = EXIT_EXECERROR;
            }
        }

        // template == prefix, must be freed only once
        free(prefix);

        exit(status);
    }

    if (arg && strcmp(arg, "appimage-version") == 0) {
        fprintf(stderr, "Version: %s\n", PROJECT_VERSION);
        exit(0);
    }

    if (arg && (strcmp(arg, "appimage-updateinformation") == 0 || strcmp(arg, "appimage-updateinfo") == 0)) {
        unsigned long offset = 0;
        unsigned long length = 0;
        appimage_get_elf_section_offset_and_length(context.appimage_path, ".upd_info", &offset, &length);
        // fprintf(stderr, "offset: %lu\n", offset);
        // fprintf(stderr, "length: %lu\n", length);
        // print_hex(appimage_path, offset, length);
        appimage_print_binary(context.appimage_path, offset, length);
        exit(0);
    }

    if (arg && strcmp(arg, "appimage-signature") == 0) {
        unsigned long offset = 0;
        unsigned long length = 0;
        appimage_get_elf_section_offset_and_length(context.appimage_path, ".sha256_sig", &offset, &length);
        // fprintf(stderr, "offset: %lu\n", offset);
        // fprintf(stderr, "length: %lu\n", length);
        // print_hex(appimage_path, offset, length);
        appimage_print_binary(context.appimage_path, offset, length);
        exit(0);
    }

    portable_option(arg, context.appimage_path, "home");
    portable_option(arg, context.appimage_path, "config");

    // If there is an argument starting with appimage- (but not appimage-mount which is handled further down)
    // then stop here and print an error message
    if ((arg && strncmp(arg, "appimage-", 8) == 0) && (arg && strcmp(arg, "appimage-mount") != 0)) {
        fprintf(stderr, "--%s is not yet implemented in version %s\n", arg, PROJECT_VERSION);
        exit(EXIT_EXECERROR);
    }

    // allocate enough memory (size of name won't exceed 60 bytes)
    char *       mount_dir = appimage_generate_mount_path(&context, NULL);
    mount_data_t cb_data;
    cb_data.arg       = arg;
    cb_data.mount_dir = mount_dir;
    cb_data.argc      = argc;
    cb_data.argv      = argv;

    if (!appimage_self_mount(&context, mount_dir, &mounted_cb, &cb_data)) {
        exit(EXIT_EXECERROR);
    }

    return 0;
}
