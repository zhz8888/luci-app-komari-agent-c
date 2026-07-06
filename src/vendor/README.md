# Vendor: cJSON

This directory contains a vendored copy of [cJSON](https://github.com/DaveGamble/cJSON),
used by the agent for JSON parsing and serialization.

## Version

- **Upstream repository**: https://github.com/DaveGamble/cJSON
- **Version**: 1.7.19
- **Tag**: `v1.7.19`
- **Synced date**: 2026-01
- **Local modifications**: None. The files are identical to the upstream tag.

## Files

| File | Description |
|------|-------------|
| `cJSON.c` | Parser and serializer implementation |
| `cJSON.h` | Public API header (defines `CJSON_VERSION_MAJOR/MINOR/PATCH`) |

## Updating

To sync a new upstream version:

```bash
# Clone upstream at a specific tag
git clone --branch v1.7.19 --depth 1 https://github.com/DaveGamble/cJSON /tmp/cjson
# Copy the two files into this directory
cp /tmp/cjson/cJSON.c /tmp/cjson/cJSON.h src/vendor/
# Verify no local patches are lost (there should be none)
git diff src/vendor/
# Update the version number in this README and in CLAUDE.md
```

## Security

cJSON has had historical CVEs (e.g., CVE-2023-32694, CVE-2024-32255). When
upgrading, consult the upstream changelog and verify the new version includes
all relevant security fixes. The current version (1.7.19) was released in
2023 and includes fixes for known issues at that time.

## License

cJSON is licensed under the MIT License. See the copyright header in each
file for details.
