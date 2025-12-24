#include "vtfs.h"

int vtfs_iterate(struct file* filp, struct dir_context* ctx)
{
    struct inode* inode = filp->f_inode;
    struct vtfs_dir* dir;
    struct vtfs_file* file;
    unsigned long index = 0;

    dir = vtfs_get_dir(inode->i_sb, inode);
    if (!dir)
        return 0;

    if (ctx->pos == 0) {
        if (!dir_emit(ctx, ".", 1, inode->i_ino, DT_DIR))
            return 0;
        ctx->pos = 1;
    }

    if (ctx->pos == 1) {
        ino_t pino = inode->i_ino;
        if (!dir_emit(ctx, "..", 2, pino, DT_DIR))
            return 0;
        ctx->pos = 2;
    }

    down_read(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (index++ < ctx->pos - 2)
            continue;

        if (!dir_emit(ctx,
                      file->name,
                      strlen(file->name),
                      file->ino,
                      S_ISDIR(file->mode) ? DT_DIR : DT_REG))
            break;

        ctx->pos++;
    }
    up_read(&dir->sem);

    return 0;
}
