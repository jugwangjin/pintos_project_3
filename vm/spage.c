#include <hash.h>
#include "vm/spage.h"
#include "threads/vaddr.h"
#include "userprog/exception.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "userprog/syscall.h"
#include "vm/swap.h"
#include "vm/frame.h"
#include "devices/intq.h"
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}

static struct mapid_element*
addr_to_mapid_element (void *addr)
{
  struct thread *cur = thread_current ();
  struct list_elem *e;
  struct mapid_element *mapid_elem;
  for(e = list_begin (&(cur->mapid_table)); e != list_end (&(cur->mapid_table)); e = list_next (e))
  {
    mapid_elem = list_entry (e, struct mapid_element, elem);
    if (mapid_elem -> addr == addr)
    {
      return mapid_elem;
    }
  }
  return NULL;
}

static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

void
init_spage (struct hash *spage_table)
{
  hash_init (spage_table, spage_hash, spage_less, NULL);
}

unsigned
spage_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct spage_table_entry *s = hash_entry (p_, struct spage_table_entry, hash_elem);
  return hash_bytes (&s->uaddr, sizeof s->uaddr);
}

bool
spage_less (const struct hash_elem *a_, const struct hash_elem *b_,
            void *aux UNUSED)
{
  const struct spage_table_entry *a = hash_entry (a_, struct spage_table_entry, hash_elem);
  const struct spage_table_entry *b = hash_entry (b_, struct spage_table_entry, hash_elem);
  return a->uaddr < b->uaddr;
}

struct spage_table_entry *
get_spage (struct hash *spage_table, void *uaddr)
{
  struct spage_table_entry s;
  struct hash_elem *e;
  
  s.uaddr = pg_round_down (uaddr);
  e = hash_find (spage_table, &s.hash_elem);
  return e != NULL ? hash_entry (e, struct spage_table_entry, hash_elem) : NULL;
}

bool
make_spage_for_stack_growth (struct hash *spage_table, void *fault_addr)
{
  struct spage_table_entry *ste = malloc (sizeof (struct spage_table_entry));
  if (!ste)
    return false;
  ste->uaddr = pg_round_down (fault_addr);
  ste->writable = true;
  ste->mmap = false;
  ste->file = false;
  ste->swap = false;
  ste->pin = false;
 
  hash_insert (spage_table, &ste->hash_elem);
  return spage_get_frame (ste);
}

bool
spage_free_page (void *uaddr, struct hash *spage_table)
{
  struct spage_table_entry search_ste;
  struct spage_table_entry *ste;
  struct hash_elem *e;
  struct thread *t = thread_current ();
  struct mapid_element *mapid_elem;
  search_ste.uaddr = uaddr;
  e = hash_delete (spage_table, &search_ste.hash_elem);
  if (e != NULL)
  {
    ste = (hash_entry (e, struct spage_table_entry, hash_elem));
    if(ste)
    {
      if (ste->swap)
        get_user (ste->uaddr);
      if (ste->mmap)
      {
        spage_write_back (ste, t);
        mapid_elem = addr_to_mapid_element (ste->uaddr);
        if (mapid_elem != NULL)
          munmap_close (mapid_elem->fd);
      }
    frame_free_page (pagedir_get_page (t->pagedir, ste->uaddr));
    pagedir_clear_page (t->pagedir, ste->uaddr);
    free (ste);
    return true;
    }
  }
  return false;
}

bool
load_file (struct spage_table_entry *ste, void *frame)
{
/*  if(!ste->file)
    return false;
  if(ste->mmap && ste->read_bytes == 0)
    return false;*/
  if(ste->file)
  {
    uaddr_set_pin_true (ste->uaddr, &thread_current ()->spage_table);
    if (file_read_at (ste->file_ptr, frame, ste->read_bytes, ste->ofs) != (int) ste->read_bytes)
    {
      uaddr_set_pin_false (ste->uaddr, &thread_current ()->spage_table);
      return false;
    }
    uaddr_set_pin_false (ste->uaddr, &thread_current ()->spage_table);
    memset (frame + ste->read_bytes, 0, ste->zero_bytes);
  }
/*  else if (ste->mmap && ste->zero_bytes!=0)
    memset (frame, 0, PGSIZE);*/
  return true;
}

