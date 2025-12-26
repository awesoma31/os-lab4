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
    struct vtfs_file *src, *new_file;
    struct inode *inode = d_inode(old);
    struct vtfs_fs_info *info = parent->i_sb->s_fs_info;
    const char *name = new->d_name.name;

    if (!old || !parent || !new) {
        return -EINVAL;
    }
    
    inode = old->d_inode;
    if (!dir || !inode)
        return -ENOENT;

    if (S_ISDIR(inode->i_mode)) {
        return -EPERM;
    }

    //
    src = vtfs_get_file_by_inode(inode);
    if (!src)
        return -ENOENT;

    down_write(&dir->sem);

    if (vtfs_find_file(dir, name)) {
        up_write(&dir->sem);
        return -EEXIST;
    }

    new_file = kzalloc(sizeof(*new_file), GFP_KERNEL);
    if (!new_file) {
        up_write(&dir->sem);
        return -ENOMEM;
    }

    INIT_LIST_HEAD(&new_file->list);
    new_file->ino = src->ino;
    new_file->mode = src->mode;
    strscpy(new_file->name, name, VTFS_MAX_NAME - 1);
    new_file->name[VTFS_MAX_NAME - 1] = '\0';
    new_file->dir_data = NULL;
    new_file->data = src->data;
    new_file->data_size = src->data_size;

    src->nlink++;
    new_file->nlink = src->nlink;

    list_add_tail(&new_file->list, &dir->files);
    up_write(&dir->sem);

    if (info)
        vtfs_update_nlink_all(&info->root_dir, src->ino, src->nlink);

    set_nlink(inode, src->nlink);

    ihold(inode);
    d_instantiate(new, inode);

    return 0;
}


/* unlink */
int vtfs_unlink(struct inode *parent, struct dentry *dentry)
{
    struct vtfs_dir *dir = vtfs_get_dir(parent->i_sb, parent);
    struct vtfs_file *file, *main_file;
    struct inode *inode = d_inode(dentry);
    struct vtfs_fs_info *info = parent->i_sb->s_fs_info;
    const char *name;
    ino_t ino;
    unsigned int new_nlink;

    char *data_to_free = NULL;
    struct vtfs_dir *dir_data_to_free = NULL;
    bool should_free_data = false;

    if (!dir || !inode)
        return -ENOENT;

    name = dentry->d_name.name;
    ino = inode->i_ino;

    main_file = vtfs_get_file_by_inode(inode);
    if (!main_file)
        return -ENOENT;

    new_nlink = (main_file->nlink > 0) ? (main_file->nlink - 1) : 0;
    should_free_data = (new_nlink == 0);

    down_write(&dir->sem);

    file = vtfs_find_file(dir, name);
    if (!file || file->ino != ino) {
        up_write(&dir->sem);
        return -ENOENT;
    }

    if (should_free_data) {
        data_to_free = file->data;
        dir_data_to_free = file->dir_data;
    }

    list_del(&file->list);
    up_write(&dir->sem);

    kfree(file);

    if (info)
        vtfs_update_nlink_all(&info->root_dir, ino, new_nlink);

    set_nlink(inode, new_nlink);

    if (should_free_data) {
        if (info)
            vtfs_remove_all_by_ino(&info->root_dir, ino);

        if (dir_data_to_free) {
            vtfs_cleanup_dir(dir_data_to_free);
            kfree(dir_data_to_free);
        }
        kfree(data_to_free);
    }

    return 0;
}
