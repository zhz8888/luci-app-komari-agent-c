#!/usr/bin/env bash
# Generate release notes for GitHub Releases from Conventional Commits.
# Usage: generate-release-notes.sh <version> <repository>
#   version:    semver string (e.g., 1.0.1)
#   repository: GitHub repository (e.g., owner/repo)
#
# Dependencies: git, running in a git repository with tag history.

set -euo pipefail

VERSION="$1"
REPOSITORY="$2"

# ---- Find previous tag ----
CURRENT_TAG="v${VERSION}"
PREVIOUS_TAG=""
while IFS= read -r tag; do
    if [ "$tag" = "$CURRENT_TAG" ]; then
        break
    fi
    PREVIOUS_TAG="$tag"
done < <(git tag --sort=-version:refname 2>/dev/null || echo "")

if [ -n "$PREVIOUS_TAG" ]; then
    LOG_RANGE="${PREVIOUS_TAG}..HEAD"
    COMPARE_URL="https://github.com/${REPOSITORY}/compare/${PREVIOUS_TAG}...${CURRENT_TAG}"
else
    LOG_RANGE="HEAD"
    COMPARE_URL=""
fi

# ---- Fetch commits ----
COMMITS=$(git log --no-merges "${LOG_RANGE}" --format="%s" 2>/dev/null || true)

# ---- Categorize commits ----
CAT_FEAT=""; CAT_FIX=""; CAT_PERF=""
CAT_REFACTOR=""; CAT_DOCS=""; CAT_TEST=""
CAT_BUILD=""; CAT_CI=""; CAT_CHORE=""
CAT_OTHER=""

while IFS= read -r line; do
    [ -z "$line" ] && continue
    bullet="  - ${line}"
    case "$line" in
        feat*)  CAT_FEAT="${CAT_FEAT}${bullet}"$'\n' ;;
        fix*)   CAT_FIX="${CAT_FIX}${bullet}"$'\n' ;;
        perf*)  CAT_PERF="${CAT_PERF}${bullet}"$'\n' ;;
        refactor*) CAT_REFACTOR="${CAT_REFACTOR}${bullet}"$'\n' ;;
        docs*)  CAT_DOCS="${CAT_DOCS}${bullet}"$'\n' ;;
        test*)  CAT_TEST="${CAT_TEST}${bullet}"$'\n' ;;
        build*) CAT_BUILD="${CAT_BUILD}${bullet}"$'\n' ;;
        ci*)    CAT_CI="${CAT_CI}${bullet}"$'\n' ;;
        chore*) CAT_CHORE="${CAT_CHORE}${bullet}"$'\n' ;;
        style*) ;;    # skip trivial style commits
        revert*) ;;   # skip reverts (noise)
        *)      CAT_OTHER="${CAT_OTHER}${bullet}"$'\n' ;;
    esac
done <<< "$COMMITS"

# ---- Helper: print a section if non-empty ----
print_section() {
    local heading="$1"
    local content="$2"
    if [ -n "$content" ]; then
        # Trim trailing newline
        content="${content%"${content##*[![:space:]]}"}"
        echo ""
        echo "### ${heading}"
        echo "${content}"
    fi
}

# ---- Generate release notes ----
{
    echo "# Komari Agent v${VERSION}"
    echo ""

    # Quick Installation section
    echo "## 快速安装"
    echo ""
    echo "### OpenWrt 系统（推荐）"
    echo ""
    echo '```bash'
    echo "# OpenWrt >= 25.12（使用 APK）"
    echo "apk add komari-agent-c-${VERSION}-1-<arch>.apk"
    echo ""
    echo "# OpenWrt 24.10（使用 APK）"
    echo "apk add komari-agent-c-${VERSION}-1-<arch>.apk"
    echo ""
    echo "# OpenWrt < 24.10（使用 IPK）"
    echo "opkg install komari-agent-c_${VERSION}-1_<arch>.ipk"
    echo '```'
    echo ""
    echo "安装 LuCI 前端界面（可选）："
    echo ""
    echo '```bash'
    echo "# APK（OpenWrt >= 24.10）"
    echo "apk add luci-app-komari-agent-c-${VERSION}-1-<arch>.apk"
    echo ""
    echo "# IPK（OpenWrt < 24.10）"
    echo "opkg install luci-app-komari-agent-c_${VERSION}-1_<arch>.ipk"
    echo '```'
    echo ""
    echo "### 其他 Linux 系统"
    echo ""
    echo '```bash'
    echo "wget https://github.com/${REPOSITORY}/releases/download/v${VERSION}/komari-agent-c-${VERSION}-linux-<arch>.tar.gz"
    echo "tar -xzf komari-agent-c-${VERSION}-linux-<arch>.tar.gz"
    echo '```'
    echo ""

    # Changelog section
    echo "## 更新日志"
    echo ""

    print_section "新功能" "${CAT_FEAT}"
    print_section "Bug 修复" "${CAT_FIX}"
    print_section "性能优化" "${CAT_PERF}"
    print_section "代码重构" "${CAT_REFACTOR}"
    print_section "文档" "${CAT_DOCS}"
    print_section "测试" "${CAT_TEST}"

    # Merge build+ci+chore into Maintenance
    CAT_MAINT=""
    [ -n "$CAT_BUILD" ] && CAT_MAINT="${CAT_MAINT}${CAT_BUILD}"
    [ -n "$CAT_CI" ]    && CAT_MAINT="${CAT_MAINT}${CAT_CI}"
    [ -n "$CAT_CHORE" ] && CAT_MAINT="${CAT_MAINT}${CAT_CHORE}"
    # Trim trailing newline
    CAT_MAINT="${CAT_MAINT%"${CAT_MAINT##*[![:space:]]}"}"
    print_section "维护" "${CAT_MAINT}"

    print_section "其他变更" "${CAT_OTHER}"

    # Comparison link
    if [ -n "$COMPARE_URL" ]; then
        echo ""
        echo "**完整变更日志**: [${PREVIOUS_TAG}...${CURRENT_TAG}](${COMPARE_URL})"
    fi

} > release_notes.md

echo "发布说明已生成: release_notes.md"
echo "  版本: ${VERSION}"
echo "  上个标签: ${PREVIOUS_TAG:-无}"
echo "  分析提交数: $(echo "${COMMITS}" | wc -l)"
