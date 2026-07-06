#!/usr/bin/env python3
"""
CI Configuration Verification Script

Validates GitHub Actions workflow files, Docker infrastructure,
and project structural integrity for komari-agent-c project.

Usage:
    python scripts/verify_ci_config.py yaml        # YAML syntax check only
    python scripts/verify_ci_config.py config      # CI config check only
    python scripts/verify_ci_config.py all         # All checks (default)
    python scripts/verify_ci_config.py --help      # Show help

Returns:
    0 - All checks passed
    1 - One or more checks failed
"""

import argparse
import json
import logging
import os
import re
import sys
import traceback
from datetime import datetime
from pathlib import Path

try:
    import yaml
except ImportError:
    print("ERROR: PyYAML is required. Install with: pip install pyyaml")
    sys.exit(1)


class CIConfigValidator:
    """Validates CI configuration files and project structure."""

    def __init__(self, repo_root=None, verbose=False, log_file=None):
        self.repo_root = Path(repo_root or Path(__file__).resolve().parent.parent)
        self.passed = 0
        self.failed = 0
        self.errors = []
        self._setup_logging(verbose, log_file)

    def _setup_logging(self, verbose, log_file):
        """Configure logging with console and optional file output."""
        self.logger = logging.getLogger("ci-verify")
        self.logger.setLevel(logging.DEBUG if verbose else logging.INFO)
        fmt = logging.Formatter(
            "%(asctime)s [%(levelname)s] %(message)s",
            datefmt="%Y-%m-%d %H:%M:%S"
        )

        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(logging.DEBUG if verbose else logging.INFO)
        ch.setFormatter(fmt)
        self.logger.addHandler(ch)

        if log_file:
            fh = logging.FileHandler(log_file, encoding="utf-8")
            fh.setLevel(logging.DEBUG)
            fh.setFormatter(fmt)
            self.logger.addHandler(fh)

    # ------------------------------------------------------------------
    # Core check helpers
    # ------------------------------------------------------------------

    def check(self, name, condition, detail=""):
        """Record a check result with pass/fail and optional detail."""
        if condition:
            self.logger.info("  PASS: %s", name)
            self.passed += 1
        else:
            msg = f"FAIL: {name}"
            if detail:
                msg += f" - {detail}"
            self.logger.error("  %s", msg)
            self.failed += 1
            self.errors.append(msg)

    def _read_file(self, rel_path):
        """Read file content, returning (content, error)."""
        path = self.repo_root / rel_path
        if not path.exists():
            return None, f"File not found: {rel_path}"
        try:
            return path.read_text(encoding="utf-8"), None
        except Exception as e:
            return None, f"Error reading {rel_path}: {e}"

    def _read_yaml(self, rel_path):
        """Read and parse a YAML file, returning (data, error)."""
        content, err = self._read_file(rel_path)
        if err:
            return None, err
        try:
            data = yaml.safe_load(content)
            if data is None:
                return None, f"Empty YAML file: {rel_path}"
            return data, None
        except yaml.YAMLError as e:
            detail = self._format_yaml_error(e)
            return None, f"YAML error in {rel_path}: {detail}"

    def _format_yaml_error(self, error):
        """Extract readable location info from a YAMLError."""
        if hasattr(error, 'problem_mark'):
            mark = error.problem_mark
            return (
                f"Line {mark.line + 1}, Column {mark.column + 1}: {error.problem}"
                + (f" (context: {error.context})" if error.context else "")
            )
        return str(error)

    # ------------------------------------------------------------------
    # 1. YAML Syntax Checks
    # ------------------------------------------------------------------

    def check_yaml_syntax(self):
        """Strict YAML syntax validation with error location."""
        self.logger.info("--- YAML Syntax Check ---")

        yaml_files = [
            ".github/workflows/ci.yml",
            ".github/workflows/release.yml",
            "docker/docker-compose.yml",
        ]

        for rel_path in yaml_files:
            path = self.repo_root / rel_path
            if not path.exists():
                self.check(f"{rel_path}: file exists", False)
                continue

            # Check 1a: Basic YAML parsing
            content, r_err = self._read_file(rel_path)
            if r_err:
                self.check(f"{rel_path}: readable", False, r_err)
                continue

            parsed = None
            try:
                parsed = yaml.safe_load(content)
                if parsed is None:
                    self.check(f"{rel_path}: valid YAML syntax", False, "File is empty")
                    continue
                self.check(f"{rel_path}: valid YAML syntax", True)
            except yaml.YAMLError as e:
                self.check(f"{rel_path}: valid YAML syntax", False,
                           self._format_yaml_error(e))
                continue

            # Check 1b: Top-level must be a mapping
            self.check(
                f"{rel_path}: top-level is mapping",
                isinstance(parsed, dict),
                f"got {type(parsed).__name__}"
            )

            # Check 1c: No null/None values at expected structure points
            # (exclude workflow_dispatch which is valid as null in GitHub Actions)
            null_issues = self._find_null_values(parsed)
            null_issues = [i for i in null_issues if "workflow_dispatch" not in i]
            self.check(
                f"{rel_path}: no unexpected null values",
                len(null_issues) == 0,
                "; ".join(null_issues[:5]) if null_issues else ""
            )

            # Check 1d: Strict structure rules for workflow files
            if "ci.yml" in rel_path or "release.yml" in rel_path:
                self._check_workflow_strict(rel_path, content, parsed)

            # Check 1e: Verify action version pinning
            if "ci.yml" in rel_path or "release.yml" in rel_path:
                self._check_action_version_pins(rel_path, content)

        self.logger.info("")

    def _find_null_values(self, obj, path=""):
        """Walk structure and report paths with null values."""
        issues = []
        if obj is None:
            issues.append(f"null value at '{path or 'root'}'")
        elif isinstance(obj, dict):
            for k, v in obj.items():
                fp = f"{path}.{k}" if path else k
                issues.extend(self._find_null_values(v, fp))
        elif isinstance(obj, list):
            for i, v in enumerate(obj):
                fp = f"{path}[{i}]"
                issues.extend(self._find_null_values(v, fp))
        return issues

    def _check_workflow_strict(self, rel_path, content, parsed):
        """Apply strict structural rules to GitHub Actions workflow files."""
        # All jobs must have a 'name' or the step must be identifiable
        jobs = parsed.get("jobs", {})
        if not isinstance(jobs, dict):
            return

        for job_id, job_def in jobs.items():
            if not isinstance(job_def, dict):
                continue
            steps = job_def.get("steps", [])
            if not isinstance(steps, list):
                continue
            for idx, step in enumerate(steps):
                if not isinstance(step, dict):
                    continue
                # Each step should have a 'name' or be a simple 'uses'
                if "name" not in step and "uses" not in step:
                    self.check(
                        f"{rel_path}: job '{job_id}' step {idx} has name or uses",
                        False, "Step must have 'name' or 'uses'"
                    )
                # If step uses an action, it must have a version pin
                if "uses" in step:
                    uses_val = step.get("uses", "")
                    # docker:// references are exempt
                    if not uses_val.startswith("docker://"):
                        if "@" not in uses_val:
                            self.check(
                                f"{rel_path}: job '{job_id}' step {idx} "
                                f"pins action version",
                                False, f"'{uses_val}' missing @version tag"
                            )

                # Run steps should have a name
                if "run" in step and "name" not in step:
                    self.check(
                        f"{rel_path}: job '{job_id}' step {idx} has name",
                        False, "Shell step missing 'name'"
                    )

    def _check_action_version_pins(self, rel_path, content):
        """Warn about actions using @main/@master/@latest instead of pinned tags."""
        issues = []
        for m in re.finditer(r'uses:\s+(\S+?)(@\S+)', content):
            action = m.group(1)
            version = m.group(2)
            if version in ("@main", "@master", "@latest"):
                issues.append(f"{action}{version}")
        self.check(
            f"{rel_path}: all actions use pinned versions",
            len(issues) == 0,
            f"unpinned: {', '.join(issues)}" if issues else ""
        )

    # ------------------------------------------------------------------
    # 2. CI Config Checks
    # ------------------------------------------------------------------

    def check_ci_config(self):
        """Comprehensive CI configuration validation."""
        self._check_job_dependencies()
        self._check_docker_infrastructure()
        self._check_build_patterns()
        self._check_cross_compilation()
        self._check_cmake_config()
        self._check_openwrt_config()
        self._check_version_consistency()

    def _check_job_dependencies(self):
        """Verify workflow job dependency chains."""
        self.logger.info("--- Job Dependencies Check ---")

        ci_data, err = self._read_yaml(".github/workflows/ci.yml")
        if not err:
            jobs = ci_data.get("jobs", {})

            # test-openwrt-build must depend on test-binary-build
            towb = jobs.get("test-openwrt-build", {})
            self.check(
                "ci.yml: test-openwrt-build needs test-binary-build",
                towb.get("needs") == "test-binary-build",
                f"got: {towb.get('needs')}"
            )
            self.check(
                "ci.yml: test-openwrt-build has if: always()",
                towb.get("if") == "always()",
                f"got: {towb.get('if')}"
            )

            # test-luci-build must depend on test-binary-build
            tlb = jobs.get("test-luci-build", {})
            self.check(
                "ci.yml: test-luci-build needs test-binary-build",
                tlb.get("needs") == "test-binary-build",
                f"got: {tlb.get('needs')}"
            )
            self.check(
                "ci.yml: test-luci-build has if: always()",
                tlb.get("if") == "always()",
                f"got: {tlb.get('if')}"
            )

            # lint must be independent
            lint = jobs.get("lint", {})
            self.check(
                "ci.yml: lint has no needs (independent)",
                "needs" not in lint,
                f"got: {lint.get('needs')}"
            )

        release_data, err = self._read_yaml(".github/workflows/release.yml")
        if not err:
            jobs = release_data.get("jobs", {})
            release_job = jobs.get("release", {})
            release_needs = release_job.get("needs", [])
            expected = ["build-openwrt", "build-binaries", "build-luci"]
            self.check(
                "release.yml: release needs all build jobs",
                release_needs == expected,
                f"got: {release_needs}"
            )

        self.logger.info("")

    def _check_docker_infrastructure(self):
        """Verify Docker infrastructure files and compose services."""
        self.logger.info("--- Docker Infrastructure Check ---")

        required_files = [
            "docker/Dockerfile.build",
            "docker/Dockerfile.legacy",
            "docker/build.sh",
            "docker/test.sh",
            "docker/docker-compose.yml",
            "scripts/docker-build.sh",
            ".dockerignore",
        ]
        for rel_path in required_files:
            path = self.repo_root / rel_path
            self.check(f"{rel_path}: exists", path.exists())

        exec_files = ["docker/build.sh", "docker/test.sh", "scripts/docker-build.sh"]
        for rel_path in exec_files:
            path = self.repo_root / rel_path
            exists = path.exists()
            executable = exists and os.access(path, os.X_OK)
            self.check(f"{rel_path}: executable", executable)

        dc_data, err = self._read_yaml("docker/docker-compose.yml")
        if not err:
            services = dc_data.get("services", {})
            required_services = [
                "build-amd64", "build-arm64", "build-arm", "build-armv7",
                "build-mipsel", "build-mips64", "build-riscv64", "build-386",
                "test",
            ]
            missing = [s for s in required_services if s not in services]
            self.check(
                "docker-compose.yml: all 9 services defined",
                len(missing) == 0,
                f"missing: {missing}" if missing else ""
            )

        self.logger.info("")

    def _check_build_patterns(self):
        """Verify build patterns in workflow files."""
        self.logger.info("--- Build Pattern Checks ---")

        for workflow in ("ci.yml", "release.yml"):
            path = self.repo_root / ".github/workflows" / workflow
            if not path.exists():
                continue
            content = path.read_text(encoding="utf-8")

            self.check(
                f"{workflow}: Docker compose for binary builds",
                "docker compose -f docker/docker-compose.yml run --rm build-" in content,
            )
            self.check(
                f"{workflow}: no host-system cmake --build",
                "cmake --build" not in content,
            )

        ci_path = self.repo_root / ".github/workflows/ci.yml"
        if ci_path.exists():
            content = ci_path.read_text(encoding="utf-8")
            self.check(
                "ci.yml: lint runs test in Docker",
                "docker compose -f docker/docker-compose.yml run --rm test" in content,
            )

        rel_path = self.repo_root / ".github/workflows/release.yml"
        if rel_path.exists():
            content = rel_path.read_text(encoding="utf-8")
            self.check(
                "release.yml: no host cross-compiler install",
                not re.search(
                    r'sudo apt-get install.*gcc-(.*-linux-gnu|mingw)',
                    content
                ),
            )

        self.logger.info("")

    def _check_cross_compilation(self):
        """Verify cross-compilation settings in docker/build.sh."""
        self.logger.info("--- Cross-Compilation Config Check ---")

        content, err = self._read_file("docker/build.sh")
        if err:
            self.logger.warning("  Skipping cross-compilation checks: %s", err)
            self.logger.info("")
            return

        checks = {
            "CMAKE_SYSTEM_NAME=Linux": "CMAKE_SYSTEM_NAME=Linux" in content,
            "CMAKE_LIBRARY_ARCHITECTURE": "CMAKE_LIBRARY_ARCHITECTURE" in content,
            "PKG_CONFIG_LIBDIR": "PKG_CONFIG_LIBDIR" in content,
            "CC compiler existence check": 'command -v "$CC"' in content,
            "binary architecture verification": "Verify binary architecture" in content,
            "KOMARI_BUILD_PROFILE=binary": "KOMARI_BUILD_PROFILE=binary" in content,
            "KOMARI_BUILD_TESTS=OFF": "KOMARI_BUILD_TESTS=OFF" in content,
        }
        for name, ok in checks.items():
            self.check(f"docker/build.sh: {name}", ok)

        self.logger.info("")

    def _check_cmake_config(self):
        """Validate CMake configuration files and presets."""
        self.logger.info("--- CMake Configuration Check ---")

        # Module files
        cmake_modules = [
            "cmake/BuildOptions.cmake",
            "cmake/Version.cmake",
            "cmake/Platform.cmake",
            "cmake/CompilerFlags.cmake",
            "cmake/Dependencies.cmake",
            "cmake/toolchain-openwrt.cmake",
        ]
        for rel_path in cmake_modules:
            self.check(f"{rel_path}: exists",
                       (self.repo_root / rel_path).exists())

        # CMakePresets.json
        presets_path = self.repo_root / "CMakePresets.json"
        if presets_path.exists():
            try:
                with open(presets_path, "r", encoding="utf-8") as f:
                    presets = json.load(f)

                configure_presets = presets.get("configurePresets", [])
                preset_names = {cp["name"] for cp in configure_presets}
                required = {"default", "debug", "release", "openwrt",
                            "sanitize", "coverage"}
                missing = required - preset_names
                self.check(
                    "CMakePresets.json: required presets present",
                    len(missing) == 0,
                    f"missing: {missing}" if missing else ""
                )

                for cp in configure_presets:
                    if cp.get("name") == "openwrt":
                        cv = cp.get("cacheVariables", {})
                        self.check(
                            "CMakePresets.json: openwrt profile",
                            cv.get("KOMARI_BUILD_PROFILE") == "openwrt",
                        )
                        self.check(
                            "CMakePresets.json: openwrt binaryDir",
                            "build-openwrt" in cp.get("binaryDir", ""),
                        )
                        break
            except (json.JSONDecodeError, KeyError) as e:
                self.check("CMakePresets.json: valid JSON", False, str(e))
        else:
            self.check("CMakePresets.json: exists", False)

        # CMakeLists.txt references
        content, err = self._read_file("CMakeLists.txt")
        if not err:
            refs = {
                "includes BuildOptions": "include(BuildOptions)" in content,
                "includes CompilerFlags": "include(CompilerFlags)" in content,
                "includes Dependencies": "include(Dependencies)" in content,
                "references KOMARI_BUILD_PROFILE": "KOMARI_BUILD_PROFILE" in content,
            }
            for name, ok in refs.items():
                self.check(f"CMakeLists.txt: {name}", ok)

        self.logger.info("")

    def _check_openwrt_config(self):
        """Verify OpenWrt configuration files."""
        self.logger.info("--- OpenWrt Configuration Check ---")

        # OpenWrt Makefile
        content, err = self._read_file("openwrt/Makefile")
        if not err:
            refs = {
                "uses KOMARI_BUILD_SUBDIR": "KOMARI_BUILD_SUBDIR" in content,
                "sets KOMARI_BUILD_PROFILE": "KOMARI_BUILD_PROFILE" in content,
                "uses build-openwrt directory": "build-openwrt" in content,
                "defines Package/komari-agent-c": "Package/komari-agent-c" in content,
            }
            for name, ok in refs.items():
                self.check(f"openwrt/Makefile: {name}", ok)

        # Required files
        for rel_path in ("openwrt/files/komari-agent-c.config",
                         "openwrt/files/komari-agent-c.init"):
            self.check(f"{rel_path}: exists",
                       (self.repo_root / rel_path).exists())

        # PKG_SOURCE removal sed in workflows
        for workflow in ("ci.yml", "release.yml"):
            content, err = self._read_file(f".github/workflows/{workflow}")
            if not err:
                self.check(
                    f"{workflow}: PKG_SOURCE removal sed",
                    "PKG_SOURCE_PROTO:/d" in content,
                )

        self.logger.info("")

    def _check_version_consistency(self):
        """Verify version numbers are consistent across version.h, openwrt/Makefile, and luci/Makefile."""
        self.logger.info("--- Version Consistency Check ---")

        versions = {}

        # 1. include/komari-agent-c/version.h
        version_h, err = self._read_file("include/komari-agent-c/version.h")
        if not err:
            match = re.search(
                r'#define\s+KOMARI_AGENT_C_VERSION_STRING\s+"([^"]+)"',
                version_h,
            )
            if match:
                versions["version.h"] = match.group(1)
                self.check(
                    "version.h: KOMARI_AGENT_C_VERSION_STRING is defined",
                    True,
                )
            else:
                self.check(
                    "version.h: KOMARI_AGENT_C_VERSION_STRING is defined",
                    False,
                )
        else:
            self.check("version.h: file exists", False)

        # 2. openwrt/Makefile
        openwrt_mk, err = self._read_file("openwrt/Makefile")
        if not err:
            match = re.search(r"^PKG_VERSION:=(\S+)", openwrt_mk, re.MULTILINE)
            if match:
                versions["openwrt/Makefile"] = match.group(1)
                self.check(
                    "openwrt/Makefile: PKG_VERSION is defined",
                    True,
                )
            else:
                self.check(
                    "openwrt/Makefile: PKG_VERSION is defined",
                    False,
                )

            # Also check PKG_SOURCE_VERSION matches v<version>
            match_src = re.search(
                r"^PKG_SOURCE_VERSION:=(\S+)", openwrt_mk, re.MULTILINE
            )
            if match_src and "openwrt/Makefile" in versions:
                expected = f"v{versions['openwrt/Makefile']}"
                self.check(
                    f"openwrt/Makefile: PKG_SOURCE_VERSION matches v<version> "
                    f"(expected {expected}, got {match_src.group(1)})",
                    match_src.group(1) == expected,
                )
        else:
            self.check("openwrt/Makefile: file exists", False)

        # 3. luci/Makefile
        luci_mk, err = self._read_file("luci/Makefile")
        if not err:
            match = re.search(r"^PKG_VERSION:=(\S+)", luci_mk, re.MULTILINE)
            if match:
                versions["luci/Makefile"] = match.group(1)
                self.check(
                    "luci/Makefile: PKG_VERSION is defined",
                    True,
                )
            else:
                self.check(
                    "luci/Makefile: PKG_VERSION is defined",
                    False,
                )
        else:
            self.check("luci/Makefile: file exists", False)

        # 4. Cross-check consistency
        if len(versions) >= 2:
            unique_versions = set(versions.values())
            self.check(
                f"Version consistency across {', '.join(sorted(versions.keys()))} "
                f"(all should be equal: {versions})",
                len(unique_versions) == 1,
            )

        self.logger.info("")

    # ------------------------------------------------------------------
    # Run all checks
    # ------------------------------------------------------------------

    def run_yaml_checks(self):
        """Run only YAML syntax checks."""
        self.logger.info("=== CI YAML Syntax Verification ===")
        self.check_yaml_syntax()
        return self._summary()

    def run_config_checks(self):
        """Run only CI config checks."""
        self.logger.info("=== CI Configuration Verification ===")
        self.check_ci_config()
        return self._summary()

    def run_all(self):
        """Run all checks."""
        self.logger.info("=== CI Configuration Verification ===")
        self.logger.info("Started at: %s", datetime.now().isoformat())
        self.logger.info("Repository: %s", self.repo_root)
        self.logger.info("")

        self.check_yaml_syntax()
        self.check_ci_config()

        return self._summary()

    def _summary(self):
        """Print summary and return exit code."""
        elapsed = datetime.now()
        self.logger.info("=== Summary ===")
        self.logger.info("Passed: %d", self.passed)
        self.logger.info("Failed: %d", self.failed)
        if self.failed > 0:
            self.logger.info("Failed checks:")
            for err in self.errors:
                self.logger.info("  - %s", err)
        self.logger.info("Result: %s", "ALL PASSED" if self.failed == 0 else "SOME CHECKS FAILED")
        return 0 if self.failed == 0 else 1


def main():
    parser = argparse.ArgumentParser(
        description="Verify CI configuration for komari-agent-c project"
    )
    parser.add_argument(
        "mode",
        nargs="?",
        choices=["yaml", "config", "all"],
        default="all",
        help="Check mode: yaml (syntax only), config (CI rules only), all (default)"
    )
    parser.add_argument(
        "--repo-root",
        default=None,
        help="Repository root (default: parent of script directory)"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Enable verbose logging"
    )
    parser.add_argument(
        "--log-file",
        default=None,
        help="Write detailed log to file"
    )

    args = parser.parse_args()

    validator = CIConfigValidator(
        repo_root=args.repo_root,
        verbose=args.verbose,
        log_file=args.log_file,
    )

    try:
        if args.mode == "yaml":
            exit_code = validator.run_yaml_checks()
        elif args.mode == "config":
            exit_code = validator.run_config_checks()
        else:
            exit_code = validator.run_all()
    except Exception as e:
        print(f"FATAL: Unhandled exception: {e}", file=sys.stderr)
        traceback.print_exc(file=sys.stderr)
        exit_code = 1

    sys.exit(exit_code)


if __name__ == "__main__":
    main()
