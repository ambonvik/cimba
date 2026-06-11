#!/usr/bin/env bash
#
# verify_install.sh - Verify that an *installed* copy of Cimba is complete and
# usable, exactly as a downstream user would consume it. This is the check that
# the in-tree build cannot make: it compiles only against the installed headers
# and links only against the installed library, with no access to the source
# tree's include directories.
#
# It verifies three things:
#   1. Every public header (cmb_*.h, cimba.h) compiles standalone, i.e. is
#      self-contained - including it as the first thing in a translation unit
#      pulls in everything it needs. This catches the class of bug where a
#      public header uses a type (va_list, NULL, ...) without including the
#      standard header that defines it, which only "works" in-tree by accident
#      of include order. Checked under both the default dialect and -std=c2x.
#   2. The installed header set is closed: because the probes see only the
#      installed include directory, a public header that includes a header
#      which was not installed fails here.
#   3. The documented hello.c compiles, links against the installed library,
#      and runs - the end-to-end downstream experience from the install guide.
#
# Usage:
#   verify_install.sh [PREFIX]
#
# PREFIX defaults to /usr/local (the location the install guide uses). Pass a
# custom prefix to verify a staged/--prefix install without root.
#
# Exits nonzero on the first failure, with the compiler diagnostics shown.

set -euo pipefail

PREFIX="${1:-/usr/local}"
INCDIR="$PREFIX/include"
CC="${CC:-cc}"

if [[ ! -d "$INCDIR" ]]; then
    echo "verify_install: no include directory at $INCDIR" >&2
    exit 1
fi

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) OS=windows ;;
    Darwin)               OS=macos ;;
    *)                    OS=linux ;;
esac

# Locate what we link against and what must be present at runtime. On Windows
# these differ: gcc links the import library (lib/libcimba.dll.a) while the
# program loads the runtime DLL (bin/libcimba.dll), which therefore has to be on
# PATH to execute. Elsewhere a single shared object serves both roles.
if [[ "$OS" == windows ]]; then
    IMPLIB="$(find "$PREFIX" -name 'libcimba.dll.a' 2>/dev/null | head -n1 || true)"
    RUNLIB="$(find "$PREFIX" -name 'libcimba.dll'    2>/dev/null | head -n1 || true)"
    if [[ -z "$IMPLIB" || -z "$RUNLIB" ]]; then
        echo "verify_install: import lib and/or DLL not found under $PREFIX" >&2
        exit 1
    fi
    LINKDIR="$(dirname "$IMPLIB")"
    RUNDIR="$(dirname "$RUNLIB")"
else
    RUNLIB="$(find "$PREFIX" \( -name 'libcimba.so' -o -name 'libcimba.dylib' \) 2>/dev/null | head -n1 || true)"
    if [[ -z "$RUNLIB" ]]; then
        echo "verify_install: libcimba not found under $PREFIX" >&2
        exit 1
    fi
    LINKDIR="$(dirname "$RUNLIB")"
    RUNDIR="$LINKDIR"
fi

echo "verify_install: prefix=$PREFIX include=$INCDIR linkdir=$LINKDIR rundir=$RUNDIR cc=$CC os=$OS"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# ---- 1 & 2: every public header compiles standalone, against installed dir only
probe_headers() {
    local std_flag="$1" label="$2" failed=0 h
    for h in "$INCDIR"/cmb_*.h "$INCDIR"/cimba.h; do
        local base; base="$(basename "$h")"
        printf '#include <%s>\nint main(void){return 0;}\n' "$base" > "$WORK/probe.c"
        if ! "$CC" $std_flag -I"$INCDIR" -c "$WORK/probe.c" -o "$WORK/probe.o" 2> "$WORK/err"; then
            echo "  FAIL [$label]: $base is not self-contained" >&2
            sed 's/^/      /' "$WORK/err" >&2
            failed=1
        fi
    done
    return $failed
}

echo "verify_install: checking public headers are self-contained (default dialect)"
probe_headers "" "default"
echo "verify_install: checking public headers are self-contained (-std=c2x)"
probe_headers "-std=c2x" "c2x"
echo "  all public headers compile standalone"

# ---- 3: the documented hello.c builds, links, and runs
HELLO="$WORK/hello.c"
cat > "$HELLO" <<'EOF'
#include <cimba.h>
#include <stdio.h>

int main(void) {
    printf("Hello world, I am Cimba %s.\n", cimba_version());
}
EOF

echo "verify_install: building and running the install-guide hello program"
if [[ "$OS" == windows ]]; then
    # No rpath on PE; the loader finds libcimba.dll via PATH.
    "$CC" "$HELLO" -I"$INCDIR" -L"$LINKDIR" -lcimba -o "$WORK/hello.exe"
    OUT="$(PATH="$RUNDIR:$PATH" "$WORK/hello.exe")"
else
    "$CC" "$HELLO" -I"$INCDIR" -L"$LINKDIR" -Wl,-rpath,"$RUNDIR" -lcimba -o "$WORK/hello"
    OUT="$("$WORK/hello")"
fi
echo "  program output: $OUT"
case "$OUT" in
    "Hello world, I am Cimba "*) ;;
    *) echo "verify_install: unexpected program output" >&2; exit 1 ;;
esac

echo "verify_install: OK"
