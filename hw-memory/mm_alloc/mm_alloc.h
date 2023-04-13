/*
 * mm_alloc.h
 *
 * Exports a clone of the interface documented in "man 3 malloc".
 */

#pragma once

#ifndef _malloc_H_
#define _malloc_H_

#include <stdlib.h>
#include <stdbool.h>
typedef struct block {
  /* metadata header*/
  size_t size;
  bool free;
  struct block* next;
  struct block* prev;
  /* detail: https://en.wikipedia.org/wiki/Flexible_array_member */
  char data[];
} block;
void* mm_malloc(size_t size);
void* mm_realloc(void* ptr, size_t size);
void mm_free(void* ptr);

#endif
