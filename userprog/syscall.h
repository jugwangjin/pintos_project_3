#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <list.h>
void syscall_init (void);

struct fd_element
  {
    int fd;
    struct file* file;
    struct list_elem elem;
  };

struct mapid_element
  {
    int mapid;
    int fd;
//    int pg_number;
    void *addr;
    struct list_elem elem;
  };

void munmap_close (int mapid);
#endif /* userprog/syscall.h */
