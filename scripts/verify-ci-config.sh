#!/usr/bin/env bash
# Verify CI configuration files are valid and correctly structured
# Checks Docker-based build pipeline (no host-system compilation)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$SCRIPT_DIR"

PASS=0
FAIL=0

pass() {
    echo "PASS $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "FAIL $1"
    FAIL=$((FAIL + 1))
}

echo "=== CI Configuration Verification ==="
echo ""

# 1. Verify YAML syntax of workflow files
echo "--- YAML Syntax Check ---"
for f in .github/workflows/ci.yml .github/workflows/release.yml docker/docker-compose.yml; do
    if [ ! -f "$f" ]; then
        fail "$f: file not found"
        continue
    fi
    if python3 -c "import yaml; yaml.safe_load(open('$f'))" 2>/dev/null; then
        pass "$f: valid YAML syntax"
    else
        fail "$f: invalid YAML syntax"
    fi
done
echo ""

# 2. Verify ci.yml job dependencies
echo "--- CI Job Dependencies Check ---"
if python3 -c "
import yaml
with open('.github/workflows/ci.yml') as f:
    ci = yaml.safe_load(f)
jobs = ci.get('jobs', {})

# test-openwrt-build should need test-binary-build
towb = jobs.get('test-openwrt-build', {})
needs = towb.get('needs', '')
if needs == 'test-binary-build':
    print('OK: test-openwrt-build needs test-binary-build')
else:
    print(f'FAIL: test-openwrt-build needs should be test-binary-build, got: {needs}')
    exit(1)

# test-openwrt-build should have if: always()
if_cond = towb.get('if', '')
if if_cond == 'always()':
    print('OK: test-openwrt-build has if: always()')
else:
    print(f'FAIL: test-openwrt-build if should be always(), got: {if_cond}')
    exit(1)

# lint should NOT have needs dependency
lint = jobs.get('lint', {})
if 'needs' not in lint:
    print('OK: lint job has no needs dependency (independent)')
else:
    print(f'FAIL: lint job should not have needs, got: {lint.get(\"needs\")}')
    exit(1)
" 2>/dev/null; then
    pass "ci.yml job dependencies correct"
else
    fail "ci.yml job dependencies incorrect"
fi
echo ""

# 3. Verify Docker infrastructure files exist
echo "--- Docker Infrastructure Check ---"
for f in docker/Dockerfile.build docker/build.sh docker/test.sh docker/docker-compose.yml scripts/docker-build.sh .dockerignore; do
    if [ -f "$f" ]; then
        pass "$f: exists"
    else
        fail "$f: missing"
    fi
done

# build.sh and test.sh should be executable
for f in docker/build.sh docker/test.sh scripts/docker-build.sh; do
    if [ -x "$f" ]; then
        pass "$f: executable"
    else
        fail "$f: not executable"
    fi
done
echo ""

# 4. Verify docker-compose.yml defines all required services
echo "--- Docker Compose Services Check ---"
if python3 -c "
import yaml
with open('docker/docker-compose.yml') as f:
    dc = yaml.safe_load(f)
services = dc.get('services', {})
required = ['build-amd64', 'build-arm64', 'build-arm', 'build-armv7',
            'build-mipsel', 'build-mips64', 'build-riscv64', 'build-386', 'test']
for svc in required:
    if svc not in services:
        print(f'FAIL: missing service: {svc}')
        exit(1)
print(f'OK: all {len(required)} services defined')
" 2>/dev/null; then
    pass "docker-compose.yml: all 9 services defined"
else
    fail "docker-compose.yml: missing services"
fi
echo ""

# 5. Verify ci.yml uses Docker for builds (no host-system compilation)
echo "--- Docker-Based Build Check in ci.yml ---"
if grep -q "docker compose -f docker/docker-compose.yml run --rm build-" .github/workflows/ci.yml; then
    pass "ci.yml: test-binary-build uses docker compose run"
else
    fail "ci.yml: test-binary-build does not use docker compose run"
fi

if grep -q "docker compose -f docker/docker-compose.yml run --rm test" .github/workflows/ci.yml; then
    pass "ci.yml: lint job uses docker compose run test"
else
    fail "ci.yml: lint job does not use docker compose run test"
fi

# Ensure no host-system cmake --build in ci.yml (Docker handles it now)
if grep -q "cmake --build" .github/workflows/ci.yml; then
    fail "ci.yml: still contains host-system 'cmake --build' (should be in Docker)"
else
    pass "ci.yml: no host-system cmake --build (compiled in Docker)"
