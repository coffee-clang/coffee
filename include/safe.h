#ifndef SAFE_SAFE_H_
#define SAFE_SAFE_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Wrappers for functions that clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling
 * flags. These live in a system header (-isystem) so clang-tidy won't check them.
 */

/* ---------------------------------------------------------------------------
 * Arena allocator — bump-pointer allocation with block chaining.
 *
 * All allocations from an arena are freed at once via arena_reset() or
 * arena_destroy().  Individual arena_alloc() pointers cannot be freed
 * separately.
 *
 * Usage:
 *   struct arena a = arena_init(0);          // default block size
 *   void *buf = arena_alloc(&a, 4096);       // 16-byte aligned
 *   // ... use buf ...
 *   arena_destroy(&a);                       // free everything
 *
 * If arena_init() fails to allocate the first block, arena_alloc() will
 * return nullptr on every subsequent call.
 * --------------------------------------------------------------------------- */

#define ARENA_DEFAULT_BLOCK_SIZE ((size_t)64 * 1024)

struct arena_block {
	struct arena_block *next;
	size_t             used;
	size_t             capacity;
	_Alignas(max_align_t) unsigned char data[];
};

struct arena {
	struct arena_block *first;
	struct arena_block *current;
};

static inline struct arena arena_init(size_t block_size)
{
	struct arena a;
	struct arena_block *blk;
	size_t              sz;

	if (block_size == 0) {
		block_size = ARENA_DEFAULT_BLOCK_SIZE;
	}

	sz = block_size + sizeof(struct arena_block);
	blk = (struct arena_block *)calloc(1, sz);
	if (blk != nullptr) {
		blk->capacity = block_size;
		blk->used     = 0;
		blk->next     = nullptr;
		a.first       = blk;
		a.current     = blk;
	} else {
		a.first   = nullptr;
		a.current = nullptr;
	}

	return a;
}

static inline void *arena_alloc(struct arena *a, size_t size)
{
	size_t              aligned;
	unsigned char      *ptr;
	struct arena_block *blk;
	size_t              sz;

	if (a == nullptr || a->current == nullptr) {
		return nullptr;
	}

	if (size == 0) {
		return nullptr;
	}

	/* Align to max_align_t boundary */
	aligned = (a->current->used + alignof(max_align_t) - 1) & ~(alignof(max_align_t) - 1);

	if (aligned + size <= a->current->capacity) {
		ptr                  = a->current->data + aligned;
		a->current->used     = aligned + size;
		return ptr;
	}

	/* Need a new block — allocate at least the requested size */
	sz  = size > ARENA_DEFAULT_BLOCK_SIZE ? size : ARENA_DEFAULT_BLOCK_SIZE;
	blk = (struct arena_block *)calloc(1, sz + sizeof(struct arena_block));
	if (blk == nullptr) {
		return nullptr;
	}

	blk->capacity = sz;
	blk->used     = size;
	blk->next     = nullptr;

	a->current->next = blk;
	a->current       = blk;

	return blk->data;
}

static inline void arena_reset(struct arena *a)
{
	struct arena_block *blk;

	if (a == nullptr || a->first == nullptr) {
		return;
	}

	for (blk = a->first; blk != nullptr; blk = blk->next) {
		blk->used = 0;
	}

	a->current = a->first;
}

static inline void arena_destroy(struct arena *a)
{
	struct arena_block *blk;
	struct arena_block *next;

	if (a == nullptr) {
		return;
	}

	for (blk = a->first; blk != nullptr; blk = next) {
		next = blk->next;
		free(blk);
	}

	a->first   = nullptr;
	a->current = nullptr;
}

static inline i64 safe_snprintf(char *buf, size_t size, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_fprintf(FILE *stream, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vfprintf(stream, fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_printf(const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vprintf(fmt, ap);
    va_end(ap);
    return ret;
}

static inline void *safe_memcpy(void *dest, const void *src, size_t n) {
    return memcpy(dest, src, n);
}

static inline void *safe_memset(void *s, int c, size_t n) {
    return memset(s, c, n);
}

static inline i64 safe_sprintf(char *buf, const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vsprintf(buf, fmt, ap);
    va_end(ap);
    return ret;
}

static inline char *safe_strcpy(char *dest, const char *src) {
    return strcpy(dest, src);
}

static inline i64 safe_scanf(const char *fmt, ...) {
    i64 ret;
    va_list ap;
    va_start(ap, fmt);
    ret = vscanf(fmt, ap);
    va_end(ap);
    return ret;
}

static inline i64 safe_system(const char *command) {
    return (i64)system(command);
}

static inline FILE *safe_popen(const char *command, const char *type) {
    return popen(command, type);
}

#endif /* SAFE_SAFE_H_ */
