#pragma once

#ifdef __GNUC__
// FIXME: use _Noreturn for all compilers when we move to C11
#define NORETURN __attribute__((noreturn))
#elif defined(_MSC_VER)
#define NORETURN __declspec(noreturn)
#else
#define NORETURN /* nothing */
#endif

static inline NORETURN void graphviz_exit(int status) {
#ifdef __MINGW32__
  // workaround for https://gitlab.com/graphviz/graphviz/-/issues/2178
  fflush(stdout);
  fflush(stderr);
#ifdef __cplusplus
  std::cout.flush();
  std::cerr.flush();
#endif
#endif
  exit(status);
}

#undef NORETURN
