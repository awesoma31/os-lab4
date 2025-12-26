#include "vtfs.h"
#include <linux/printk.h>

int vtfs_open(struct inode* inode, struct file* filp)
{
    if (filp->f_flags & O_TRUNC) {
        struct vtfs_file* file = vtfs_get_file_by_inode(inode);
        if (file && file->data) {
            char* old = file->data;
            struct vtfs_fs_info* info = inode->i_sb->s_fs_info;

            if (info) {
                vtfs_update_data_all(&info->root_dir,
                                     inode->i_ino,
                                     old,
                                     NULL,
                                     0);
            }

            kfree(old);
            file->data = NULL;
            file->data_size = 0;
            inode->i_size = 0;
        }
    }
    return 0;
}

ssize_t vtfs_read(struct file* filp, char __user* buffer,
                  size_t len, loff_t* offset)
{
    struct inode* inode = filp->f_inode;
    struct vtfs_file* file = vtfs_get_file_by_inode(inode);
    size_t to_read;

    

    if (!file || !file->data)
        return 0;

    if (*offset >= file->data_size)
        return 0;

    to_read = min(len, file->data_size - *offset);

    if (copy_to_user(buffer, file->data + *offset, to_read))
        return -EFAULT;

    *offset += to_read;
    return to_read;
}

ssize_t vtfs_write(struct file* filp, const char __user* buffer,
                   size_t len, loff_t* offset)
{
    struct inode* inode = filp->f_inode;
    struct vtfs_file* file = vtfs_get_file_by_inode(inode);
    struct vtfs_fs_info* info = inode->i_sb->s_fs_info;
    char *new_data;
    char *old_data;
    size_t new_size;

    if (!file)
        return -ENOENT;

    if (filp->f_flags & O_APPEND)
        *offset = file->data_size;

    old_data = file->data;
    new_size = *offset + len;

    new_data = krealloc(old_data, new_size, GFP_KERNEL);
    if (!new_data)
        return -ENOMEM;

    if (file->data_size < *offset)
        memset(new_data + file->data_size, 0, *offset - file->data_size);

    /* ВАЖНО: если есть hard links — обновлять ВСЕ записи этого ino */
    if (info && file->nlink > 1) {
        vtfs_update_data_all(&info->root_dir,
                             inode->i_ino,
                             old_data,     /* может быть NULL */
                             new_data,
                             new_size);

        /* заново возьмём file, т.к. vtfs_get_file_by_inode может вернуть другой entry */
        file = vtfs_get_file_by_inode(inode);
        if (!file)
            return -ENOENT;
    } else {
        file->data = new_data;
        file->data_size = new_size;
    }

    if (copy_from_user(file->data + *offset, buffer, len))
        return -EFAULT;

    *offset += len;
    inode->i_size = file->data_size;
    return len;
}
