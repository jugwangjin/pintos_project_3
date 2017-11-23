#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
/* include synch.h for semaphore 
   we have to check if semaphore works or not.
   for now, it seems like that it works.*/
#include "threads/synch.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"

static void syscall_handler (struct intr_frame *);
int file_to_new_fd (struct file *file);
struct fd_element* fd_to_fd_element (int fd);
int addr_to_new_mapid (void *addr, int fd);
struct mapid_element *mapid_to_mapid_element (int mapid);
struct semaphore sys_sema;
void
syscall_init (void) 
{
  sema_init (&sys_sema, 1);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1, 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}

/* Returns true if the given string is valid in two ways:
   1. no page fault
   2. the string's content does not exceed the PHYS_BASE (user address) */
static bool
is_valid_string (const char *uaddr)
{
  char ch;
  int i;
  if (!is_user_vaddr(uaddr))
    return false;
  for(i = 0; (ch = get_user((char *)(uaddr + i))) != -1 && ch != 0; i++)
  {
    if(!is_user_vaddr(uaddr + i + 1))
      return false;
  }
  return ch == 0;
}


static void
syscall_handler (struct intr_frame *f) 
{
  int syscall_number;
  void *argument_1;
  void *argument_2;
  void *argument_3;
  struct fd_element *fd_elem;
  struct file *file;
  struct thread *cur = thread_current ();

  /* Number of arguments that are used depends on syscall number.
     Max number of arguments is 3. */
  if(!is_user_vaddr ((f->esp))
     || get_user(f->esp) == -1)
  {
    thread_exit ();
  }
  syscall_number = *(int *)(f->esp);
  if(syscall_number >= SYS_CREATE && syscall_number <=SYS_CLOSE)
    sema_down (&sys_sema);
  if(syscall_number < 0 || syscall_number > 20)
    thread_exit ();
  /* treat all syscall region as a critical section 
     we have to sema_up if the handler returns in the switch case
     (maybe later we make all syscalls, we may need only one sema_up in the last*/
  //sema_down (&syscall_sema);
  //printf ("system call! syscall number is : %d\n", syscall_number);
cur->esp = f->esp;
  switch(syscall_number)
  {
    case SYS_HALT:
     // printf("SYS_HALT\n");
      shutdown_power_off ();
      return;
    case SYS_EXIT:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        thread_exit ();
      }
      ///seaf->eax = *(int *)argument_1;
      thread_current () -> exit_status = *(int *)argument_1;
     // sema_up (&syscall_sema);
      thread_exit ();
    case SYS_EXEC:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        thread_exit ();
      }
      if(!is_valid_string(*(char **)argument_1))
      {
        thread_exit ();
      }
      char *fn_copy;
      fn_copy = palloc_get_page (0);
      if(fn_copy == NULL)
      {
        f->eax = TID_ERROR;
        return;
      }
      strlcpy (fn_copy, *(char **)argument_1, PGSIZE);
      f->eax = process_execute (fn_copy);
      palloc_free_page(fn_copy);
     // sema_up (&syscall_sema);
      return;
    case SYS_WAIT:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        thread_exit ();
      }
      f->eax = process_wait (*(tid_t *) argument_1); 
      return;
    case SYS_CREATE:
      //printf("SYS_CREATE\n");
      argument_1 = (f->esp)+4;
      argument_2 = (f->esp)+8; 
      if(!is_user_vaddr ((f->esp)+8))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if(!is_valid_string(*(char **)argument_1))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      f->eax = filesys_create(*(char **)argument_1, *(off_t *)argument_2); 
      sema_up (&sys_sema);
      return;
    case SYS_REMOVE:
     // printf("SYS_REMOVE\n");
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if(!is_valid_string(*(char **)argument_1))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      f->eax = filesys_remove (*(char **)argument_1);
      sema_up (&sys_sema);
      return;
    case SYS_OPEN:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if(!is_valid_string(*(char **)argument_1))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      file = filesys_open (*(char **)argument_1);
      if(file != NULL)
        f->eax = file_to_new_fd (file);
      else
        f->eax = -1;
      sema_up (&sys_sema);
      return;
     // printf("SYS_OPEN\n");
    case SYS_FILESIZE:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem == NULL)
      {
        f->eax = -1;
        sema_up (&sys_sema);
        return;
      }
      file = fd_elem->file;
      f->eax = file_length (file);
      sema_up (&sys_sema);
      return;
      //printf("SYS_FILESIZE\n");
    case SYS_READ:
     // printf("SYS_READ\n");
      argument_1 = (f->esp)+4;
      argument_2 = (f->esp)+8;
      argument_3 = (f->esp)+12;
      if(!is_user_vaddr ((f->esp)+12))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if (*(int *)argument_1 == STDIN_FILENO)
      {
        input_getc();
        f->eax = 1;
        sema_up (&sys_sema);
        return;
      }
      fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem == NULL)
      {
        f->eax = -1;
        sema_up (&sys_sema);
        return;
      }
      file = fd_elem -> file;
      if(file == NULL)
      {
        f->eax = -1;
        sema_up (&sys_sema);
        return;
      }
      if(!is_user_vaddr(*(uint8_t **)argument_2 + *(off_t *)argument_3))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if(get_user((*(uint8_t **)argument_2)) == -1)
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      uaddr_set_pin_true (*(void **)argument_2, &cur->spage_table);
      f->eax = file_read (file, *(void **)argument_2, *(off_t *)argument_3);
      uaddr_set_pin_false (*(void **)argument_2, &cur->spage_table);
      sema_up (&sys_sema);
      return;
    case SYS_WRITE:
     // printf("SYS_WRITE\n");
      argument_1 = (f->esp)+4;
      argument_2 = (f->esp)+8;
      argument_3 = (f->esp)+12;
      if(!is_user_vaddr ((f->esp)+12)||
         !is_user_vaddr(*(uint8_t **)argument_2 + *(off_t *)argument_3) ||
         get_user((*(uint8_t **)argument_2)) == -1)
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      if(*(int *)argument_1 == STDOUT_FILENO)
      {
      /* Fd 1 writes to the console. 
         breaks down the buffer by 256 bytes.*/
        f->eax = 0;
        while(*(size_t *)argument_3 > 256)
        {
            putbuf (*(char **)argument_2, 256);
            *(char **)argument_2 += 256;
            *(size_t *)argument_3 -= 256;
        }
        putbuf (*(char **)argument_2, *(size_t *)argument_3);
      }
      else
      {
        fd_elem = fd_to_fd_element (*(int *)argument_1);
        if (fd_elem == NULL)
        {
          f->eax = -1;
          sema_up (&sys_sema);
          return;
        }
        file = fd_elem -> file;
        if (file == NULL)
        {
          f->eax = -1;
          sema_up (&sys_sema);
          return;
        }
        uaddr_set_pin_true (*(void **)argument_2, &cur->spage_table);
        f->eax = file_write (file, *(void **)argument_2, *(off_t *)argument_3); 
        uaddr_set_pin_false (*(void **)argument_2, &cur->spage_table);
      }
      sema_up (&sys_sema);
      return;
    case SYS_SEEK:
     // printf("SYS_SEEK\n");
      argument_1 = (f->esp)+4;
      argument_2 = (f->esp)+8;
      if(!is_user_vaddr ((f->esp)+8))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem == NULL)
      {
        sema_up (&sys_sema);
        return;
      }
      file = fd_elem -> file;
      if(file == NULL)
      {
        sema_up (&sys_sema);
        return;
      }
      file_seek (file, *(off_t *)argument_2);
      sema_up (&sys_sema);
      return;
    case SYS_TELL:
     // printf("SYS_TELL\n");
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      argument_1 = (f->esp)+4;
      fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem == NULL)
      {
        f->eax = -1;
        sema_up (&sys_sema);
        return;
      }
      file = fd_elem->file;
      if(file == NULL)
      {
        f->eax = -1;
        sema_up (&sys_sema);
        return;
      }
      f->eax = file_tell (file);
      sema_up (&sys_sema);
      return;
    case SYS_CLOSE:
     // printf("SYS_CLOSE\n");
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      }
      struct fd_element* fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem != NULL)
      {
        file_close (fd_elem->file);
        list_remove (&fd_elem->elem);
        palloc_free_page (fd_elem);
      }
      sema_up (&sys_sema);
      return;
    case SYS_MMAP:
      argument_1 = (f->esp)+4;
      argument_2 = (f->esp)+8;
      if(!is_user_vaddr ((f->esp)+4) || !is_user_vaddr ((f->esp)+8))
      {
        sema_up (&sys_sema);
        thread_exit ();
      } 
      if (*(int *)argument_1 == 0 || *(int *)argument_1 == 1)
      {
        f->eax = 0xffffffff;
        sema_up (&sys_sema);
        return;
      }

      fd_elem = fd_to_fd_element (*(int *)argument_1);
      if(fd_elem == NULL)
      {
        f->eax = 0xffffffff;
        sema_up (&sys_sema);
        return;
      }
      file = file_reopen (fd_elem->file);
      int new_fd = -1;
      if(file != NULL)
        new_fd = file_to_new_fd (file);
      if (new_fd == -1)
      {
        f->eax = 0xffffffff;
        sema_up (&sys_sema);
        return;
      }
      fd_elem = fd_to_fd_element (new_fd);
      if (!spage_mmap (fd_elem->file, *(void **)argument_2))
        f->eax = 0xffffffff;
      else
        f->eax = addr_to_new_mapid (*(void **)argument_2, new_fd); 
      sema_up (&sys_sema);
      return;

    case SYS_MUNMAP:
      argument_1 = (f->esp)+4;
      if(!is_user_vaddr ((f->esp)+4))
      {
        sema_up (&sys_sema);
        thread_exit ();
      } 

      struct mapid_element *mapid_elem = mapid_to_mapid_element (* (int *)argument_1);      
      spage_free_page (mapid_elem->addr, &thread_current ()->spage_table);

      sema_up (&sys_sema);
      return;
  }
  /* treat all syscall region as a critical section */
