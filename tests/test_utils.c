#include "unity.h"
#include "../src/utils/utils.h"
#include "komari_errno.h"
#include <string.h>
#include <stdlib.h>

void setUp(void) {
}

void tearDown(void) {
}

void test_utils_str_duplicate(void) {
    const char *original = "hello world";
    char *dup = utils_str_duplicate(original);

    TEST_ASSERT_NOT_NULL(dup);
    TEST_ASSERT_EQUAL_STRING(original, dup);

    free(dup);

    char *null_dup = utils_str_duplicate(NULL);
    TEST_ASSERT_NULL(null_dup);
}

void test_utils_str_trim(void) {
    char str1[] = "  hello  ";
    char *result1 = utils_str_trim(str1);
    TEST_ASSERT_EQUAL_STRING("hello", result1);

    char str2[] = "\t\ntest\t\n";
    char *result2 = utils_str_trim(str2);
    TEST_ASSERT_EQUAL_STRING("test", result2);

    char *null_result = utils_str_trim(NULL);
    TEST_ASSERT_NULL(null_result);
}

void test_utils_str_starts_with(void) {
    TEST_ASSERT_TRUE(utils_str_starts_with("hello world", "hello"));
    TEST_ASSERT_TRUE(utils_str_starts_with("hello world", ""));
    TEST_ASSERT_FALSE(utils_str_starts_with("hello world", "world"));
    TEST_ASSERT_FALSE(utils_str_starts_with("", "hello"));

    TEST_ASSERT_FALSE(utils_str_starts_with(NULL, "hello"));
    TEST_ASSERT_FALSE(utils_str_starts_with("hello", NULL));
}

void test_utils_str_ends_with(void) {
    TEST_ASSERT_TRUE(utils_str_ends_with("hello world", "world"));
    TEST_ASSERT_TRUE(utils_str_ends_with("hello world", ""));
    TEST_ASSERT_FALSE(utils_str_ends_with("hello world", "hello"));
    TEST_ASSERT_FALSE(utils_str_ends_with("", "hello"));

    TEST_ASSERT_FALSE(utils_str_ends_with(NULL, "hello"));
    TEST_ASSERT_FALSE(utils_str_ends_with("hello", NULL));
}

void test_utils_json_escape(void) {
    char *escaped = utils_json_escape("hello\"world\\test\nvalue");
    TEST_ASSERT_NOT_NULL(escaped);
    TEST_ASSERT_TRUE(strstr(escaped, "\\\"") != NULL);
    TEST_ASSERT_TRUE(strstr(escaped, "\\\\") != NULL);
    TEST_ASSERT_TRUE(strstr(escaped, "\\n") != NULL);
    free(escaped);

    char *null_escaped = utils_json_escape(NULL);
    TEST_ASSERT_NULL(null_escaped);
}

void test_utils_file_exists(void) {
    TEST_ASSERT_TRUE(utils_file_exists("/"));
    TEST_ASSERT_TRUE(utils_file_exists("/tmp"));
    TEST_ASSERT_FALSE(utils_file_exists("/nonexistent_path_12345"));
    TEST_ASSERT_FALSE(utils_file_exists(NULL));
}

void test_utils_get_current_timestamp(void) {
    uint64_t ts1 = utils_get_current_timestamp();
    TEST_ASSERT_TRUE(ts1 > 0);

    uint64_t ts2 = utils_get_current_timestamp();
    TEST_ASSERT_TRUE(ts2 >= ts1);
}

void test_utils_format_timestamp(void) {
    char buf[64];
    uint64_t ts = 1704067200; // 2024-01-01 00:00:00 UTC

    int result = utils_format_timestamp(ts, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(strlen(buf) > 0);
}

/* komari_strerror must return non-NULL, human-readable strings for every
 * documented error code and a fallback for unknown values. The returned
 * pointers must be static (safe to compare by address). */
void test_utils_komari_strerror(void) {
    TEST_ASSERT_EQUAL_STRING("success", komari_strerror(KOMARI_OK));
    TEST_ASSERT_EQUAL_STRING("invalid argument", komari_strerror(KOMARI_ERR_INVALID_ARG));
    TEST_ASSERT_EQUAL_STRING("not found", komari_strerror(KOMARI_ERR_NOT_FOUND));
    TEST_ASSERT_EQUAL_STRING("buffer too small", komari_strerror(KOMARI_ERR_BUFFER_TOO_SMALL));
    TEST_ASSERT_EQUAL_STRING("parse error", komari_strerror(KOMARI_ERR_PARSE));
    TEST_ASSERT_EQUAL_STRING("network error", komari_strerror(KOMARI_ERR_NETWORK));
    TEST_ASSERT_EQUAL_STRING("out of memory", komari_strerror(KOMARI_ERR_NOMEM));
    TEST_ASSERT_EQUAL_STRING("operation not supported", komari_strerror(KOMARI_ERR_UNSUPPORTED));
    TEST_ASSERT_EQUAL_STRING("unspecified error", komari_strerror(KOMARI_ERR_GENERIC));
    /* Unknown code returns a non-NULL fallback rather than crashing. */
    TEST_ASSERT_NOT_NULL(komari_strerror((komari_errno_t)-999));
    TEST_ASSERT_EQUAL_STRING("unknown error code", komari_strerror((komari_errno_t)-999));
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_utils_str_duplicate);
    RUN_TEST(test_utils_str_trim);
    RUN_TEST(test_utils_str_starts_with);
    RUN_TEST(test_utils_str_ends_with);
    RUN_TEST(test_utils_json_escape);
    RUN_TEST(test_utils_file_exists);
    RUN_TEST(test_utils_get_current_timestamp);
    RUN_TEST(test_utils_format_timestamp);
    RUN_TEST(test_utils_komari_strerror);

    return UNITY_END();
}
