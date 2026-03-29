#pragma once
#include <cstdio>
#include <cstdlib>

#ifdef NDEBUG
#define MONOLITH_ASSERT(expr, msg) ((void)0)
#else
#define MONOLITH_ASSERT(expr, msg)                                             \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr,                                                     \
                   "ASSERT FAILED: %s\n  Message: %s\n  File: %s  Line: %d\n", \
                   #expr,                                                      \
                   msg,                                                        \
                   __FILE__,                                                   \
                   __LINE__);                                                  \
      __builtin_trap(); /* breaks in debugger on macOS/Linux */                \
    }                                                                          \
  } while (0)
#endif
