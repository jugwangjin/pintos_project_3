#include "vm/frame.h"
#include "threads/palloc.h"
#include "vm/swap.h"
#include "vm/spage.h"
#include "userprog/pagedir.h"

void
frame_init (void)
{
  hash_init (&frame_table, frame_hash, frame_less, NULL);
  lock_init (&frame_lock);
  clock_hand = NULL;
//  swap_init ();
}

unsigned
frame_hash (const struct hash_elem *p_, void *aux UNUSED)
{
  const struct frame_table_entry *f = hash_entry (p_, struct frame_table_entry, hash_elem);
  return hash_bytes (&f->kaddr, sizeof f->kaddr);
}

bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
            void *aux UNUSED)
{
  const struct frame_table_entry *a = hash_entry (a_, struct frame_table_entry, hash_elem);
  const struct frame_table_entry *b = hash_entry (b_, struct frame_table_entry, hash_elem);
  return a->kaddr < b->kaddr;
}

void
*frame_get_page (enum palloc_flags flags, struct spage_table_entry *spage)
{
  void *frame;
  struct frame_table_entry *fte;
  uint32_t *pd;
  struct hash_iterator it;
  struct hash_elem *e;
  struct spage_table_entry *ste;
 
  frame = NULL;
  if (! (flags & PAL_USER))
    return NULL;
  frame = palloc_get_page (flags);
  if (frame != NULL)
  {
    frame_table_insert (frame, spage->uaddr);
    return frame;
  }
  else
  {
    /* search for kaddr hash */
    if (clock_hand == NULL)
      hash_first (&it, &frame_table);
    else
    {
      hash_first (&it, &frame_table);
      while ((e = hash_next (&it)) != NULL)
      {
        fte = hash_entry (e, struct frame_table_entry, hash_elem);
        if (fte->kaddr == clock_hand)
          break;
      }
      if (e == NULL)
        hash_first (&it, &frame_table);
    }

    while (true)
    {
      e = hash_next (&it);
      if (e == NULL)
      {
	hash_first (&it, &frame_table);
	e = hash_next (&it);
      }
      fte = hash_entry (e, struct frame_table_entry, hash_elem);
      if (fte == NULL)
      {
        PANIC("aa");
      }
      pd = fte->thread->pagedir;
      if (!fte->pin)
      {
	if (pagedir_is_accessed (pd, fte->uaddr))
	  pagedir_set_accessed (pd, fte->uaddr, false);
	else
	{
          ste = get_spage (&fte->thread->spage_table, fte->uaddr);
	  if (ste->mmap)
	    spage_write_back (ste, fte->thread);
          else
          {
            if(pagedir_is_dirty (pd, fte->uaddr))
            {
              ste->swap_index = swap_save_page (fte->kaddr);
              ste->swap = true;
            }
          }
	  pagedir_set_accessed (pd, fte->uaddr, false);
          pagedir_set_dirty (pd, fte->uaddr, false);
	  pagedir_clear_page (pd, fte->uaddr);
	  frame = fte->kaddr;
	  break;
	}
      }
    }
    return frame;
  }
  return NULL;
}

bool
frame_table_insert (void *frame, void *uaddr)
{
  struct frame_table_entry *fte = malloc (sizeof (struct frame_table_entry));
  fte->kaddr = frame;
  fte->uaddr = uaddr;
  fte->thread = thread_current ();
  return hash_insert (&frame_table, &fte->hash_elem);
}

void
frame_free_page (void *frame)
{
  struct frame_table_entry fte;
  struct hash_elem *e;
  struct frame_table_entry *fte_en;
  fte.kaddr = frame;
  e = hash_delete (&frame_table, &fte.hash_elem);
  if (e != NULL)
  {  
    fte_en = hash_entry (e, struct frame_table_entry, hash_elem);
    palloc_free_page (fte_en->kaddr);
    free (fte_en);
  }
}

