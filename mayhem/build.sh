#!/usr/bin/env bash
#
# Builds libopenmpt (vendored at Frameworks/OpenMPT/OpenMPT) twice from a scratch copy under
# mayhem/build/ (gitignored): once sanitized for the fuzz harness, once with normal flags for the
# functional suite. The upstream tree itself is never written to.
set -euo pipefail

[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS COVERAGE_FLAGS

cd "$SRC"
OMPT="$SRC/Frameworks/OpenMPT/OpenMPT"
B="$SRC/mayhem/build"

# Cog's tree includes <libOpenMPT/libopenmpt_version.h>; a symlink in the scratch copy resolves it.
prep_tree() {
  rm -rf "$1"
  mkdir -p "$1"
  cp -a "$OMPT/." "$1/"
  ln -s libopenmpt "$1/libOpenMPT"
}

# Bundled miniz/minimp3/stb_vorbis replace zlib/mpg123/vorbis, so no network or apt packages are needed.
OMPT_MAKE_ARGS=(CONFIG=clang EXAMPLES=0 OPENMPT123=0 SHARED_LIB=0 STATIC_LIB=1 QUIET=1
  NO_ZLIB=1 NO_MPG123=1 NO_OGG=1 NO_VORBIS=1 NO_VORBISFILE=1)

# 1) sanitized library for fuzzing (same defines upstream's fuzz build uses)
FUZZ_CPPFLAGS="-I. -DMPT_BUILD_FUZZER -DMPT_BUILD_FATAL_ASSERTS"
# coverage-instrument the library too (the ASan runtime supplies the sancov callbacks for the standalone link);
# without it the fuzzer only sees the harness's own handful of edges
FUZZ_COV=""
[ -n "$LIB_FUZZING_ENGINE" ] && [ -n "$SANITIZER_FLAGS" ] && FUZZ_COV="-fsanitize=fuzzer-no-link"
prep_tree "$B/fuzz"
(
  cd "$B/fuzz"
  CPPFLAGS="$FUZZ_CPPFLAGS" CFLAGS="$SANITIZER_FLAGS $FUZZ_COV $DEBUG_FLAGS" CXXFLAGS="$SANITIZER_FLAGS $FUZZ_COV $DEBUG_FLAGS" \
    LDFLAGS="$SANITIZER_FLAGS" \
    make -j"$MAYHEM_JOBS" "${OMPT_MAKE_ARGS[@]}" TEST=0 OPTIMIZE=speed bin/libopenmpt.a
)

# 2) harness: libFuzzer binary + standalone reproducer, both with LeakSanitizer switched off
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS -c "$SRC/mayhem/lsan_off.cc" -o "$B/lsan_off.o"
$CC $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$B/standalone_main.o"
HARNESS_FLAGS=(-std=c++17 -fexceptions -frtti -pthread -Wno-nonportable-include-path $FUZZ_CPPFLAGS -DLIBOPENMPT_BUILD
  -I"$B/fuzz" -I"$B/fuzz/src" -I"$B/fuzz/common" -I"$B/fuzz/include")
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${HARNESS_FLAGS[@]}" $LIB_FUZZING_ENGINE \
  "$SRC/mayhem/harnesses/fuzz_openmpt.cpp" "$B/lsan_off.o" "$B/fuzz/bin/libopenmpt.a" -lm \
  -o "$SRC/fuzz_openmpt"
# shellcheck disable=SC2086
$CXX $SANITIZER_FLAGS $DEBUG_FLAGS "${HARNESS_FLAGS[@]}" \
  "$SRC/mayhem/harnesses/fuzz_openmpt.cpp" "$B/standalone_main.o" "$B/lsan_off.o" "$B/fuzz/bin/libopenmpt.a" -lm \
  -o "$SRC/fuzz_openmpt-standalone"

# 3) functional suite with the project's NORMAL flags: upstream's libopenmpt_test plus a known-answer probe
prep_tree "$B/test"
(
  cd "$B/test"
  env -u SANITIZER_FLAGS -u DEBUG_FLAGS CPPFLAGS="-I." CFLAGS="$COVERAGE_FLAGS" CXXFLAGS="$COVERAGE_FLAGS" \
    LDFLAGS="$COVERAGE_FLAGS" \
    make -j"$MAYHEM_JOBS" "${OMPT_MAKE_ARGS[@]}" TEST=1 bin/libopenmpt.a bin/libopenmpt_test
  # shellcheck disable=SC2086
  $CXX -O2 $COVERAGE_FLAGS -std=c++17 -pthread -I. "$SRC/mayhem/kat/kat_probe.cpp" bin/libopenmpt.a -lm \
    -o bin/kat_probe
)
