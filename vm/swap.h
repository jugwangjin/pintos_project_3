#ifndef VM_SWAP_H
#define VM_SWAP_H

void swap_init(void);
void swap_load_page(size_t index, uint32_t* kaddr);
size_t swap_save_page(uint32_t* kaddr);
void swap_free_page(size_t index);

#endif
