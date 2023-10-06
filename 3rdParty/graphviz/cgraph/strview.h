/// \file
/// \brief Non-owning string references
///
/// This is similar to C++17’s `std::string_view`. Instances of `strview_t`
/// should generally be passed around by value rather than pointer as they are
/// small.

#pragma once

#include "../cgraph/alloc.h"
#include "../cgraph/startswith.h"
#include "../cgraph/strcasecmp.h"

/// a non-owning string reference
typedef struct {
  const char *data; ///< start of the pointed to string
  size_t size;      ///< extent of the string in bytes
} strview_t;

/// create a string reference
static inline strview_t strview(const char *referent, char terminator) {

  assert(referent != NULL);

  // can we find the terminator before the end of the containing string?
  const char *end = strchr(referent, terminator);
  if (end != NULL) {
    strview_t temp = {};
    temp.data = referent;
    temp.size = (size_t)(end - referent);
    return temp;
  }

  // otherwise, span the entire string
  strview_t temp = {};
  temp.data = referent;
  temp.size = strlen(referent);
  return temp;
}

/// make a heap-allocated string from this string view
static inline char *strview_str(strview_t source) {

  assert(source.data != NULL);

  return gv_strndup(source.data, source.size);
}

/// compare two string references for case insensitive equality
static inline bool strview_case_eq(strview_t a, strview_t b) {

  assert(a.data != NULL);
  assert(b.data != NULL);

  if (a.size != b.size) {
    return false;
  }

  return strncasecmp(a.data, b.data, a.size) == 0;
}

/// compare a string reference to a string for case insensitive equality
static inline bool strview_case_str_eq(strview_t a, const char *b) {

  assert(a.data != NULL);
  assert(b != NULL);

  return strview_case_eq(a, strview(b, '\0'));
}

/// compare two string references
static inline int strview_cmp(strview_t a, strview_t b) {

  size_t min_size = a.size > b.size ? b.size : a.size;
  int cmp = strncmp(a.data, b.data, min_size);
  if (cmp != 0) {
    return cmp;
  }

  if (a.size > b.size) {
    return 1;
  }
  if (a.size < b.size) {
    return -1;
  }
  return 0;
}

/// compare two string references for equality
static inline bool strview_eq(strview_t a, strview_t b) {

  assert(a.data != NULL);
  assert(b.data != NULL);

  return strview_cmp(a, b) == 0;
}

/// compare a string reference to a string for equality
static inline bool strview_str_eq(strview_t a, const char *b) {

  assert(a.data != NULL);
  assert(b != NULL);

  return strview_eq(a, strview(b, '\0'));
}

/// does the given string appear as a substring of the string view?
static inline bool strview_str_contains(strview_t haystack,
                                        const char *needle) {

  assert(haystack.data != NULL);
  assert(needle != NULL);

  // the empty string is a substring of everything
  if (strcmp(needle, "") == 0) {
    return true;
  }

  for (size_t offset = 0; offset < haystack.size;) {

    // find the next possible starting point for the substring
    const char *candidate = (const char *)memchr(
        haystack.data + offset, needle[0], haystack.size - offset);
    if (candidate == NULL) {
      break;
    }

    // is it too close to the end of the containing string?
    if ((size_t)(haystack.data + haystack.size - candidate) < strlen(needle)) {
      return false;
    }

    // is it a match?
    if (startswith(candidate, needle)) {
      return true;
    }

    // advance to the position after this match for the next scan
    offset = (size_t)(candidate - haystack.data) + 1;
  }

  return false;
}
