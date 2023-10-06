/*************************************************************************
 * Copyright (c) 2011 AT&T Intellectual Property
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v1.0
 * which accompanies this distribution, and is available at
 * http://www.eclipse.org/legal/epl-v10.html
 *
 * Contributors: Details at https://graphviz.org
 *************************************************************************/

#pragma once

#include "../cgraph/alloc.h"

/// a description of where a buffer is located
typedef enum {
  AGXBUF_INLINE_SIZE_0 = 0,
  AGXBUF_ON_HEAP = 254,  ///< buffer is dynamically allocated
  AGXBUF_ON_STACK = 255, ///< buffer is statically allocated
  /// other values mean an inline buffer with size N
} agxbuf_loc_t;

/// extensible buffer
///
/// Malloc'ed memory is never released until \p agxbdisown or \p agxbfree is
/// called.
///
/// This has the following layout assuming x86-64.
///
///                                                               located
///                                                                  ↓
///   ┌───────────────┬───────────────┬───────────────┬─────────────┬─┐
///   │      buf      │     size      │   capacity    │   padding   │ │
///   ├───────────────┴───────────────┴───────────────┴─────────────┼─┤
///   │                             store                           │ │
///   └─────────────────────────────────────────────────────────────┴─┘
///   0               8               16              24              32
///
/// \p buf, \p size, and \p capacity are in use when \p located is
/// \p AGXBUF_ON_HEAP or \p AGXBUF_ON_STACK. \p store is in use when \p located
/// is > \p AGXBUF_ON_STACK.
typedef struct {
  union {
    struct {
      char *buf;                        ///< start of buffer
      size_t size;                      ///< number of characters in the buffer
      size_t capacity;                  ///< available bytes in the buffer
      char padding[sizeof(size_t) - 1]; ///< unused; for alignment
      unsigned char
          located; ///< where does the backing memory for this buffer live?
    };
    char store[sizeof(char *) + sizeof(size_t) * 3 -
               1]; ///< inline storage used when \p located is
                   ///< > \p AGXBUF_ON_STACK
  };
} agxbuf;

static inline bool agxbuf_is_inline(const agxbuf *xb) {
  assert((xb->located == AGXBUF_ON_HEAP || xb->located == AGXBUF_ON_STACK ||
          xb->located <= sizeof(xb->store)) &&
         "corrupted agxbuf type");
  return xb->located < AGXBUF_ON_HEAP;
}

/* agxbinit:
 * Initializes new agxbuf; caller provides memory.
 * Assume if init is non-null, hint = sizeof(init[])
 */
static inline void agxbinit(agxbuf *xb, unsigned int hint, char *init) {
  if (init != NULL) {
    xb->buf = init;
    xb->located = AGXBUF_ON_STACK;
  } else {
    memset(xb->store, 0, sizeof(agxbuf));
    return;
  }
  xb->size = 0;
  xb->capacity = hint;
}

/* agxbfree:
 * Free any malloced resources.
 */
static inline void agxbfree(agxbuf *xb) {
  if (xb->located == AGXBUF_ON_HEAP)
    free(xb->buf);
}

/* agxbstart
 * Return pointer to beginning of buffer.
 */
static inline char *agxbstart(agxbuf *xb) {
  return agxbuf_is_inline(xb) ? xb->store : xb->buf;
}

/* agxblen:
 * Return number of characters currently stored.
 */
static inline size_t agxblen(const agxbuf *xb) {
  if (agxbuf_is_inline(xb)) {
    return xb->located - AGXBUF_INLINE_SIZE_0;
  }
  return xb->size;
}

/// get the capacity of the backing memory of a buffer
///
/// In contrast to \p agxblen, this is the total number of usable bytes in the
/// backing store, not the total number of currently stored bytes.
///
/// \param xb Buffer to operate on
/// \return Number of usable bytes in the backing store
static inline size_t agxbsizeof(const agxbuf *xb) {
  if (agxbuf_is_inline(xb)) {
    return sizeof(xb->store);
  }
  return xb->capacity;
}

/* agxbpop:
 * Removes last character added, if any.
 */
static inline int agxbpop(agxbuf *xb) {

  size_t len = agxblen(xb);
  if (len == 0) {
    return -1;
  }

  if (agxbuf_is_inline(xb)) {
    assert(xb->located > AGXBUF_INLINE_SIZE_0);
    int c = xb->store[len - 1];
    --xb->located;
    return c;
  }

  int c = xb->buf[xb->size - 1];
  --xb->size;
  return c;
}

/* agxbmore:
 * Expand buffer to hold at least ssz more bytes.
 */
static inline void agxbmore(agxbuf *xb, size_t ssz) {
  size_t cnt = 0;   // current no. of characters in buffer
  size_t size = 0;  // current buffer size
  size_t nsize = 0; // new buffer size
  char *nbuf;       // new buffer

  size = agxbsizeof(xb);
  nsize = size == 0 ? BUFSIZ : (2 * size);
  if (size + ssz > nsize)
    nsize = size + ssz;
  cnt = agxblen(xb);

  if (xb->located == AGXBUF_ON_HEAP) {
    nbuf = (char *)gv_recalloc(xb->buf, size, nsize, sizeof(char));
  } else if (xb->located == AGXBUF_ON_STACK) {
    nbuf = (char *)gv_calloc(nsize, sizeof(char));
    memcpy(nbuf, xb->buf, cnt);
  } else {
    nbuf = (char *)gv_calloc(nsize, sizeof(char));
    memcpy(nbuf, xb->store, cnt);
    xb->size = cnt;
  }
  xb->buf = nbuf;
  xb->capacity = nsize;
  xb->located = AGXBUF_ON_HEAP;
}

