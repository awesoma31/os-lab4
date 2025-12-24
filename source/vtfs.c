#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/mount.h>

#include "vtfs.h"

static int vtfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct vtfs_fs_info *info;
    struct inode *inode;

    (void)data;
    (void)silent;

    info = kzalloc(sizeof(*info), GFP_KERNEL);
    if (!info)
        return -ENOMEM;

    INIT_LIST_HEAD(&info->root_dir.files);
    init_rwsem(&info->root_dir.sem);
    info->next_ino = 200;
    info->sb = sb;

    sb->s_fs_info   = info;
    sb->s_magic     = 0x56544653; /* VTFS */
    sb->s_time_gran = 1;

    inode = vtfs_get_inode(
        sb,
        NULL,
        S_IFDIR | 0755,
        VTFS_ROOT_INO
    );
    if (!inode)
        goto err;

    sb->s_root = d_make_root(inode);
    if (!sb->s_root)
        goto err;

    return 0;

err:
    vtfs_cleanup_dir(&info->root_dir);
    kfree(info);
    sb->s_fs_info = NULL;
    return -ENOMEM;
}

/*  mount / unmount */

static struct dentry *vtfs_mount(
    struct file_system_type *fs_type,
    int flags,
    const char *dev_name,
    void *data
)
{
    return mount_nodev(fs_type, flags, NULL, vtfs_fill_super);
}

static void vtfs_kill_sb(struct super_block *sb)
{
    struct vtfs_fs_info *info;

    if (!sb)
        return;

    info = sb->s_fs_info;
    if (info) {
        vtfs_cleanup_dir(&info->root_dir);
        kfree(info);
        sb->s_fs_info = NULL;
    }

    kill_litter_super(sb);
}

static struct file_system_type vtfs_fs_type = {
    .owner   = THIS_MODULE,
    .name    = "vtfs",
    .mount   = vtfs_mount,
    .kill_sb = vtfs_kill_sb,
};

static int __init vtfs_init(void)
{
    pr_info("[vtfs] VTFS loaded\n");
    return register_filesystem(&vtfs_fs_type);
}

static void __exit vtfs_exit(void)
{
    unregister_filesystem(&vtfs_fs_type);
    pr_info("[vtfs] VTFS unloaded\n");
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("secs-awesoma");
MODULE_DESCRIPTION("VTFS RAM-only filesystem");

module_init(vtfs_init);
module_exit(vtfs_exit);
