#!/bin/bash
# musl-gcc compilation compatibility check script
# Check whether all source files can be compiled under musl libc

set -e

cd /mnt/e/Work/luci-app-komari-agent-c

INCLUDES="-Iinclude -Iinclude/komari-agent-c -Isrc -Isrc/vendor -Isrc/utils -Isrc/config -Isrc/network -Isrc/monitoring -Isrc/protocol -Isrc/report -Isrc/ping -Isrc/terminal -Isrc/core -Isrc/autodiscovery -Isrc/update"
CFLAGS="-std=c99 -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra"

# Note: musl-gcc comes with musl libc headers, do not add -I/usr/include (would cause glibc/musl header conflicts)
# Files missing openssl/zlib headers will fail, but this is not a code compatibility issue
# In a real OpenWrt SDK environment, these headers are located in staging_dir

echo "=== musl-gcc compilation compatibility check ==="
echo "Compiler: $(musl-gcc --version | head -1)"
echo ""

FAILED=0
TOTAL=0

for f in $(find src/ -name "*.c" -not -path "src/vendor/*"); do
    TOTAL=$((TOTAL + 1))
    if musl-gcc $CFLAGS $INCLUDES -c "$f" -o /dev/null 2>/dev/null; then
        echo "  OK: $f"
    else
        echo "  FAIL: $f"
        musl-gcc $CFLAGS $INCLUDES -c "$f" -o /dev/null 2>&1 | head -5 | sed 's/^/    /'
        FAILED=$((FAILED + 1))
    fi
done

echo ""
echo "=== Result: $((TOTAL - FAILED))/$TOTAL files compiled successfully ==="
if [ $FAILED -eq 0 ]; then
    echo "musl libc compatibility: PASS"
    exit 0
else
    echo "musl libc compatibility: FAIL ($FAILED files failed)"
    exit 1
fi
