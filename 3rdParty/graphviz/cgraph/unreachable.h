#pragma once

/** Marker for a point in code which execution can never reach.
 *
 * As a C11 function, this could be thought of as:
 *
 *   _Noreturn void UNREACHABLE(void);
 *
 * This can be used to explain that a switch is exhaustive:
 *
 *   switch (…) {
 *   default: UNREACHABLE();
 *   …remaining cases that cover all possibilities…
 *   }
 *
 * or that a function coda can be omitted:
 *
 *   int foo(void) {
 *     while (always_true()) {
 *     }
 *     UNREACHABLE();
 *   }
 */
#define UNREACHABLE()                                                          \
  do {                                                                         \
    fprintf(stderr, "%s:%d: claimed unreachable code was reached", __FILE__,   \
            __LINE__);                                                         \
    abort();                                                                   \
  } while (0)
