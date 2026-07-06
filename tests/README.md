# Tests: Unity Test Framework

This directory contains the [Unity](https://github.com/ThrowTheSwitch/Unity) test
framework (v2.6.1) plus the unit test suites for komari-agent-c.

## Vendored Unity files

| File | Description |
|------|-------------|
| `unity.c` | Unity framework implementation |
| `unity.h` | Public API header (defines `UNITY_VERSION_*` macros) |
| `unity_internals.h` | Internal macros used by `unity.c` and `unity.h` |

## Version

- **Upstream repository**: https://github.com/ThrowTheSwitch/Unity
- **Version**: 2.6.1
- **Tag**: `v2.6.1`
- **Synced date**: 2026-01
- **Local modifications**: None. The three Unity files are identical to the
  upstream v2.6.1 tag.

## Updating

To sync a new upstream version:

```bash
git clone --branch v2.6.1 --depth 1 https://github.com/ThrowTheSwitch/Unity /tmp/unity
cp /tmp/unity/unity.c /tmp/unity/unity.h /tmp/unity/unity_internals.h tests/
git diff tests/unity.c tests/unity.h tests/unity_internals.h
# Update the version in this README, tests/CMakeLists.txt (comment), and CLAUDE.md
```

After updating, verify the version macros in `unity.h` match the documented
version:

```bash
grep UNITY_VERSION_MAJOR tests/unity.h
```

## Test suites

The remaining `.c` files in this directory are the project's own test suites
(`test_utils.c`, `test_config.c`, `test_monitoring.c`, etc.). They are not
part of Unity and are licensed under the project's MIT License.

## License

Unity is licensed under the MIT License. See the copyright header in each
Unity file for details.
