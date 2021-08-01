// Copyright 2004-18 Simon Peter
// Copyright 2007    Alexander Larsson
// Copyright 2021    Daniel Mensinger
// SPDX-License-Identifier: MIT

#include "libruntime.h"

#define _GNU_SOURCE

#include <squashfuse.h>
#include <squashfs_fs.h>
#include <nonstd.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fnmatch.h>
#include <sys/stat.h>

/* Fill in a stat structure. Does not set st_ino */
sqfs_err private_sqfs_stat(sqfs *fs, sqfs_inode *inode, struct stat *st) {
    sqfs_err err = SQFS_OK;
    uid_t    id;

    memset(st, 0, sizeof(*st));
    st->st_mode  = inode->base.mode;
    st->st_nlink = inode->nlink;
    st->st_mtime = st->st_ctime = st->st_atime = inode->base.mtime;

    if (S_ISREG(st->st_mode)) {
        /* FIXME: do symlinks, dirs, etc have a size? */
        st->st_size   = inode->xtra.reg.file_size;
        st->st_blocks = st->st_size / 512;
    } else if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
        st->st_rdev = sqfs_makedev(inode->xtra.dev.major, inode->xtra.dev.minor);
    } else if (S_ISLNK(st->st_mode)) {
        st->st_size = inode->xtra.symlink_size;
    }

    st->st_blksize = fs->sb.block_size; /* seriously? */

    err = sqfs_id_get(fs, inode->base.uid, &id);
    if (err) return err;
    st->st_uid = id;
    err        = sqfs_id_get(fs, inode->base.guid, &id);
    st->st_gid = id;
    if (err) return err;

    return SQFS_OK;
}

