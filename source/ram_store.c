#include <linux/slab.h>
#include <linux/string.h>

#include "vtfs.h"
#include "vtfs_ram_store.h"

struct vtfs_file* vtfs_find_file_by_ino(struct vtfs_dir* dir, ino_t ino) {
  struct vtfs_file* file;
  
  if (!dir) return NULL;
  
  down_read(&dir->sem);
  list_for_each_entry(file, &dir->files, list) {
    if (file->ino == ino) {
      up_read(&dir->sem);
      return file;
    }
    if (file->dir_data) {
      struct vtfs_file* found = vtfs_find_file_by_ino(file->dir_data, ino);
      if (found) {
        up_read(&dir->sem);
        return found;
      }
    }
  }
  up_read(&dir->sem);
  return NULL;
}

struct vtfs_file* vtfs_find_file(struct vtfs_dir* dir, const char* name) {
  struct vtfs_file* file;
  if (!dir) return NULL;
  
  list_for_each_entry(file, &dir->files, list) {
    if (!strcmp(file->name, name)) {
      return file;
    }
  }
  return NULL;
}

struct vtfs_file* vtfs_create_file(
    struct vtfs_dir* dir,
    const char* name,
    umode_t mode,
    ino_t ino
) {
    struct vtfs_file* file;

    if (!dir || !name || strlen(name) >= VTFS_MAX_NAME)
        return NULL;

    if (vtfs_find_file(dir, name))
        return NULL;

    file = kzalloc(sizeof(*file), GFP_KERNEL);
    if (!file)
        return NULL;

    INIT_LIST_HEAD(&file->list);
    file->ino  = ino;
    file->mode = mode;
    file->nlink = 1;

    strscpy(file->name, name, VTFS_MAX_NAME);

    if (S_ISDIR(mode)) {
        file->dir_data = kzalloc(sizeof(struct vtfs_dir), GFP_KERNEL);
        if (!file->dir_data) {
            kfree(file);
            return NULL;
        }
        INIT_LIST_HEAD(&file->dir_data->files);
        init_rwsem(&file->dir_data->sem);
    }

    list_add_tail(&file->list, &dir->files);
    return file;
}


int vtfs_remove_file(struct vtfs_dir* dir, const char* name) {
    struct vtfs_file* file;

    if (!dir || !name)
        return -ENOENT;

    down_write(&dir->sem);
    file = vtfs_find_file(dir, name);
    if (!file) {
        up_write(&dir->sem);
        return -ENOENT;
    }

    list_del(&file->list);
    up_write(&dir->sem);

    if (file->dir_data) {
        vtfs_cleanup_dir(file->dir_data);
        kfree(file->dir_data);
    }

    if (file->data) kfree(file->data);
    kfree(file);

    return 0;
}

/* ram_store.c */

void vtfs_cleanup_dir(struct vtfs_dir *dir)
{
    struct vtfs_file *file, *tmp;

    /* список уникальных data-указателей, чтобы не было double-free на hard links */
    struct list_head data_list;
    struct data_ptr_entry *data_entry, *data_tmp;
    bool found;

    if (!dir)
        return;

    INIT_LIST_HEAD(&data_list);

    /*
     * 1) Собираем уникальные file->data (read-lock).
     *    Здесь мы НЕ трогаем сам список dir->files, только читаем.
     */
    down_read(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (!file->data)
            continue;

        found = false;
        list_for_each_entry(data_entry, &data_list, list) {
            if (data_entry->data == file->data) {
                found = true;
                break;
            }
        }

        if (!found) {
            data_entry = kmalloc(sizeof(*data_entry), GFP_KERNEL);
            if (data_entry) {
                data_entry->data = file->data;
                INIT_LIST_HEAD(&data_entry->list);
                list_add_tail(&data_entry->list, &data_list);
            }
            /* если kmalloc не удалось — лучше утечка, чем double-free */
        }
    }
    up_read(&dir->sem);

    /*
     * 2) Удаляем все vtfs_file (и рекурсивно поддиректории), но НЕ kfree(file->data) здесь.
     */
    down_write(&dir->sem);
    list_for_each_entry_safe(file, tmp, &dir->files, list) {
        list_del(&file->list);

        if (file->dir_data) {
            vtfs_cleanup_dir(file->dir_data);
            kfree(file->dir_data);
            file->dir_data = NULL;
        }

        /* данные освобождаем отдельно */
        file->data = NULL;
        file->data_size = 0;

        kfree(file);
    }
    up_write(&dir->sem);

    /*
     * 3) Освобождаем каждый уникальный data-буфер ровно один раз
     */
    list_for_each_entry_safe(data_entry, data_tmp, &data_list, list) {
        void *data_ptr = data_entry->data;
        list_del(&data_entry->list);
        kfree(data_entry);

        if (data_ptr)
            kfree(data_ptr);
    }
}


