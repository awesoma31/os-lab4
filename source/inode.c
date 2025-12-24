#include "vtfs.h"
#include <linux/slab.h>
#include <linux/namei.h>

struct inode *vtfs_get_inode(struct super_block *sb,
                             const struct inode *dir,
                             umode_t mode,
                             ino_t ino)
{
    struct inode *inode = new_inode(sb);
    if (!inode)
        return NULL;

    inode->i_ino  = ino;
    inode->i_mode = mode;
    inode->i_uid  = dir ? dir->i_uid : GLOBAL_ROOT_UID;
    inode->i_gid  = dir ? dir->i_gid : GLOBAL_ROOT_GID;

    inode->i_op = &vtfs_inode_ops;

    if (S_ISDIR(mode)) {
        inode->i_fop = &vtfs_dir_ops;
        set_nlink(inode, 2);
    } else {
        inode->i_fop = &vtfs_file_ops;
        set_nlink(inode, 1);
        inode->i_size = 0;
    }

    return inode;
}

/* lookup */

struct dentry *vtfs_lookup(struct inode *parent,
                           struct dentry *dentry,
                           unsigned int flags)
{
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file;
    struct inode *inode;

    if (!dir)
        return NULL;

    down_read(&dir->sem);
    file = vtfs_find_file(dir, dentry->d_name.name);
    if (!file) {
        up_read(&dir->sem);
        return NULL;
    }

    inode = vtfs_get_inode(parent->i_sb, parent,
                            file->mode, file->ino);
    if (S_ISREG(file->mode))
        inode->i_size = file->data_size;

    set_nlink(inode, file->nlink);

    up_read(&dir->sem);
    d_add(dentry, inode);
    return NULL;
}

/* create */

int vtfs_create(struct mnt_idmap *idmap,
                struct inode *parent,
                struct dentry *dentry,
                umode_t mode,
                bool excl)
{
    struct vtfs_fs_info *info = parent->i_sb->s_fs_info;
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file;
    struct inode *inode;

    if (!info || !dir)
        return -ENOENT;

    down_write(&dir->sem);

    if (vtfs_find_file(dir, dentry->d_name.name)) {
        up_write(&dir->sem);
        return -EEXIST;
    }

    file = vtfs_create_file(dir, dentry->d_name.name,
                            S_IFREG | mode, info->next_ino++);
    up_write(&dir->sem);

    if (!file)
        return -ENOMEM;

    inode = vtfs_get_inode(parent->i_sb, parent,
                            file->mode, file->ino);
    d_add(dentry, inode);
    return 0;
}

/* unlink */

int vtfs_unlink(struct inode *parent, struct dentry *dentry)
{
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file;

    if (!dir)
        return -ENOENT;

    down_write(&dir->sem);
    file = vtfs_find_file(dir, dentry->d_name.name);
    if (!file) {
        up_write(&dir->sem);
        return -ENOENT;
    }

    list_del(&file->list);
    file->nlink--;
    up_write(&dir->sem);

    set_nlink(d_inode(dentry), file->nlink);

    if (file->nlink == 0) {
        if (file->data)
            kfree(file->data);
        if (file->dir_data)
            kfree(file->dir_data);
        kfree(file);
    }

    return 0;
}

/* mkdir / rmdir */

int vtfs_mkdir(struct mnt_idmap *idmap,
               struct inode *parent,
               struct dentry *dentry,
               umode_t mode)
{
    struct vtfs_fs_info *info = parent->i_sb->s_fs_info;
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file;
    struct inode *inode;

    if (!info || !dir)
        return -ENOENT;

    down_write(&dir->sem);
    file = vtfs_create_file(dir, dentry->d_name.name,
                            S_IFDIR | mode, info->next_ino++);
    up_write(&dir->sem);

    inode = vtfs_get_inode(parent->i_sb, parent,
                            file->mode, file->ino);
    d_add(dentry, inode);
    inc_nlink(parent);
    return 0;
}

int vtfs_rmdir(struct inode *parent, struct dentry *dentry)
{
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file;

    if (!dir)
        return -ENOENT;

    down_write(&dir->sem);
    file = vtfs_find_file(dir, dentry->d_name.name);

    if (!file || !list_empty(&file->dir_data->files)) {
        up_write(&dir->sem);
        return -ENOTEMPTY;
    }

    list_del(&file->list);
    up_write(&dir->sem);

    kfree(file->dir_data);
    kfree(file);
    drop_nlink(parent);
    return 0;
}

/* hard link */

int vtfs_link(struct dentry *old, struct inode *parent, struct dentry *new)
{
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *src, *link;

    if (!dir)
        return -ENOENT;

    src = vtfs_get_file_by_inode(d_inode(old));
    if (!src)
        return -ENOENT;

    down_write(&dir->sem);

    link = kzalloc(sizeof(*link), GFP_KERNEL);
    *link = *src;
    strscpy(link->name, new->d_name.name, VTFS_MAX_NAME);
    INIT_LIST_HEAD(&link->list);

    src->nlink++;
    link->nlink = src->nlink;

    list_add_tail(&link->list, &dir->files);
    up_write(&dir->sem);

    inc_nlink(d_inode(old));
    d_instantiate(new, d_inode(old));
    return 0;
}