bool appimage_self_extract(appimage_context_t *const context,
                           const char *const         _prefix,
                           const char *const         _pattern,
                           const bool                overwrite,
                           const bool                verbose) {
    sqfs_err      err = SQFS_OK;
    sqfs_traverse trv;
    sqfs          fs;
    char          prefixed_path_to_extract[1024];

    // local copy we can modify safely
    // allocate 1 more byte than we would need so we can add a trailing slash if there is none yet
    char *prefix = malloc(strlen(_prefix) + 2);
    strcpy(prefix, _prefix);

    // sanitize prefix
    if (prefix[strlen(prefix) - 1] != '/') strcat(prefix, "/");

    if (access(prefix, F_OK) == -1) {
        if (appimage_mkdir_p(prefix) == false) {
            perror("appimage_mkdir_p error");
            return false;
        }
    }

    if ((err = sqfs_open_image(&fs, context->appimage_path, context->fs_offset))) {
        fprintf(stderr, "Failed to open squashfs image\n");
        return false;
    };

    // track duplicate inodes for hardlinks
    char **created_inode = calloc(fs.sb.inodes, sizeof(char *));
    if (created_inode == NULL) {
        fprintf(stderr, "Failed allocating memory to track hardlinks\n");
        return false;
    }

    if ((err = sqfs_traverse_open(&trv, &fs, sqfs_inode_root(&fs)))) {
        fprintf(stderr, "sqfs_traverse_open error\n");
        free(created_inode);
        return false;
    }

    bool rv = true;

    while (sqfs_traverse_next(&trv, &err)) {
        if (!trv.dir_end) {
            if (_pattern == NULL || fnmatch(_pattern, trv.path, FNM_FILE_NAME | FNM_LEADING_DIR) == 0) {
                // fprintf(stderr, "trv.path: %s\n", trv.path);
                // fprintf(stderr, "sqfs_inode_id: %lu\n", trv.entry.inode);
                sqfs_inode inode;
                if (sqfs_inode_get(&fs, &inode, trv.entry.inode)) {
                    fprintf(stderr, "sqfs_inode_get error\n");
                    rv = false;
                    break;
                }
                // fprintf(stderr, "inode.base.inode_type: %i\n", inode.base.inode_type);
                // fprintf(stderr, "inode.xtra.reg.file_size: %lu\n", inode.xtra.reg.file_size);
                strcpy(prefixed_path_to_extract, "");
                strcat(strcat(prefixed_path_to_extract, prefix), trv.path);

                if (verbose) fprintf(stdout, "%s\n", prefixed_path_to_extract);

                if (inode.base.inode_type == SQUASHFS_DIR_TYPE || inode.base.inode_type == SQUASHFS_LDIR_TYPE) {
                    // fprintf(stderr, "inode.xtra.dir.parent_inode: %ui\n", inode.xtra.dir.parent_inode);
                    // fprintf(stderr, "appimage_mkdir_p: %s/\n", prefixed_path_to_extract);
                    if (access(prefixed_path_to_extract, F_OK) == -1) {
                        if (appimage_mkdir_p(prefixed_path_to_extract) == false) {
                            perror("appimage_mkdir_p error");
                            rv = false;
                            break;
                        }
                    }
                } else if (inode.base.inode_type == SQUASHFS_REG_TYPE || inode.base.inode_type == SQUASHFS_LREG_TYPE) {
                    // if we've already created this inode, then this is a hardlink
                    char *existing_path_for_inode = created_inode[inode.base.inode_number - 1];
                    if (existing_path_for_inode != NULL) {
                        unlink(prefixed_path_to_extract);
                        if (link(existing_path_for_inode, prefixed_path_to_extract) == -1) {
                            fprintf(stderr,
                                    "Couldn't create hardlink from \"%s\" to \"%s\": %s\n",
                                    prefixed_path_to_extract,
                                    existing_path_for_inode,
                                    strerror(errno));
                            rv = false;
                            break;
                        } else {
                            continue;
                        }
                    } else {
                        struct stat st;
                        if (!overwrite && stat(prefixed_path_to_extract, &st) == 0 &&
                            (uint64_t)st.st_size == inode.xtra.reg.file_size) {
                            fprintf(stderr, "File exists and file size matches, skipping\n");
                            continue;
                        }

                        // track the path we extract to for this inode, so that we can `link` if this inode is found
                        // again
                        created_inode[inode.base.inode_number - 1] = strdup(prefixed_path_to_extract);
                        // fprintf(stderr, "Extract to: %s\n", prefixed_path_to_extract);
                        if (private_sqfs_stat(&fs, &inode, &st) != 0) {
                            fprintf(stderr, "private_sqfs_stat error\n");
                            rv = false;
                            break;
                        }

                        // create parent dir
                        char *p = strrchr(prefixed_path_to_extract, '/');
                        if (p) {
                            // set an \0 to end the split the string
                            *p = '\0';
                            appimage_mkdir_p(prefixed_path_to_extract);

                            // restore dir seprator
                            *p = '/';
                        }

                        // Read the file in chunks
                        sqfs_off_t bytes_already_read = 0;
                        sqfs_off_t bytes_at_a_time    = 64 * 1024;
                        FILE *     f;
                        f = fopen(prefixed_path_to_extract, "w+");
                        if (f == NULL) {
                            perror("fopen error");
                            rv = false;
                            break;
                        }
                        while ((uint64_t)bytes_already_read < inode.xtra.reg.file_size) {
                            char buf[bytes_at_a_time];
                            if (sqfs_read_range(&fs, &inode, bytes_already_read, &bytes_at_a_time, buf)) {
                                perror("sqfs_read_range error");
                                rv = false;
                                break;
                            }
                            // fwrite(buf, 1, bytes_at_a_time, stdout);
                            fwrite(buf, 1, bytes_at_a_time, f);
                            bytes_already_read = bytes_already_read + bytes_at_a_time;
                        }
                        fclose(f);
                        chmod(prefixed_path_to_extract, st.st_mode);
                        if (!rv) break;
                    }
                } else if (inode.base.inode_type == SQUASHFS_SYMLINK_TYPE ||
                           inode.base.inode_type == SQUASHFS_LSYMLINK_TYPE) {
                    size_t size;
                    sqfs_readlink(&fs, &inode, NULL, &size);
                    char buf[size];
                    int  ret = sqfs_readlink(&fs, &inode, buf, &size);
                    if (ret != 0) {
                        perror("symlink error");
                        rv = false;
                        break;
                    }
                    // fprintf(stderr, "Symlink: %s to %s \n", prefixed_path_to_extract, buf);
                    unlink(prefixed_path_to_extract);
                    ret = symlink(buf, prefixed_path_to_extract);
                    if (ret != 0) fprintf(stderr, "WARNING: could not create symlink\n");
                } else {
                    fprintf(stderr, "TODO: Implement inode.base.inode_type %i\n", inode.base.inode_type);
                }
                // fprintf(stderr, "\n");

                if (!rv) break;
            }
        }
    }
    for (uint32_t i = 0; i < fs.sb.inodes; i++) {
        free(created_inode[i]);
    }
    free(created_inode);

    if (err != SQFS_OK) {
        fprintf(stderr, "sqfs_traverse_next error\n");
        rv = false;
    }
    sqfs_traverse_close(&trv);
    sqfs_fd_close(fs.fd);

    return rv;
}
