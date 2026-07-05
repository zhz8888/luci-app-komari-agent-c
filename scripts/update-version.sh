#!/usr/bin/env bash
# Update version in all project configuration files from git tag.
# Usage: update-version.sh <version>
#   version: semver string (e.g., 1.0.1)
#
# This script is used by the release workflow to ensure the version
# from the git tag (v*) is written into all hardcoded version fields
# before building artifacts.

set -euo pipefail

VERSION="$1"

# Validate version format (strict semver: X.Y.Z)
if ! echo "$VERSION" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "Error: Invalid version format: '$VERSION' (expected semver like 1.0.1)"
    exit 1
fi

MAJOR=$(echo "$VERSION" | cut -d. -f1)
MINOR=$(echo "$VERSION" | cut -d. -f2)
PATCH=$(echo "$VERSION" | cut -d. -f3)

echo "=========================================="
echo "Syncing version to $VERSION"
echo "  major=$MAJOR minor=$MINOR patch=$PATCH"
echo "=========================================="

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ERRORS=0

# ---- 1. include/komari-agent-c/version.h ----
VERSION_H="$PROJECT_ROOT/include/komari-agent-c/version.h"
if [ -f "$VERSION_H" ]; then
    sed -i "s/^#define KOMARI_AGENT_C_VERSION_MAJOR[ \t]\+[0-9]\+/#define KOMARI_AGENT_C_VERSION_MAJOR $MAJOR/" "$VERSION_H"
    sed -i "s/^#define KOMARI_AGENT_C_VERSION_MINOR[ \t]\+[0-9]\+/#define KOMARI_AGENT_C_VERSION_MINOR $MINOR/" "$VERSION_H"
    sed -i "s/^#define KOMARI_AGENT_C_VERSION_PATCH[ \t]\+[0-9]\+/#define KOMARI_AGENT_C_VERSION_PATCH $PATCH/" "$VERSION_H"
    sed -i "s/^#define KOMARI_AGENT_C_VERSION_STRING[ \t]\+\"[^\"]*\"/#define KOMARI_AGENT_C_VERSION_STRING \"$VERSION\"/" "$VERSION_H"
    echo "  [OK] include/komari-agent-c/version.h"
else
    echo "  [WARN] include/komari-agent-c/version.h not found, skipping"
fi

# ---- 2. openwrt/Makefile ----
OPENWRT_MK="$PROJECT_ROOT/openwrt/Makefile"
if [ -f "$OPENWRT_MK" ]; then
    sed -i "s/^PKG_VERSION:=.*/PKG_VERSION:=$VERSION/" "$OPENWRT_MK"
    sed -i "s/^PKG_SOURCE_VERSION:=.*/PKG_SOURCE_VERSION:=v$VERSION/" "$OPENWRT_MK"
    echo "  [OK] openwrt/Makefile"
else
    echo "  [WARN] openwrt/Makefile not found, skipping"
fi

# ---- 3. luci/Makefile ----
LUCI_MK="$PROJECT_ROOT/luci/Makefile"
if [ -f "$LUCI_MK" ]; then
    sed -i "s/^PKG_VERSION:=.*/PKG_VERSION:=$VERSION/" "$LUCI_MK"
    echo "  [OK] luci/Makefile"
else
    echo "  [WARN] luci/Makefile not found, skipping"
fi

echo ""
if [ "$ERRORS" -eq 0 ]; then
    echo "Version update to $VERSION completed successfully."
else
    echo "Version update completed with $ERRORS error(s)."
    exit 1
fi
