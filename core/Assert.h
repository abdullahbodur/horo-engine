#pragma once
#include <cstdio>
#include <cstdlib>

#if defined(_MSC_VER) && !defined(NDEBUG)
#include <intrin.h>
#endif

#ifdef NDEBUG
#define MONOLITH_ASSERT(expr, msg) ((void)0)
#else
#if defined(_MSC_VER)
#define MONOLITH_DEBUG_TRAP() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
#define MONOLITH_DEBUG_TRAP() __builtin_trap()
#else
#define MONOLITH_DEBUG_TRAP() ((void)0)
#endif

#define MONOLITH_ASSERT(expr, msg)                                             \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr,                                                     \
                   "ASSERT FAILED: %s\n  Message: %s\n  File: %s  Line: %d\n", \
                   #expr, msg, __FILE__, __LINE__);                            \
      MONOLITH_DEBUG_TRAP();                                                   \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
#endif
