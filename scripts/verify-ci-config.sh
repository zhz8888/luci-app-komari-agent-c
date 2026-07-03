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
for f in docker/Dockerfile.build docker/Dockerfile.legacy docker/build.sh docker/test.sh docker/docker-compose.yml scripts/docker-build.sh .dockerignore; do
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

# 7. Verify PKG_SOURCE_* removal sed command
echo "--- PKG_SOURCE Removal Command Check ---"
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

sed -i '/^PKG_SOURCE_PROTO:/d; /^PKG_SOURCE_URL:/d; /^PKG_SOURCE_VERSION:/d' "$TMPDIR/Makefile"

if ! grep -q "^PKG_SOURCE_" "$TMPDIR/Makefile"; then
    pass "sed command correctly removes all PKG_SOURCE_* lines"
else
    fail "sed command failed to remove all PKG_SOURCE_* lines"
fi

# Verify other lines remain intact
if grep -q "^PKG_NAME:=komari-agent-c" "$TMPDIR/Makefile" && grep -q "^PKG_VERSION:=1.0.0" "$TMPDIR/Makefile"; then
    pass "sed command preserves other Makefile lines"
else
    fail "sed command incorrectly removed non-PKG_SOURCE lines"
fi
echo ""

# 8. Verify release.yml and ci.yml contain PKG_SOURCE removal sed
echo "--- PKG_SOURCE Removal in Workflows ---"
if grep -q "PKG_SOURCE_PROTO:/d" .github/workflows/release.yml; then
    pass "release.yml contains PKG_SOURCE removal sed command"
else
    fail "release.yml missing PKG_SOURCE removal sed command"
fi

if grep -q "PKG_SOURCE_PROTO:/d" .github/workflows/ci.yml; then
    pass "ci.yml contains PKG_SOURCE removal sed command"
else
    fail "ci.yml missing PKG_SOURCE removal sed command"
fi
echo ""

# 9. Verify release.yml uses Docker for binary builds (no host-system compilation)
echo "--- Docker-Based Build Check in release.yml ---"
if grep -q "docker compose -f docker/docker-compose.yml run --rm build-" .github/workflows/release.yml; then
    pass "release.yml: build-binaries uses docker compose run"
else
    fail "release.yml: build-binaries does not use docker compose run"
fi

# release.yml build-binaries should NOT install cross-compiler packages (Docker handles it)
if grep -Eq 'sudo apt-get install -y cmake \$\{?matrix\.pkg\}? libssl-dev' .github/workflows/release.yml; then
    fail "release.yml: still installs host cross-compiler packages (should be in Docker)"
else
    pass "release.yml: no host cross-compiler package install (handled by Docker)"
fi

# release.yml build-binaries should NOT run host-system cmake directly
if grep -Eq 'cmake -B build -DCMAKE_C_COMPILER="\$\{?\{?matrix\.cc' .github/workflows/release.yml; then
    fail "release.yml: still contains host-system 'cmake -B build' with matrix.cc (should be in Docker)"
else
    pass "release.yml: no host-system cmake -B build with matrix.cc (compiled in Docker)"
fi
echo ""

# 10. Verify docker/build.sh uses new CMake options
echo "--- New CMake Options in docker/build.sh ---"
if grep -q "KOMARI_BUILD_PROFILE=binary" docker/build.sh; then
    pass "docker/build.sh: sets KOMARI_BUILD_PROFILE=binary"
else
    fail "docker/build.sh: missing KOMARI_BUILD_PROFILE=binary"
fi

if grep -q "KOMARI_BUILD_TESTS=OFF" docker/build.sh; then
    pass "docker/build.sh: sets KOMARI_BUILD_TESTS=OFF (skip test targets in binary build)"
else
    fail "docker/build.sh: missing KOMARI_BUILD_TESTS=OFF"
fi
echo ""

