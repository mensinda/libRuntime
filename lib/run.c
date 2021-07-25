// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#include "libruntime.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#define APPRUN_NAME "AppRun"

void appimage_execute_apprun(appimage_context_t *const context,
                             const char *              prefix,
                             int                       argc,
                             char *                    argv[],
                             const char *const         argument_skip_prefix,
                             bool                      enable_portable_support) {
    char *apprun_path = malloc(strlen(prefix) + 1 + strlen(APPRUN_NAME) + 1);
    strcpy(apprun_path, prefix);
    strcat(apprun_path, "/");
    strcat(apprun_path, APPRUN_NAME);

    // create copy of argument list without the --appimage-extract-and-run parameter
    char **new_argv      = malloc(sizeof(char *) * (argc + 1));
    int    new_argc      = 0;
    new_argv[new_argc++] = strdup(apprun_path);
    for (int i = 1; i < argc; ++i) {
        if (argument_skip_prefix == NULL || !appimage_starts_with(argument_skip_prefix, argv[i])) {
            new_argv[new_argc++] = strdup(argv[i]);
        }
    }
    new_argv[new_argc] = NULL;

    /* Setting some environment variables that the app "inside" might use */
    setenv("APPIMAGE", context->appimage_path, 1);
    setenv("ARGV0", context->argv0_path, 1);
    setenv("APPDIR", prefix, 1);

    // +16 so that is more than enough space to fit both .home and .config
    char *portable_home_dir   = malloc(strlen(context->appimage_path) + 16);
    char *portable_config_dir = malloc(strlen(context->appimage_path) + 16);

    if (enable_portable_support) {
        /* If there is a directory with the same name as the AppImage plus ".home", then export $HOME */
        strcpy(portable_home_dir, context->appimage_path);
        strcat(portable_home_dir, ".home");
        if (appimage_is_writable_directory(portable_home_dir)) {
            fprintf(stderr, "Setting $HOME to %s\n", portable_home_dir);
            setenv("HOME", portable_home_dir, 1);
        }

        /* If there is a directory with the same name as the AppImage plus ".config", then export $XDG_CONFIG_HOME */
        strcpy(portable_config_dir, context->appimage_path);
        strcat(portable_config_dir, ".config");
        if (appimage_is_writable_directory(portable_config_dir)) {
            fprintf(stderr, "Setting $XDG_CONFIG_HOME to %s\n", portable_config_dir);
            setenv("XDG_CONFIG_HOME", portable_config_dir, 1);
        }
    }

    /* Original working directory */
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        setenv("OWD", cwd, 1);
    }

    execv(apprun_path, new_argv);

    int error = errno;
    fprintf(stderr, "Failed to run %s: %s\n", apprun_path, strerror(error));

    free(apprun_path);
    free(portable_home_dir);
    free(portable_config_dir);
    exit(EXIT_EXECERROR);
}
