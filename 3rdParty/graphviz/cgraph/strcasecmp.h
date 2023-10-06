// platform abstraction for case-insensitive string functions

#pragma once

#ifdef _MSC_VER
// redirect these to the Windows alternatives

// some third-party libraries like libgd provide their own macro-based
// `strcasecmp` shim, so only define our own if their’s is not in scope
#ifndef strcasecmp
static inline int strcasecmp(const char *s1, const char *s2) {
  return _stricmp(s1, s2);
}
#endif

static inline int strncasecmp(const char *s1, const char *s2, size_t n) {
  return _strnicmp(s1, s2, n);
}

#else
// other platforms define these in strings.h

#endif
