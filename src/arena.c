#include "arena.h"
#include <stdlib.h>
#include <string.h>

enum { ARENA_ALIGN = 16, ARENA_BLOCK = 1 << 20 };

struct ArenaBlock {
  ArenaBlock *next;
  unsigned char *data;
  size_t used;
  size_t size;
};

static size_t aligned(size_t size) {
  return (size + ARENA_ALIGN - 1) / ARENA_ALIGN * ARENA_ALIGN;
}

void arena_init(Arena *arena, size_t limit) {
  arena->head = NULL;
  arena->total = 0;
  arena->limit = limit;
}

/* A new block of at least NEED bytes at the head of the list, or NULL. */
static ArenaBlock *arena_grow(Arena *arena, size_t need) {
  size_t header = aligned(sizeof(ArenaBlock));
  size_t size = need > ARENA_BLOCK ? need : ARENA_BLOCK;
  if (header + size > arena->limit - arena->total)
    return NULL;
  unsigned char *raw = malloc(header + size);
  if (raw == NULL)
    return NULL;
  ArenaBlock *block = (ArenaBlock *)(void *)raw;
  block->next = arena->head;
  block->data = raw + header;
  block->used = 0;
  block->size = size;
  arena->head = block;
  arena->total += header + size;
  return block;
}

void *arena_alloc(Arena *arena, size_t size) {
  if (size > arena->limit)
    return NULL;
  size_t need = aligned(size == 0 ? 1 : size);
  ArenaBlock *block = arena->head;
  if (block == NULL || block->size - block->used < need)
    block = arena_grow(arena, need);
  if (block == NULL)
    return NULL;
  unsigned char *memory = block->data + block->used;
  block->used += need;
  memset(memory, 0, need);
  return memory;
}

void arena_free(Arena *arena) {
  while (arena->head != NULL) {
    ArenaBlock *next = arena->head->next;
    free(arena->head);
    arena->head = next;
  }
  arena->total = 0;
}
