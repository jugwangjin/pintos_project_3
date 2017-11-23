#ifndef VM_FRAME_H
#define VM_FRAME_H
#include <hash.h>
#include "threads/thread.h"
#include "vm/spage.h"
#include "threads/palloc.h"
#include "threads/synch.h"

struct frame_table_entry
  {
    struct hash_elem hash_elem;

    void *uaddr;
    void *kaddr;
    struct thread *thread;
    bool pin;
  };

struct hash frame_table;
struct lock frame_lock;
struct lock pinning_lock;

void frame_init (void);
unsigned frame_hash (const struct hash_elem *p_, void *aux);
bool frame_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux);
void *frame_get_page (enum palloc_flags flags, struct spage_table_entry *ste);
bool frame_table_insert (void *frame, void *uaddr);
//void frame_set_pin_true (void *frame);
//void frame_set_pin_false (void *frame);
#endif
