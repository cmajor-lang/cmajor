/// \file
/// \brief String tokenization
///
/// This is essentially equivalent to `strtok` but with two main improvements:
///
///   1. The input string is not modified. This means, if you have a `const`
///      string, you do not need to `strdup` it in order to tokenize it. This
///      (combined with other properties like no opaque struct pointers) enables
///      you to tokenize a string with no heap allocation.
///
///   2. No global state. All the state for tokenization is contained in the
///      `tok_t` struct.
///
/// The above two properties are intended to make string tokenization scalable
/// (no locks, no thread-shared state) and transparent to the compiler (a good
/// optimizing compiler implements all the string.h functions we use as
/// built-ins and, if `separators` is a compile-time literal, can typically
/// flatten everything into a tight loop with no function calls).
///
/// Sample usage:
///
///   const char my_input[] = "foo; bar:/baz";
///   for (tok_t t = tok(my_input, ";:/"); !tok_end(&t); tok_next(&t)) {
///     strview_t s = tok_get(&t);
///     printf("%.*s\n", (int)s.size, s.data);
///   }
///   // prints “foo”, “ bar”, “baz”

#pragma once

#include "strview.h"

/// state for an in-progress string tokenization
typedef struct {
  const char *start;      ///< start of the string being scanned
  const char *separators; ///< characters to treat as token separators
  strview_t next;         ///< next token to yield
} tok_t;

/// begin tokenization of a new string
static inline tok_t tok(const char *input, const char *separators) {

  assert(input != NULL);
  assert(separators != NULL);
  assert(strcmp(separators, "") != 0 &&
         "at least one separator must be provided");

#ifndef NDEBUG
  for (const char *s1 = separators; *s1 != '\0'; ++s1) {
    for (const char *s2 = s1 + 1; *s2 != '\0'; ++s2) {
      assert(*s1 != *s2 && "duplicate separator characters");
    }
  }
#endif

  tok_t t = { input, separators, {} };

  // find the end of the first token
  size_t size = strcspn(input, separators);
  t.next = { input, size };

  return t;
}

/// is this tokenizer exhausted?
static inline bool tok_end(const tok_t *t) {

  assert(t != NULL);

  return t->next.data == NULL;
}

/// get the current token
static inline strview_t tok_get(const tok_t *t) {

  assert(t != NULL);
  assert(t->next.data != NULL && "extracting from an exhausted tokenizer");

  return t->next;
}

/// advance to the next token in the string being scanned
static inline void tok_next(tok_t *t) {

  assert(t != NULL);
  assert(t->start != NULL);
  assert(t->separators != NULL);
  assert(t->next.data != NULL && "advancing an exhausted tokenizer");

  // resume from where the previous token ended
  const char *start = t->next.data + t->next.size;

  // if we are at the end of the string, we are done
  if (start == t->start + strlen(t->start)) {
    t->next = strview_t{0};
    return;
  }

  // skip last separator characters
  start += strspn(start, t->separators);

  // find the end of the next token
  size_t size = strcspn(start, t->separators);

  t->next = { start, size };
}
