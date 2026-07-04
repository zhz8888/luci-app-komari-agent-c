#!/bin/sh
#
# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License
#
# ipkg-build -- construct a .ipk from a directory
# Based on OpenWrt ipkg-build script

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
FIND="$(command -v find)"
TAR="$(command -v tar)"

TIMESTAMP=$(date -u "+%Y-%m-%d %H:%M:%S")

ipkg_extract_value() {
	sed -e "s/^[^:]*:[[:space:]]*//"
}

required_field() {
	field=$1

	grep "^$field:" <"$CONTROL/control" | ipkg_extract_value
}

pkg_appears_sane() {
	local pkg_dir="$1"

	local owd="$PWD"
	cd "$pkg_dir"

	PKG_ERROR=0
	pkg="$(required_field Package)"
	# Strip any "Version:" prefix and a leading dot that some control files
	# include, leaving the bare version string.
	version="$(required_field Version | sed 's/Version://; s/^.://g;')"
	arch="$(required_field Architecture)"

	if echo "$pkg" | grep '[^a-zA-Z0-9_.+-]'; then
		echo "*** Error: Package name $name contains illegal characters, (other than [a-z0-9.+-])" >&2
		PKG_ERROR=1
	fi

	# Resolve conffiles: rewrite each absolute path under pkg_dir, drop entries
	# whose target file does not exist, then sort and write back as the
	# authoritative conffiles list.
	if [ -f "$CONTROL/conffiles" ]; then
		rm -f "$CONTROL/conffiles.resolved"

		for cf in $($FIND $(sed -e "s!^/!$pkg_dir/!" "$CONTROL/conffiles") -type f); do
			echo "${cf#$pkg_dir}" >>"$CONTROL/conffiles.resolved"
		done

		rm "$CONTROL"/conffiles
		if [ -f "$CONTROL"/conffiles.resolved ]; then
			LC_ALL=C sort -o "$CONTROL"/conffiles "$CONTROL"/conffiles.resolved
			rm "$CONTROL"/conffiles.resolved
			chmod 0644 "$CONTROL"/conffiles
		fi
	fi

	cd "$owd"
	return $PKG_ERROR
}

file_modes=""
usage="Usage: $0 [-v] [-h] [-m] <pkg_directory> [<destination_directory>]"
while getopts "hvm:" opt; do
	case $opt in
	v)
		echo "$version"
		exit 0
		;;
	h) echo "$usage" >&2 ;;
	m) file_modes=$OPTARG ;;
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

# CONTROL directory has highest priority
CONTROL=
[ -d "$pkg_dir"/CONTROL ] && CONTROL=CONTROL
if [ -z "$CONTROL" ]; then
	error "Directory $pkg_dir has no CONTROL subdirectory"
fi

if ! pkg_appears_sane "$pkg_dir"; then
	echo >&2
	error "Please fix the above errors and try again"
fi

info "Starting IPK build for $pkg_dir"

tmp_dir=$dest_dir/IPKG_BUILD.$$
mkdir "$tmp_dir"

echo $CONTROL >"$tmp_dir"/tarX
cd "$pkg_dir"

# Reproducible archive options: fixed mtime, numeric owner, sorted entry
# order, GNU format. gzip -n strips the embedded timestamp and filename so
# the archive hash is stable across builds.
$TAR -X "$tmp_dir"/tarX --format=gnu --numeric-owner --sort=name -cpf - --mtime="$TIMESTAMP" . | gzip -n - >"$tmp_dir"/data.tar.gz

installed_size=$(zcat <"$tmp_dir"/data.tar.gz | wc -c)
if grep -q "^Installed-Size:" "$pkg_dir"/$CONTROL/control; then
	sed -i -e "s/^Installed-Size: .*/Installed-Size: $installed_size/" \
		"$pkg_dir"/$CONTROL/control
else
	sed -i -e "/^Description:/i Installed-Size: $installed_size" \
		"$pkg_dir"/$CONTROL/control
fi

(cd "$pkg_dir"/$CONTROL && $TAR --format=gnu --numeric-owner --sort=name -cf - --mtime="$TIMESTAMP" . | gzip -n - >"$tmp_dir"/control.tar.gz)
rm "$tmp_dir"/tarX

# ar-based .ipk format version (2.0 = current deb-based ipk layout)
echo "2.0" >"$tmp_dir"/debian-binary

pkg_file=$dest_dir/${pkg}_${version}_${arch}.ipk
rm -f "$pkg_file"
(cd "$tmp_dir" && $TAR --format=gnu --numeric-owner --sort=name -cf - --mtime="$TIMESTAMP" ./debian-binary ./data.tar.gz ./control.tar.gz | gzip -n - >"$pkg_file")

rm "$tmp_dir"/debian-binary "$tmp_dir"/data.tar.gz "$tmp_dir"/control.tar.gz
rmdir "$tmp_dir"

info "Packaged contents of $pkg_dir into $pkg_file"