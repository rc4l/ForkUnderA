// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
//
// [rc4l] ZX_THREAD_SANITIZER is defined in a ThreadSanitizer build, from GCC's or Clang's own flag, for the code that steps aside for it (README.md).

#ifndef ZX_SANITIZER_H
#define ZX_SANITIZER_H

#if defined(__SANITIZE_THREAD__)
#define ZX_THREAD_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define ZX_THREAD_SANITIZER 1
#endif
#endif

#endif