fi

# Ensure no host-system cmake -B build in ci.yml
if grep -q "cmake -B build" .github/workflows/ci.yml; then
    fail "ci.yml: still contains host-system 'cmake -B build' (should be in Docker)"
else
    pass "ci.yml: no host-system cmake -B build (configured in Docker)"
fi
echo ""

# 6. Verify docker/build.sh contains cross-compilation settings
echo "--- Cross-Compilation Config in docker/build.sh ---"
if grep -q "CMAKE_SYSTEM_NAME=Linux" docker/build.sh; then
    pass "docker/build.sh: contains CMAKE_SYSTEM_NAME=Linux"
else
    fail "docker/build.sh: missing CMAKE_SYSTEM_NAME=Linux"
fi

if grep -q "CMAKE_LIBRARY_ARCHITECTURE" docker/build.sh; then
    pass "docker/build.sh: contains CMAKE_LIBRARY_ARCHITECTURE"
else
    fail "docker/build.sh: missing CMAKE_LIBRARY_ARCHITECTURE"
fi

if grep -q "PKG_CONFIG_LIBDIR" docker/build.sh; then
    pass "docker/build.sh: contains PKG_CONFIG_LIBDIR"
else
    fail "docker/build.sh: missing PKG_CONFIG_LIBDIR"
fi

if grep -q 'command -v "$CC"' docker/build.sh; then
    pass "docker/build.sh: verifies CC compiler exists"
else
    fail "docker/build.sh: does not verify CC compiler"
fi

if grep -q "Verify binary architecture" docker/build.sh; then
    pass "docker/build.sh: contains binary architecture verification"
else
    fail "docker/build.sh: missing binary architecture verification"
fi
echo ""

# 7. Verify PKG_MIRROR_HASH sed command
echo "--- PKG_MIRROR_HASH sed Command Check ---"
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

cat > "$TMPDIR/Makefile" <<'EOF'
PKG_NAME:=komari-agent-c
PKG_VERSION:=1.0.0
PKG_RELEASE:=1
PKG_MAINTAINER:=zhz8888
PKG_LICENSE:=MIT
PKG_SOURCE_PROTO:=git
PKG_SOURCE_URL:=https://github.com/zhz8888/luci-app-komari-agent-c.git
PKG_SOURCE_VERSION:=v1.0.0
EOF

sed -i "s/PKG_SOURCE_VERSION:=v[0-9.]\+/PKG_SOURCE_VERSION:=abc123/" "$TMPDIR/Makefile"
if ! grep -q "^PKG_MIRROR_HASH" "$TMPDIR/Makefile"; then
    sed -i "/^PKG_SOURCE_VERSION:/a PKG_MIRROR_HASH:=x" "$TMPDIR/Makefile"
fi

if grep -q "^PKG_MIRROR_HASH:=x$" "$TMPDIR/Makefile"; then
    pass "sed command correctly adds PKG_MIRROR_HASH:=x"
else
    fail "sed command failed to add PKG_MIRROR_HASH:=x"
fi

# Verify idempotency
if ! grep -q "^PKG_MIRROR_HASH" "$TMPDIR/Makefile"; then
    sed -i "/^PKG_SOURCE_VERSION:/a PKG_MIRROR_HASH:=x" "$TMPDIR/Makefile"
fi
COUNT=$(grep -c "^PKG_MIRROR_HASH:=x$" "$TMPDIR/Makefile")
if [ "$COUNT" -eq 1 ]; then
    pass "sed command is idempotent (no duplicate insertion)"
else
    fail "sed command inserted PKG_MIRROR_HASH $COUNT times (expected 1)"
fi
echo ""

# 8. Verify release.yml and ci.yml contain PKG_MIRROR_HASH sed
echo "--- PKG_MIRROR_HASH in Workflows ---"
if grep -q "PKG_MIRROR_HASH:=x" .github/workflows/release.yml; then
    pass "release.yml contains PKG_MIRROR_HASH:=x sed command"
else
    fail "release.yml missing PKG_MIRROR_HASH:=x sed command"
fi

if grep -q "PKG_MIRROR_HASH:=x" .github/workflows/ci.yml; then
    pass "ci.yml contains PKG_MIRROR_HASH:=x sed command"
else
    fail "ci.yml missing PKG_MIRROR_HASH:=x sed command"
fi
echo ""

# Summary
echo "=== Summary ==="
echo "Passed: $PASS"
echo "Failed: $FAIL"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo "All checks passed!"
