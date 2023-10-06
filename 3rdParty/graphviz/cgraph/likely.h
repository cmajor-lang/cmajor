#pragma once

/** Markers to indicate a branch condition is predictably true or false.
 *
 * These hints can be used to signal to the compiler it should bias its
 * optimization towards a particular branch outcome. For example:
 *
 *   p = malloc(20);
 *   if (UNLIKELY(p == NULL)) {
 *     // error path
 *     …
 *   }
 *
 * Despite their names, there are also uses for them on branches that should be
 * biased a certain way even though they may *not* be likely-taken or
 * unlikely-taken. For example, optimizing for the case when a user has not
 * passed extra verbosity flags:
 *
 *   if (UNLIKELY(verbose)) {
 *     …some logging code…
 *   }
 */
#ifdef __GNUC__
#define LIKELY(expr) __builtin_expect(!!(expr), 1)
#define UNLIKELY(expr) __builtin_expect((expr), 0)
#else
#define LIKELY(expr) (expr)
#define UNLIKELY(expr) (expr)
#endif