bool
spage_get_frame (struct spage_table_entry *ste)
{
  void *allocated_frame;
  bool success=false;
  struct frame_table_entry fte;
  struct hash_elem *e;
  struct frame_table_entry *fte_en;
  lock_acquire (&frame_lock);
  allocated_frame = frame_get_page (PAL_USER, ste);
  if (!allocated_frame)
  {
    lock_release (&frame_lock);
    return false;
  }
  fte.kaddr = allocated_frame;
  e = hash_find (&frame_table, &fte.hash_elem);
  fte_en = hash_entry (e, struct frame_table_entry, hash_elem);
  fte_en->uaddr = ste->uaddr;
  fte_en->thread = thread_current ();
  success = install_page (ste->uaddr, allocated_frame, ste->writable);
  lock_release (&frame_lock);
  if (!success)
    return success;
//printf("before load_file\n");
  if (ste->swap)
  {
uaddr_set_pin_true (ste->uaddr, &thread_current ()->spage_table);
    swap_load_page (ste->swap_index, allocated_frame);
uaddr_set_pin_false (ste->uaddr, &thread_current ()->spage_table);
    ste->swap = false;
    success = true;
    //swap_free_page (ste->swap_index);
  }
  else
  {
    if (ste->file)
    {
      success = load_file (ste, allocated_frame);
    }
  } 

//  success = install_page (ste->uaddr, allocated_frame, ste->writable);  
/*  if (success == false)
  {
    frame_free_page (allocated_frame);
  }
*/
  return success;
}
    
bool
spage_mmap (struct file* file, void *addr)
{
  if ((int)addr % PGSIZE != 0 || addr == 0)
    return false;

  
/* same as load_segment. */
  size_t ofs = 0;
  uint32_t read_bytes = file_length (file);
  
  struct spage_table_entry *ste;
  struct thread *t = thread_current ();
  
  file_seek (file, ofs);
  while (read_bytes > 0)
  {
    size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
    size_t page_zero_bytes = PGSIZE - page_read_bytes;
  
    ste = malloc (sizeof (struct spage_table_entry));
    if (!ste)
      return false;
    ste->uaddr = addr;
    ste->writable = true;
    ste->mmap = true;
    ste->file_ptr = file;
    ste->file = true;
    ste->swap = false;
    ste->ofs = ofs;
    ste->read_bytes = page_read_bytes;
    ste->zero_bytes = page_zero_bytes;
    ste->pin = false;

    if (hash_insert (&t->spage_table, &ste->hash_elem) != NULL)
      return false;

    ofs += page_read_bytes;
    read_bytes -= page_read_bytes;
    addr += PGSIZE;
//    pg_number += 1;
  }
  return true;
}

void
spage_write_back (struct spage_table_entry *ste, struct thread *t)
{
  bool dirty;
  if (ste == NULL)
    return;

  dirty = pagedir_is_dirty(t->pagedir, ste->uaddr);
  if (dirty)
  {
    uaddr_set_pin_true (ste->uaddr, &t->spage_table);
//    file_write_at (ste->file_ptr, pagedir_get_page(t->pagedir, ste->uaddr), ste->read_bytes, ste->ofs);
    file_write_at (ste->file_ptr, ste->uaddr, ste->read_bytes, ste->ofs);
    uaddr_set_pin_false (ste->uaddr, &t->spage_table);
  }
}
/* same as hash_destroy, but I could not find argument. so make it */
void
spage_destroy (struct hash *spage_table)
{
  struct hash_iterator iter;
  struct hash_elem *e;
  struct spage_table_entry *ste;
  hash_first (&iter, spage_table);
  e = hash_next (&iter);
  while (e != NULL)
  {
    ste = hash_entry (e, struct spage_table_entry, hash_elem);
    if (!ste)
      return;
    bool success = spage_free_page (ste->uaddr, spage_table);
    if (!success)
      return;
    hash_first (&iter, spage_table);
    e = hash_next (&iter);
  }
}

void
uaddr_set_pin (void *uaddr, struct hash *spage_table, bool pin)
{

lock_acquire (&pinning_lock);
  struct spage_table_entry ste;
  struct hash_elem *e;
  struct spage_table_entry *ste_en;
  ste.uaddr = uaddr;
  e = hash_find (spage_table, &ste.hash_elem);
  if (e != NULL)
  {  
    ste_en = hash_entry (e, struct spage_table_entry, hash_elem);
    ste_en->pin = pin;
  }
lock_release (&pinning_lock);
}

void uaddr_set_pin_true (void *uaddr, struct hash *spage_table)
{
  uaddr_set_pin (uaddr, spage_table, true);
}

void uaddr_set_pin_false (void *uaddr, struct hash *spage_table)
{
  uaddr_set_pin (uaddr, spage_table, false);
}
