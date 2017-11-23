#include <stdbool.h>
#include <stddef.h>
#include <inttypes.h>
#include <bitmap.h>
#include "devices/block.h"
#include "threads/vaddr.h"
#include "vm/swap.h"
#include "vm/frame.h"

#define SECTORS_IN_PAGE (PGSIZE / BLOCK_SECTOR_SIZE)

static struct block *swap_device;
static struct bitmap *swap_table;

void swap_init(void)
{
  swap_device = block_get_role(BLOCK_SWAP);
  swap_table = bitmap_create(block_size(swap_device) / SECTORS_IN_PAGE);
  if(!swap_device || !swap_table)
    PANIC("swap_init failed.");
  bitmap_set_all(swap_table, false);
}

/* Loads the content of swap slot at INDEX into a frame at KADDR.
   Afterwards, the swap slot is freed by setting swap_table's bit to 0. */
void swap_load_page(size_t index, uint32_t* kaddr)
{
  uint32_t cnt, cur_sector = (uint32_t)index * SECTORS_IN_PAGE;
  for(cnt = 0; cnt < SECTORS_IN_PAGE; cnt++)
    block_read(swap_device, cur_sector + cnt, (uint8_t *)kaddr + cnt * BLOCK_SECTOR_SIZE);
  bitmap_reset(swap_table, index);
}

/* Finds a free swap slot for saving frame's content at KADDR. If there is no
   free swap slot, panics the kernel. */
size_t swap_save_page(uint32_t* kaddr)
{
  size_t index = bitmap_scan_and_flip(swap_table, 0, 1, 0);
  if(index == BITMAP_ERROR)
    PANIC("swap slot is full.");
  uint32_t cnt, cur_sector = (uint32_t)index * SECTORS_IN_PAGE;
  for(cnt = 0; cnt < SECTORS_IN_PAGE; cnt++)
    block_write(swap_device, cur_sector + cnt, (uint8_t *)kaddr + cnt * BLOCK_SECTOR_SIZE);
  return index;
}

void swap_free_page(size_t index)
{
  ASSERT(bitmap_test(swap_table, index)); // This assertion can be removed, if necessary.
  bitmap_reset(swap_table, index);
}
