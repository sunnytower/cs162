/*
 * mm_alloc.c
 */

#include "mm_alloc.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
size_t metasize = sizeof(block);
block* list = NULL;
void* mm_malloc(size_t size) {
  if (size == 0) {
    return NULL;
  }

  block* cur = list;
  block* prev = NULL;
  /* find if free block */
  while (cur) {
    if (cur->free && (cur->size >= size)) {
      size_t remain = cur->size - size;
      /* could split to 2 block */
      if (remain > metasize) {
        cur->size = size;
        block* split_block = (block*)(cur->data + size);
        split_block->prev = cur;
        split_block->next = cur->next;
        cur->next = split_block;
        if (split_block->next != NULL) {
          split_block->next->prev = split_block;
        }
        split_block->free = true;
        split_block->size = remain - metasize;
      }
      /* just use cur block */
      cur->free = false;
      memset(cur->data, 0, cur->size);
      return cur->data;
    }
    prev = cur;
    cur = cur->next;
  }

  /* allocate new block */
  if ((cur = sbrk(metasize + size)) == (void*)-1) {
    printf("sbrk error\n");
    return NULL;
  }
  if (prev == NULL) {
    list = cur;
  } else {
    prev->next = cur;
  }
  cur->prev = prev;
  cur->next = NULL;
  cur->free = false;
  cur->size = size;
  memset(cur->data, 0, size);
  return cur->data;
}

void* mm_realloc(void* ptr, size_t size) {
  if (ptr == NULL) {
    return mm_malloc(size);
  }
  if (size == 0) {
    mm_free(ptr);
    return NULL;
  }
  block* block_to_realloc = (block*)(ptr - metasize);
  if (block_to_realloc->size >= size) {
    return ptr;
  }
  void* new_ptr = mm_malloc(size);
  if (new_ptr == NULL) {
    return NULL;
  }
  memcpy(new_ptr, ptr, block_to_realloc->size);
  mm_free(ptr);
  return new_ptr;
}

void mm_free(void* ptr) {
  if (ptr == NULL) {
    return;
  }
  block* block_to_free = (block*)(ptr - metasize);
  if (!block_to_free->free) {
    block_to_free->free = true;
  }

  /* merge with prev free block */
  while (block_to_free->prev && block_to_free->prev->free) {
    block_to_free = block_to_free->prev;
    block_to_free->size += block_to_free->next->size + metasize;
    block_to_free->next = block_to_free->next->next;
    block_to_free->next->prev = block_to_free;
  }
  /* merge with next free block */
  while (block_to_free->next && block_to_free->next->free) {
    block* next_block = block_to_free->next;
    block_to_free->size += next_block->size + metasize;
    block_to_free->next = next_block->next;
    if (next_block->next != NULL) {
      next_block->next->prev = block_to_free;
    }
  }
  /* zero fill block_to_free */
  memset(block_to_free->data, 0, block_to_free->size);
}
