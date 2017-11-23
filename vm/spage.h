#ifndef VM_SPAGE_H
#define VM_SPAGE_H
#include <hash.h>
#include "threads/thread.h"
struct spage_table_entry
{
  struct hash_elem hash_elem;
 
  void *uaddr;
  bool writable;
  bool mmap;
  
  bool swap;
  size_t swap_index;
  
  bool file;
  struct file *file_ptr;
  size_t ofs;
  size_t read_bytes;
  size_t zero_bytes;
  bool pin;
};

void spage_init (struct hash *spage_table);
unsigned spage_hash (const struct hash_elem *p_, void *aux);
bool spage_less (const struct hash_elem *a_, const struct hash_elem *b_,
                 void *aux);
struct spage_table_entry *get_spage (struct hash *spage_table, void *uaddr);
bool make_spage_for_stack_growth (struct hash *spage_table, void *esp);
bool load_file (struct spage_table_entry *ste, void *frame);
bool spage_get_frame (struct spage_table_entry *ste);
bool spage_free_page (void *uaddr, struct hash *spage_table);
bool spage_mmap (struct file *file, void *addr);
void spage_munmap (void *addr);
void spage_destroy (struct hash *spage_table);
void spage_write_back (struct spage_table_entry *ste, struct thread *t);
void uaddr_set_pin_true (void *uaddr, struct hash *spage_table);
void uaddr_set_pin_false (void *uaddr, struct hash *spage_table);
#endif