//  sema_up (&syscall_sema);
  thread_exit ();
}

int
file_to_new_fd (struct file* file)
{
  struct thread *cur = thread_current ();
  struct fd_element *fd_elem;
  fd_elem = palloc_get_page (0);
  if(fd_elem == NULL)
    return -1;
  fd_elem->file = file;
  fd_elem->fd = cur->next_fd;
  cur->next_fd += 1;
  list_push_back (&(cur->fd_table), &(fd_elem->elem));
  return fd_elem->fd;
}

struct fd_element*
fd_to_fd_element (int fd)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct fd_element *fd_elem;

  for(e = list_begin (&(cur->fd_table)); e != list_end (&(cur->fd_table)); e = list_next (e))
  {
    fd_elem = list_entry (e, struct fd_element, elem);
    if (fd_elem -> fd == fd)
    {
      return fd_elem;
    }
  }
  return NULL;
}


int
addr_to_new_mapid (void *addr, int fd)
{
  struct thread *cur = thread_current ();
  struct mapid_element *mapid_elem;
  mapid_elem = palloc_get_page (0);
  if(mapid_elem == NULL)
    return -1;
  mapid_elem->addr = addr;
  mapid_elem->mapid = cur->next_mapid;
  mapid_elem->fd = fd;
//  mapid_elem->pg_number = pg_number;
  cur->next_mapid += 1;
  list_push_back (&(cur->mapid_table), &(mapid_elem->elem));
  return mapid_elem->mapid;
}

struct mapid_element*
mapid_to_mapid_element (int mapid)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct mapid_element *mapid_elem;

  for(e = list_begin (&(cur->mapid_table)); e != list_end (&(cur->mapid_table)); e = list_next (e))
  {
    mapid_elem = list_entry (e, struct mapid_element, elem);
    if (mapid_elem -> mapid == mapid)
    {
      return mapid_elem;
    }
  }
  return NULL;
}

void
munmap_close (int mapid)
{
  struct mapid_element *mapid_elem = mapid_to_mapid_element (mapid);
      if (mapid_elem != NULL)
      {
        struct fd_element* fd_elem = fd_to_fd_element (mapid_elem->fd);
        list_remove (&mapid_elem->elem);
        palloc_free_page (&mapid_elem->elem); 
        if(fd_elem != NULL)
        {
          file_close (fd_elem->file);
          list_remove (&fd_elem->elem);
          palloc_free_page (fd_elem);
        } 
      }
}

