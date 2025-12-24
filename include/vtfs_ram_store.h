#ifndef VTFS_RAM_STORE_H
#define VTFS_RAM_STORE_H

#include <linux/list.h>

struct data_ptr_entry {
    void *data;
    struct list_head list;
};

#endif
