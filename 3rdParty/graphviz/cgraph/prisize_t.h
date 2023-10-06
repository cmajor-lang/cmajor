#pragma once

/// \brief \p PRIu64 alike for printing \p size_t
///
/// Use this as:
///
/// \code{.c}
///   size_t value = 42;
///   printf("value is %" PRISIZE_T ".", value);
///   // prints “value is 42.”
/// \endcode
///
/// Note that leaving a space on either side of \p PRISIZE_T does not seem
/// relevant in C, but if you omit this in C++ it will be interpreted as a
/// user-defined string literal indicator. So it is best to always use a space
/// on either side.
#ifdef __MINGW64__
// Microsoft’s Visual C Runtime (msvcrt) ships a printf that does not
// understand "%zu". MSVC itself uses a different printf that does not rely on
// this, but MinGW uses msvcrt and so cannot handle "%zu".
#define PRISIZE_T "llu"
#elif defined(__MINGW32__)
#define PRISIZE_T "u"
#else
#define PRISIZE_T "zu"
#endif