struct vtfs_dir* vtfs_get_dir(struct super_block* sb, struct inode* inode) {
    struct vtfs_fs_info* info;
    struct vtfs_file* file;

    if (!sb || !inode)
        return NULL;

    info = sb->s_fs_info;
    if (!info)
        return NULL;

    if (inode->i_ino == VTFS_ROOT_INO)
        return &info->root_dir;

    file = vtfs_find_file_by_ino(&info->root_dir, inode->i_ino);
    if (file && S_ISDIR(file->mode))
        return file->dir_data;

    return NULL;
}

struct vtfs_file* vtfs_get_file_by_inode(struct inode* inode) {
    struct vtfs_fs_info* info;

    if (!inode || !inode->i_sb)
        return NULL;

    info = inode->i_sb->s_fs_info;
    if (!info)
        return NULL;

    return vtfs_find_file_by_ino(&info->root_dir, inode->i_ino);
}

void vtfs_update_nlink_all(struct vtfs_dir* dir, ino_t ino, unsigned int nlink)
{
    struct vtfs_file* file;

    if (!dir)
        return;

    /* обновляем текущий каталог */
    down_write(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (file->ino == ino)
            file->nlink = nlink;
    }
    up_write(&dir->sem);

    /* рекурсивно спускаемся в подкаталоги */
    down_read(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (file->dir_data)
            vtfs_update_nlink_all(file->dir_data, ino, nlink);
    }
    up_read(&dir->sem);
}


void vtfs_update_data_all(
    struct vtfs_dir* dir,
    ino_t ino,
    char* old_data,
    char* new_data,
    size_t new_size
)
{
    struct vtfs_file* file;

    if (!dir)
        return;

    /* обновляем текущий каталог */
    down_write(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (file->ino == ino) {
            /* размер должен быть одинаков у всех hard links */
            file->data_size = new_size;

            /* указатель меняем только у тех, кто ещё держит old_data */
            if (file->data == old_data)
                file->data = new_data;
        }
    }
    up_write(&dir->sem);

    /* рекурсивно спускаемся в подкаталоги */
    down_read(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (file->dir_data)
            vtfs_update_data_all(file->dir_data, ino, old_data, new_data, new_size);
    }
    up_read(&dir->sem);
}


void vtfs_remove_all_by_ino(struct vtfs_dir* dir, ino_t ino) {
    struct vtfs_file* file;
    struct vtfs_file* tmp;

    if (!dir)
        return;

    down_write(&dir->sem);
    list_for_each_entry_safe(file, tmp, &dir->files, list) {
        if (file->ino == ino) {
            list_del(&file->list);
            kfree(file);
        }
    }
    up_write(&dir->sem);

    down_read(&dir->sem);
    list_for_each_entry(file, &dir->files, list) {
        if (file->dir_data) {
            vtfs_remove_all_by_ino(file->dir_data, ino);
        }
    }
    up_read(&dir->sem);
}
