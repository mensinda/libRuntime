// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE
#define _XOPEN_SOURCE 500

#include "libruntime.h"
#include "private.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/wait.h>

static pid_t fuse_pid;
static int   keepalive_pipe[2];

static void *write_pipe_thread(void *arg) {
    (void)arg;
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
    pthread_create(&thread, NULL, write_pipe_thread, NULL);
}

bool appimage_self_mount(appimage_context_t *const context,
                         const char *              mount_path,
                         appimage_cb_mounted       mounted_cb,
                         void *                    cb_user_data) {
    int    dir_fd, res;
    pid_t  pid;

    if (pipe(keepalive_pipe) == -1) {
        perror("pipe error");
        return false;
    }

    pid = fork();
    if (pid == -1) {
        perror("fork error");
        return false;
    }

    if (pid == 0) {
        /* in child */

        char *child_argv[5];

        /* close read pipe */
        close(keepalive_pipe[0]);

        char *dir = realpath(context->appimage_path, NULL);

        char options[100];
        sprintf(options, "ro,offset=%lu", context->fs_offset);

        child_argv[0] = dir;
        child_argv[1] = "-o";
        child_argv[2] = options;
        child_argv[3] = dir;
        child_argv[4] = (char *)mount_path;  // Remove const

        if (0 != fusefs_main(5, child_argv, fuse_mounted)) {
            printf("Cannot mount AppImage, please check your FUSE setup.\n");
            printf("You might still be able to extract the contents of this AppImage \n"
                   "if you run it with the --appimage-extract option. \n"
                   "See https://github.com/AppImage/AppImageKit/wiki/FUSE \n"
                   "for more information\n");
        };

        exit(EXIT_SUCCESS);
    } else {
        /* in parent, child is $pid */
        int c;

        /* close write pipe */
        close(keepalive_pipe[1]);

        /* Pause until mounted */
        read(keepalive_pipe[0], &c, 1);

        /* Fuse process has now daemonized, reap our child */
        waitpid(pid, NULL, 0);

        dir_fd = open(mount_path, O_RDONLY);
        if (dir_fd == -1) {
            perror("open dir error");
            return false;
        }

        res = dup2(dir_fd, 1023);
        if (res == -1) {
            perror("dup2 error");
            return false;
        }
        close(dir_fd);

        mounted_cb(context, cb_user_data);
    }

    return true;
}
