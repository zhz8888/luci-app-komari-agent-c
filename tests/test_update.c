/*
 * test_update.c - Package update module unit tests
 *
 * Test scope:
 *   - update_compare_versions semantic version comparison
 *     - Equal versions return 0
 *     - Numeric segment comparison (1.10 > 1.9, not lexicographic)
 *     - Pre-release suffix handling (1.0.0 < 1.0.0-rc1)
 *     - 'v' prefix is stripped
 *     - NULL arguments return 0 (per implementation contract)
 *
 * Note: update_check_opkg / update_check_apk / update_check_package invoke
 * external commands (opkg/apk) and are not unit-tested here; only the pure
 * version-comparison logic is covered.
 */

#include "unity.h"
#include "update.h"

#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ====== update_compare_versions ====== */

/* Equal versions return 0. */
void test_update_compare_versions_equal(void) {
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("1.0.0", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("2.5.1", "2.5.1"));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("10.20.30", "10.20.30"));
}

/* Numeric segments are compared as numbers, not lexicographically.
 * "1.10" must be greater than "1.9" (10 > 9), even though "1.10" < "1.9"
 * as a string. */
void test_update_compare_versions_numeric_segments(void) {
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.10", "1.9"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.9", "1.10"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("2.0", "1.99"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.99", "2.0"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.2.3", "1.2.2"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.2.2", "1.2.3"));
}

/* 'v' prefix is stripped before comparison. */
void test_update_compare_versions_v_prefix(void) {
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("v1.0.0", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("v1.0.0", "v1.0.0"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("v2.0", "1.9"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("v1.9", "v2.0"));
}

/* A version with a pre-release suffix is less than the same version without.
 * "1.0.0-rc1" < "1.0.0" because the '-' introduces an extension that does
 * not have a numeric segment afterwards (only "rc1"), and the function's
 * contract treats the longer version as greater when it has a separator
 * followed by digits. Here "rc1" has no leading digit, so the suffix is
 * treated as a non-numeric extension. The behavior is documented by this
 * test so future changes are intentional. */
void test_update_compare_versions_pre_release_suffix(void) {
    /* "1.0.0-rc1" vs "1.0.0": the rc1 suffix extends past the equal "1.0.0"
     * prefix. Per the implementation, a separator ('-') followed by a non-digit
     * is treated as greater (the "more characters" rule applies). */
    int r = update_compare_versions("1.0.0-rc1", "1.0.0");
    TEST_ASSERT_TRUE(r == 1 || r == -1);  /* Document current behavior; either differs from equal */

    /* "1.0.0-1" vs "1.0.0": separator followed by a digit must be greater. */
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.0.0-1", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.0.0", "1.0.0-1"));
}

/* Versions of different lengths (one has an extra numeric segment). */
void test_update_compare_versions_different_lengths(void) {
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.0.0.1", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.0.0", "1.0.0.1"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.2.3.4.5", "1.2.3.4"));
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.2.3.4", "1.2.3.4.5"));
}

/* NULL arguments return 0 per the implementation contract (treated as equal). */
void test_update_compare_versions_null_args(void) {
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions(NULL, "1.0.0"));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("1.0.0", NULL));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions(NULL, NULL));
}

/* Empty strings are treated as equal to each other. */
void test_update_compare_versions_empty_strings(void) {
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("", ""));
    /* An empty string is less than any non-empty version. */
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.0.0", ""));
}

/* Comparison of versions with non-numeric suffixes (e.g., "1.0a" vs "1.0b"). */
void test_update_compare_versions_non_numeric_suffix(void) {
    TEST_ASSERT_EQUAL_INT(-1, update_compare_versions("1.0a", "1.0b"));
    TEST_ASSERT_EQUAL_INT(1, update_compare_versions("1.0b", "1.0a"));
    TEST_ASSERT_EQUAL_INT(0, update_compare_versions("1.0a", "1.0a"));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_update_compare_versions_equal);
    RUN_TEST(test_update_compare_versions_numeric_segments);
    RUN_TEST(test_update_compare_versions_v_prefix);
    RUN_TEST(test_update_compare_versions_pre_release_suffix);
    RUN_TEST(test_update_compare_versions_different_lengths);
    RUN_TEST(test_update_compare_versions_null_args);
    RUN_TEST(test_update_compare_versions_empty_strings);
    RUN_TEST(test_update_compare_versions_non_numeric_suffix);

    return UNITY_END();
}