# 11. Verify CMake modular configuration files exist
echo "--- CMake Modular Config Files Check ---"
for f in cmake/BuildOptions.cmake cmake/Version.cmake cmake/Platform.cmake \
         cmake/CompilerFlags.cmake cmake/Dependencies.cmake cmake/toolchain-openwrt.cmake \
         CMakePresets.json; do
    if [ -f "$f" ]; then
        pass "$f: exists"
    else
        fail "$f: missing"
    fi
done
echo ""

# 12. Verify CMakePresets.json is valid JSON and defines key presets
echo "--- CMakePresets.json Check ---"
if python3 -c "
import json
with open('CMakePresets.json') as f:
    p = json.load(f)
presets = set(cp['name'] for cp in p.get('configurePresets', []))
required = {'default', 'debug', 'release', 'openwrt', 'sanitize', 'coverage'}
missing = required - presets
if missing:
    print(f'FAIL: missing presets: {missing}')
    exit(1)
# openwrt preset must set KOMARI_BUILD_PROFILE=openwrt and use build-openwrt dir
for cp in p['configurePresets']:
    if cp['name'] == 'openwrt':
        cv = cp.get('cacheVariables', {})
        if cv.get('KOMARI_BUILD_PROFILE') != 'openwrt':
            print('FAIL: openwrt preset does not set KOMARI_BUILD_PROFILE=openwrt')
            exit(1)
        if 'build-openwrt' not in cp.get('binaryDir', ''):
            print('FAIL: openwrt preset binaryDir should contain build-openwrt')
            exit(1)
        break
print(f'OK: {len(presets)} presets defined, required presets present, openwrt profile correct')
" 2>/dev/null; then
    pass "CMakePresets.json: valid JSON, required presets present, openwrt profile correct"
else
    fail "CMakePresets.json: invalid or missing required presets/profile"
fi
echo ""

# 13. Verify OpenWrt Makefile uses KOMARI_BUILD_SUBDIR (path separation)
echo "--- OpenWrt Path Separation Check ---"
if grep -q "KOMARI_BUILD_SUBDIR" openwrt/Makefile; then
    pass "openwrt/Makefile: uses KOMARI_BUILD_SUBDIR for path isolation"
else
    fail "openwrt/Makefile: missing KOMARI_BUILD_SUBDIR (path isolation broken)"
fi

if grep -q "KOMARI_BUILD_PROFILE" openwrt/Makefile; then
    pass "openwrt/Makefile: sets KOMARI_BUILD_PROFILE for build profile"
else
    fail "openwrt/Makefile: missing KOMARI_BUILD_PROFILE"
fi

if grep -q "build-openwrt" openwrt/Makefile; then
    pass "openwrt/Makefile: uses build-openwrt/ directory (separate from binary build/)"
else
    fail "openwrt/Makefile: missing build-openwrt directory reference"
fi
echo ""

# 14. Verify CMakeLists.txt includes modular config and supports KOMARI_BUILD_PROFILE
echo "--- Root CMakeLists.txt Modular Integration Check ---"
if grep -q "include(BuildOptions)" CMakeLists.txt; then
    pass "CMakeLists.txt: includes BuildOptions module"
else
    fail "CMakeLists.txt: missing include(BuildOptions)"
fi

if grep -q "include(CompilerFlags)" CMakeLists.txt; then
    pass "CMakeLists.txt: includes CompilerFlags module"
else
    fail "CMakeLists.txt: missing include(CompilerFlags)"
fi

if grep -q "include(Dependencies)" CMakeLists.txt; then
    pass "CMakeLists.txt: includes Dependencies module"
else
    fail "CMakeLists.txt: missing include(Dependencies)"
fi

if grep -q "KOMARI_BUILD_PROFILE" CMakeLists.txt; then
    pass "CMakeLists.txt: references KOMARI_BUILD_PROFILE"
else
    fail "CMakeLists.txt: missing KOMARI_BUILD_PROFILE reference"
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
