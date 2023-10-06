/// \file
/// \brief Implementation of a dynamically expanding stack data structure

#pragma once

#include "exit.h"
#include "likely.h"

typedef struct {
  void **base;     ///< underlying store of contained elements
  size_t size;     ///< number of elements on the stack
  size_t capacity; ///< total number of elements that can fit without expansion
} gv_stack_t;

static inline size_t stack_size(const gv_stack_t *stack) {
  assert(stack != NULL);
  return stack->size;
}

static inline bool stack_is_empty(const gv_stack_t *stack) {
  assert(stack != NULL);
  return stack_size(stack) == 0;
}

static inline int stack_push(gv_stack_t *stack, void *item) {

  assert(stack != NULL);

  // do we need to expand the stack to make room for this item?
  if (stack->size == stack->capacity) {

    // Capacity to allocate on the first push to a `gv_stack_t`. We pick
    // something that works out to an allocation of 4KB, a common page size on
    // multiple platforms, as a reasonably efficient default.
    enum { FIRST_ALLOCATION = 4096 / sizeof(void *) };

    // will our resize calculation overflow?
    if (UNLIKELY(SIZE_MAX / 2 < stack->capacity)) {
      return EOVERFLOW;
    }

    size_t c = stack->capacity == 0 ? FIRST_ALLOCATION : (2 * stack->capacity);
    void **b = (void**) realloc(stack->base, sizeof(b[0]) * c);
    if (UNLIKELY(b == NULL)) {
      return ENOMEM;
    }
    stack->capacity = c;
    stack->base = b;
  }

  assert(stack->base != NULL);
  assert(stack->capacity > stack->size);

  // insert the new item
  stack->base[stack->size] = item;
  ++stack->size;

  return 0;
}

static inline void stack_push_or_exit(gv_stack_t *stack, void *item) {

  assert(stack != NULL);

  int r = stack_push(stack, item);
  if (UNLIKELY(r != 0)) {
    fprintf(stderr, "stack_push failed: %s\n", strerror(r));
    graphviz_exit(EXIT_FAILURE);
  }
}

static inline void *stack_top(gv_stack_t *stack) {

  assert(stack != NULL);
  assert(!stack_is_empty(stack) && "access to top of an empty stack");

  return stack->base[stack->size - 1];
}

static inline void *stack_pop(gv_stack_t *stack) {
  void *top = stack_top(stack);
  --stack->size;
  return top;
}

static inline void stack_reset(gv_stack_t *stack) {

  assert(stack != NULL);

  free(stack->base);
  memset(stack, 0, sizeof(*stack));
}
