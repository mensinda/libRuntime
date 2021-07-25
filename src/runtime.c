/**************************************************************************
 *
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

#ifndef GIT_COMMIT
#define GIT_COMMIT "<UNDEFINED>"
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
#include "private.h"

static pid_t fuse_pid;
static int   keepalive_pipe[2];

static void *write_pipe_thread(void *arg) {
    char c[32];
    int  res;
    //  sprintf(stderr, "Called write_pipe_thread");
    memset(c, 'x', sizeof(c));
    while (1) {
        /* Write until we block, on broken pipe, exit */
        res = write(keepalive_pipe[1], c, sizeof(c));
        if (res == -1) {
            kill(fuse_pid, SIGTERM);
            break;
        }
    }
    return NULL;
}

void fuse_mounted(void) {
    pthread_t thread;
    fuse_pid = getpid();
    pthread_create(&thread, NULL, write_pipe_thread, keepalive_pipe);
}

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
            "  --appimage-version              Print version of AppImageKit\n"
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

int rm_recursive_callback(const char *path, const struct stat *stat, const int type, struct FTW *ftw) {
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
};

bool rm_recursive(const char *const path) {
    // FTW_DEPTH: perform depth-first search to make sure files are deleted before the containing directories
    // FTW_MOUNT: prevent deletion of files on other mounted filesystems
    // FTW_PHYS: do not follow symlinks, but report symlinks as such; this way, the symlink targets, which might point
    //           to locations outside path will not be deleted accidentally (attackers might abuse this)
    int rv = nftw(path, &rm_recursive_callback, 0, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);

    return rv == 0;
}

bool build_mount_point(char *mount_dir, const char *const argv0, char const *const temp_base, const size_t templen) {
    const size_t maxnamelen = 6;

    // when running for another AppImage, we should use that for building the mountpoint name instead
    char *target_appimage = getenv("TARGET_APPIMAGE");

    char *path_basename;
    if (target_appimage != NULL) {
        path_basename = basename(target_appimage);
    } else {
        path_basename = basename(argv0);
    }

    size_t namelen = strlen(path_basename);
    // limit length of tempdir name
    if (namelen > maxnamelen) {
        namelen = maxnamelen;
    }

    strcpy(mount_dir, temp_base);
    strncpy(mount_dir + templen, "/.mount_", 8);
    strncpy(mount_dir + templen + 8, path_basename, namelen);
    strncpy(mount_dir + templen + 8 + namelen, "XXXXXX", 6);
    mount_dir[templen + 8 + namelen + 6] = 0; // null terminate destination
}

int main(int argc, char *argv[]) {
    char *             arg;
    appimage_context_t context;

    if (appimage_detect_context(&context, argc, argv) == false) {
        exit(EXIT_EXECERROR);
    }

    // temporary directories are required in a few places
    // therefore we implement the detection of the temp base dir at the top of the code to avoid redundancy
    char *temp_base = P_tmpdir;

    {
        const char *const TMPDIR = getenv("TMPDIR");
        if (TMPDIR != NULL) {
            // Yes this will leak memory, but should be fine for this use-case.
            size_t len = strlen(TMPDIR);
            temp_base  = malloc(len + 1);
            strncpy(temp_base, TMPDIR, len);
            temp_base[len] = '\0';
        }
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
            for (size_t bytes_read; (bytes_read = fread(buf, sizeof(char), sizeof(buf), f)); bytes_read > 0) {
                Md5Update(&ctx, buf, (uint32_t)bytes_read);
            }

            MD5_HASH digest;
            Md5Finalise(&ctx, &digest);

            hexlified_digest = appimage_hexlify(digest.bytes, sizeof(digest.bytes));
        }

        char *prefix = malloc(strlen(temp_base) + 20 + strlen(hexlified_digest) + 2);
        strcpy(prefix, temp_base);
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
            if (!rm_recursive(prefix)) {
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
        fprintf(stderr, "Version: %s\n", GIT_COMMIT);
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
        fprintf(stderr, "--%s is not yet implemented in version %s\n", arg, GIT_COMMIT);
        exit(1);
    }

    int dir_fd, res;

    size_t templen = strlen(temp_base);

    // allocate enough memory (size of name won't exceed 60 bytes)
    char mount_dir[templen + 60];

    build_mount_point(mount_dir, argv[0], temp_base, templen);

    size_t mount_dir_size = strlen(mount_dir);
    pid_t  pid;
    char **real_argv;
    int    i;

    if (mkdtemp(mount_dir) == NULL) {
        perror("create mount dir error");
        exit(EXIT_EXECERROR);
    }

    if (pipe(keepalive_pipe) == -1) {
        perror("pipe error");
        exit(EXIT_EXECERROR);
    }

    pid = fork();
    if (pid == -1) {
        perror("fork error");
        exit(EXIT_EXECERROR);
    }

    if (pid == 0) {
        /* in child */

        char *child_argv[5];

        /* close read pipe */
        close(keepalive_pipe[0]);

        char *dir = realpath(context.appimage_path, NULL);

        char options[100];
        sprintf(options, "ro,offset=%lu", context.fs_offset);

        child_argv[0] = dir;
        child_argv[1] = "-o";
        child_argv[2] = options;
        child_argv[3] = dir;
        child_argv[4] = mount_dir;

        if (0 != fusefs_main(5, child_argv, fuse_mounted)) {
            char *title;
            char *body;
            printf("Cannot mount AppImage, please check your FUSE setup.\n");
            printf("You might still be able to extract the contents of this AppImage \n"
                   "if you run it with the --appimage-extract option. \n"
                   "See https://github.com/AppImage/AppImageKit/wiki/FUSE \n"
                   "for more information\n");
        };
    } else {
        /* in parent, child is $pid */
        int c;

        /* close write pipe */
        close(keepalive_pipe[1]);

        /* Pause until mounted */
        read(keepalive_pipe[0], &c, 1);

        /* Fuse process has now daemonized, reap our child */
        waitpid(pid, NULL, 0);

        dir_fd = open(mount_dir, O_RDONLY);
        if (dir_fd == -1) {
            perror("open dir error");
            exit(EXIT_EXECERROR);
        }

        res = dup2(dir_fd, 1023);
        if (res == -1) {
            perror("dup2 error");
            exit(EXIT_EXECERROR);
        }
        close(dir_fd);

        real_argv = malloc(sizeof(char *) * (argc + 1));
        for (i = 0; i < argc; i++) {
            real_argv[i] = argv[i];
        }
        real_argv[i] = NULL;

        if (arg && strcmp(arg, "appimage-mount") == 0) {
            char real_mount_dir[PATH_MAX];

            if (realpath(mount_dir, real_mount_dir) == real_mount_dir) {
                printf("%s\n", real_mount_dir);
            } else {
                printf("%s\n", mount_dir);
            }

            // stdout is, by default, buffered (unlike stderr), therefore in order to allow other processes to read
            // the path from stdout, we need to flush the buffers now
            // this is a less-invasive alternative to setbuf(stdout, NULL);
            fflush(stdout);

            for (;;) pause();

            exit(0);
        }

        appimage_execute_apprun(&context, mount_dir, argc, argv, "--appimage", true);
    }

    return 0;
}
