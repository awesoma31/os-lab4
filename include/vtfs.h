#ifndef _VTFS_H_
#define _VTFS_H_

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/list.h>
#include <linux/rwsem.h>
#include <linux/stat.h>

#define VTFS_ROOT_INO 100
#define VTFS_MAX_NAME 256

struct inode;
struct dentry;
struct file;
struct super_block;
struct dir_context;
struct mnt_idmap;
struct kstat;
struct iattr;

struct vtfs_dir;

struct vtfs_file {
    struct list_head list;
    ino_t            ino;
    umode_t          mode;
    char             name[VTFS_MAX_NAME];

    struct vtfs_dir *dir_data;   
    char            *data;       
    size_t           data_size;

    unsigned int     nlink;
};

struct vtfs_dir {
    struct list_head files;      
    struct rw_semaphore sem;
};

struct vtfs_fs_info {
    struct vtfs_dir   root_dir;
    ino_t             next_ino;
    struct super_block *sb;
};

extern const struct inode_operations vtfs_inode_ops;
extern const struct file_operations  vtfs_dir_ops;
extern const struct file_operations  vtfs_file_ops;


struct inode *vtfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, ino_t ino);

struct vtfs_dir  *vtfs_get_dir(struct super_block *sb, struct inode *inode);
struct vtfs_file *vtfs_find_file(struct vtfs_dir *dir, const char *name);
struct vtfs_file *vtfs_find_file_by_ino(struct vtfs_dir *dir, ino_t ino);
struct vtfs_file *vtfs_create_file(struct vtfs_dir *dir, const char *name, umode_t mode, ino_t ino);

int  vtfs_remove_file(struct vtfs_dir *dir, const char *name);
void vtfs_cleanup_dir(struct vtfs_dir *dir);

struct vtfs_file *vtfs_get_file_by_inode(struct inode *inode);

void vtfs_update_nlink_all(struct vtfs_dir *dir, ino_t ino, unsigned int nlink);
void vtfs_update_data_all(struct vtfs_dir *dir, ino_t ino, char *old_data, char *new_data, size_t new_size);
void vtfs_remove_all_by_ino(struct vtfs_dir *dir, ino_t ino);


struct dentry *vtfs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);

int vtfs_iterate(struct file *filp, struct dir_context *ctx);
int vtfs_create(struct mnt_idmap *idmap, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode, bool excl);
int vtfs_unlink(struct inode *parent_inode, struct dentry *child_dentry);
int vtfs_mkdir(struct mnt_idmap *idmap, struct inode *parent_inode, struct dentry *child_dentry, umode_t mode);
int vtfs_rmdir(struct inode *parent_inode, struct dentry *child_dentry);
int vtfs_link(struct dentry *old_dentry, struct inode *parent_dir, struct dentry *new_dentry);
ssize_t vtfs_read(struct file *filp, char __user *buffer, size_t len, loff_t *offset);
ssize_t vtfs_write(struct file *filp, const char __user *buffer, size_t len, loff_t *offset);

int vtfs_open(struct inode *inode, struct file *filp);

#endif /* _VTFS_H_ */
