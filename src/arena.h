/* Arena allocation of the langc front end: the tokens, names and AST
 * nodes of one run live in one arena, and arena_free releases them at
 * once. */
#ifndef LANG_ARENA_H
#define LANG_ARENA_H
#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

typedef struct {
  ArenaBlock *head;
  size_t total;  /* bytes taken from malloc */
  size_t limit;  /* most bytes the arena may take */
} Arena;

void arena_init(Arena *arena, size_t limit);
/* Zeroed memory aligned to 16 bytes, or NULL when the limit or malloc refuses. */
void *arena_alloc(Arena *arena, size_t size);
void arena_free(Arena *arena);
#endif