/* agxbnext
 * Next position for writing.
 */
static inline char *agxbnext(agxbuf *xb) {
  size_t len = agxblen(xb);
  return agxbuf_is_inline(xb) ? &xb->store[len] : &xb->buf[len];
}

/* support for extra API misuse warnings if available */
#ifdef __GNUC__
#define PRINTF_LIKE(index, first) __attribute__((format(printf, index, first)))
#else
#define PRINTF_LIKE(index, first) /* nothing */
#endif

/* agxbprint:
 * Printf-style output to an agxbuf
 */
static inline PRINTF_LIKE(2, 3) int agxbprint(agxbuf *xb, const char *fmt,
                                              ...) {
  va_list ap;
  size_t size;
  int result;

  va_start(ap, fmt);

  // determine how many bytes we need to print
  {
    va_list ap2;
    int rc;
    va_copy(ap2, ap);
    rc = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (rc < 0) {
      va_end(ap);
      return rc;
    }
    size = (size_t)rc + 1; // account for NUL terminator
  }

  // do we need to expand the buffer?
  {
    size_t unused_space = agxbsizeof(xb) - agxblen(xb);
    if (unused_space < size) {
      size_t extra = size - unused_space;
      agxbmore(xb, extra);
    }
  }

  // we can now safely print into the buffer
  char *dst = agxbnext(xb);
  result = vsnprintf(dst, size, fmt, ap);
  assert(result == (int)(size - 1) || result < 0);
  if (result > 0) {
    if (agxbuf_is_inline(xb)) {
      assert(result <= (int)UCHAR_MAX);
      xb->located += (unsigned char)result;
      assert(agxblen(xb) <= sizeof(xb->store) && "agxbuf corruption");
    } else {
      xb->size += (size_t)result;
    }
  }

  va_end(ap);
  return result;
}

#undef PRINTF_LIKE

/* agxbput_n:
 * Append string s of length ssz into xb
 */
static inline size_t agxbput_n(agxbuf *xb, const char *s, size_t ssz) {
  if (ssz == 0) {
    return 0;
  }
  if (ssz > agxbsizeof(xb) - agxblen(xb))
    agxbmore(xb, ssz);
  size_t len = agxblen(xb);
  if (agxbuf_is_inline(xb)) {
    memcpy(&xb->store[len], s, ssz);
    assert(ssz <= UCHAR_MAX);
    xb->located += (unsigned char)ssz;
    assert(agxblen(xb) <= sizeof(xb->store) && "agxbuf corruption");
  } else {
    memcpy(&xb->buf[len], s, ssz);
    xb->size += ssz;
  }
  return ssz;
}

/* agxbput:
 * Append string s into xb
 */
static inline size_t agxbput(agxbuf *xb, const char *s) {
  size_t ssz = strlen(s);

  return agxbput_n(xb, s, ssz);
}

/* agxbputc:
 * Add character to buffer.
 *  int agxbputc(agxbuf*, char)
 */
static inline int agxbputc(agxbuf *xb, char c) {
  if (agxblen(xb) >= agxbsizeof(xb)) {
    agxbmore(xb, 1);
  }
  size_t len = agxblen(xb);
  if (agxbuf_is_inline(xb)) {
    xb->store[len] = c;
    ++xb->located;
    assert(agxblen(xb) <= sizeof(xb->store) && "agxbuf corruption");
  } else {
    xb->buf[len] = c;
    ++xb->size;
  }
  return 0;
}

/* agxbclear:
 * Resets pointer to data;
 */
static inline void agxbclear(agxbuf *xb) {
  if (agxbuf_is_inline(xb)) {
    xb->located = AGXBUF_INLINE_SIZE_0;
  } else {
    xb->size = 0;
  }
}

/* agxbuse:
 * Null-terminates buffer; resets and returns pointer to data. The buffer is
 * still associated with the agxbuf and will be overwritten on the next, e.g.,
 * agxbput. If you want to retrieve and disassociate the buffer, use agxbdisown
 * instead.
 */
static inline char *agxbuse(agxbuf *xb) {
  (void)agxbputc(xb, '\0');
  agxbclear(xb);
  return agxbstart(xb);
}

/* agxbdisown:
 * Disassociate the backing buffer from this agxbuf and return it. The buffer is
 * NUL terminated before being returned. If the agxbuf is using stack memory,
 * this will first copy the data to a new heap buffer to then return. If you
 * want to temporarily access the string in the buffer, but have it overwritten
 * and reused the next time, e.g., agxbput is called, use agxbuse instead of
 * agxbdisown.
 */
static inline char *agxbdisown(agxbuf *xb) {
  char *buf;

  if (agxbuf_is_inline(xb)) {
    // the string lives in `store`, so we need to copy its contents to heap
    // memory
    buf = gv_strndup(xb->store, agxblen(xb));
  } else if (xb->located == AGXBUF_ON_STACK) {
    // the buffer is not dynamically allocated, so we need to copy its contents
    // to heap memory

    buf = gv_strndup(xb->buf, agxblen(xb));

  } else {
    // the buffer is already dynamically allocated, so terminate it and then
    // take it as-is
    agxbputc(xb, '\0');
    buf = xb->buf;
  }

  // reset xb to a state where it is usable
  agxbinit(xb, 0, NULL);

  return buf;
}
