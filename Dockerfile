# [rc4l] ZandroX dev/test/debug image — a clean Linux box with the full toolchain, so the
# GoogleTest suite + memory sanitizers + coverage run identically for everyone (no Homebrew,
# no Rosetta, no "works on my machine"). The source is MOUNTED at runtime, not copied, so the
# image is just the toolchain and never needs rebuilding as you edit code.
#
# Build once:   docker build -t zandrox-dev .
# Run the gate: docker run --rm -v "$PWD:/work" zandrox-dev
# Debug shell:  docker run --rm -it -v "$PWD:/work" zandrox-dev bash   (then gdb/valgrind work)
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential clang clang-tidy llvm cmake ninja-build git ca-certificates \
      libclang-rt-18-dev \
      gdb valgrind \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /work

# [rc4l] Default: the exact gate CI runs — sanitized build+run, then the 100% coverage check.
# Linux (unlike macOS/Rosetta) has full AddressSanitizer + LeakSanitizer + UBSan support.
CMD ["bash", "-lc", "\
    cmake -S tests -B build-tests -G Ninja -DZANDROX_TESTS_SANITIZE=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ && \
    cmake --build build-tests && \
    ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 \
      ctest --test-dir build-tests --output-on-failure && \
    bash tests/coverage.sh --auto"]
