#!/bin/sh
#
# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# apk-build -- construct a .apk from a directory
# Uses OpenWrt SDK's apk tool to build a real APK v3 format package
# No longer uses hand-written tar packaging method

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

version=1.0

APK="$(command -v apk)"
if [ -z "$APK" ]; then
	cat >&2 <<EOF
${RED}[ERROR]${NC} apk tool not found, cannot build APK v3 format package.

A real APK v3 format requires the OpenWrt SDK's apk tool to generate the correct package format.
This script no longer uses hand-written tar packaging to generate pseudo APK packages.

Please install the apk tool via one of the following methods:

  1. Install the full OpenWrt SDK and add staging_dir/host/bin to PATH
  2. Install the apk-tools (apk) command-line tool
  3. Use the OpenWrt build system's 'make package/komari-agent-c/compile' to build

After installation, make sure the 'apk' command is available in PATH, then re-run this script.
EOF
	exit 1
fi

usage="Usage: $0 [-v] [-h] <pkg_directory> [<destination_directory>]"
while getopts "hvm:" opt; do
	case $opt in
	v)
		echo "$version"
		exit 0
		;;
	h) echo "$usage" >&2 ;;
	\?) echo "$usage" >&2 ;;
	esac
done

shift $((OPTIND - 1))

case $# in
1)
	dest_dir=$PWD
	;;
2)
	dest_dir=$2
	if [ "$dest_dir" = "." ] || [ "$dest_dir" = "./" ]; then
		dest_dir=$PWD
	fi
	;;
*)
	echo "$usage" >&2
	exit 1
	;;
esac

pkg_dir="$(realpath "$1")"

if [ ! -d "$pkg_dir" ]; then
	error "Directory $pkg_dir does not exist"
fi

# CONTROL directory check
CONTROL=
[ -d "$pkg_dir"/CONTROL ] && CONTROL=CONTROL
if [ -z "$CONTROL" ]; then
    error "Directory $pkg_dir has no CONTROL subdirectory"
fi

info "Starting APK build for $pkg_dir"
info "Using apk tool: $APK"

cd "$pkg_dir"

# The APKINDEX template ships with a placeholder installed-size; apk mkpkg
# does not compute it, so we measure the on-disk footprint (KB) and patch
# the field before packaging, otherwise the package manager shows "-".
installed_size=$(du -sk "$pkg_dir" 2>/dev/null | cut -f1)
if [ -n "$installed_size" ] && [ -f "$pkg_dir"/CONTROL/APKINDEX ]; then
    sed -i -e "s/^installed-size: .*/installed-size: $installed_size/" \
        "$pkg_dir"/CONTROL/APKINDEX
    info "Updated installed-size to ${installed_size} KB"
fi

if ! "$APK" mkpkg --output "$dest_dir" "$pkg_dir"; then
	error "apk mkpkg failed to build package from $pkg_dir"
fi

info "Packaged contents of $pkg_dir into $dest_dir"
