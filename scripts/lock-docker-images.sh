#!/usr/bin/env bash
# Query Docker Hub API for the current digest of the base images used by
# Dockerfile.build (ubuntu:24.04) and Dockerfile.legacy (debian:bookworm-slim),
# and print the docker build commands with --build-arg BASE_IMAGE=<image>@sha256:<digest>.
#
# Usage:
#   ./scripts/lock-docker-images.sh          # print commands
#   ./scripts/lock-docker-images.sh --apply  # rewrite Dockerfile ARG defaults in place
#
# Requirements: curl, jq (optional — falls back to grep/sed if jq is missing)
#
# Copyright (C) 2026 zhz8888/luci-app-komari-agent-c Contributors
# Licensed under MIT License

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DOCKERFILE_BUILD="${PROJECT_ROOT}/docker/Dockerfile.build"
DOCKERFILE_LEGACY="${PROJECT_ROOT}/docker/Dockerfile.legacy"

# Base images to query
IMAGE_BUILD="ubuntu:24.04"
IMAGE_LEGACY="debian:bookworm-slim"

# Fetch the multi-arch manifest digest for a repository:tag from Docker Hub.
# Docker Hub serves the registry v2 manifest list at:
#   https://hub.docker.com/v2/repositories/<namespace>/<repo>/tags/<tag>
# The digest field in the JSON response is the manifest list digest.
fetch_digest() {
    local image="$1"
    local namespace repo tag

    # Split image into namespace/repo:tag
    if [[ "$image" == *":"* ]]; then
        tag="${image##*:}"
        local ns_repo="${image%:*}"
    else
        tag="latest"
        ns_repo="$image"
    fi

    if [[ "$ns_repo" == *"/"* ]]; then
        namespace="${ns_repo%%/*}"
        repo="${ns_repo#*/}"
    else
        namespace="library"
        repo="$ns_repo"
    fi

    local url="https://hub.docker.com/v2/repositories/${namespace}/${repo}/tags/${tag}"
    local json digest

    json="$(curl -fsSL "$url")" || {
        echo "ERROR: failed to fetch $url" >&2
        return 1
    }

    # Prefer jq if available, otherwise fall back to grep/sed
    if command -v jq >/dev/null 2>&1; then
        digest="$(echo "$json" | jq -r '.digest // empty')"
    else
        digest="$(echo "$json" | grep -o '"digest":"[^"]*"' | head -1 | sed 's/"digest":"//;s/"//')"
    fi

    if [ -z "$digest" ]; then
        echo "ERROR: no digest found for $image" >&2
        return 1
    fi

    echo "$digest"
}

# Print the locked build command for an image
print_locked_command() {
    local image="$1" digest="$2" dockerfile="$3"
    echo "# ${dockerfile##*/} (${image})"
    echo "docker build --build-arg BASE_IMAGE=${image}@${digest} -f ${dockerfile} ..."
    echo
}

# Rewrite the ARG BASE_IMAGE default in a Dockerfile to use digest
apply_digest_to_dockerfile() {
    local image="$1" digest="$2" dockerfile="$3"
    local locked="${image}@${digest}"

    # Replace: ARG BASE_IMAGE=<anything>  →  ARG BASE_IMAGE=<image>@<digest>
    sed -i.bak "s|^ARG BASE_IMAGE=.*|ARG BASE_IMAGE=${locked}|" "$dockerfile"
    rm -f "${dockerfile}.bak"
    echo "Updated ${dockerfile##*/}: ARG BASE_IMAGE=${locked}"
}

main() {
    local apply=false
    if [ "${1:-}" = "--apply" ]; then
        apply=true
    fi

    echo "Querying Docker Hub for current base image digests..."
    echo

    local digest_build digest_legacy
    digest_build="$(fetch_digest "$IMAGE_BUILD")" || exit 1
    digest_legacy="$(fetch_digest "$IMAGE_LEGACY")" || exit 1

    echo "Found digests:"
    echo "  ${IMAGE_BUILD}  → ${digest_build}"
    echo "  ${IMAGE_LEGACY}  → ${digest_legacy}"
    echo

    if $apply; then
        apply_digest_to_dockerfile "$IMAGE_BUILD" "$digest_build" "$DOCKERFILE_BUILD"
        apply_digest_to_dockerfile "$IMAGE_LEGACY" "$digest_legacy" "$DOCKERFILE_LEGACY"
        echo
        echo "Done. Review the changes with: git diff docker/Dockerfile.build docker/Dockerfile.legacy"
    else
        echo "To use these digests, pass --build-arg BASE_IMAGE=... to docker build:"
        echo
        print_locked_command "$IMAGE_BUILD" "$digest_build" "$DOCKERFILE_BUILD"
        print_locked_command "$IMAGE_LEGACY" "$digest_legacy" "$DOCKERFILE_LEGACY"
        echo
        echo "To rewrite the Dockerfile ARG defaults in place, run:"
        echo "  $0 --apply"
    fi
}

main "$@"
