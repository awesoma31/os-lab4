#include "vtfs.h"

const struct inode_operations vtfs_inode_ops = {
    .lookup = vtfs_lookup,
    .create = vtfs_create,
    .unlink = vtfs_unlink,
    .mkdir = vtfs_mkdir,
    .rmdir = vtfs_rmdir,
    .link = vtfs_link,
};

const struct file_operations vtfs_dir_ops = {
    .owner = THIS_MODULE,
    .iterate_shared = vtfs_iterate,
};

const struct file_operations vtfs_file_ops = {
    .owner = THIS_MODULE,
    .open = vtfs_open,
    .read = vtfs_read,
    .write = vtfs_write,
};
