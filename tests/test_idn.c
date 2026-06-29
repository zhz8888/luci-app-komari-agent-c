/*
 * test_idn.c - Internationalized Domain Name (IDN) conversion unit tests.
 *
 * Test coverage:
 *   - idn_contains_non_ascii: non-ASCII character detection
 *   - idn_convert_host_to_ascii: hostname conversion (label-by-label per RFC 3490/3491)
 *   - idn_convert_url_to_ascii: URL conversion
 *   - idn_punycode_encode: Punycode encoding (RFC 3492)
 *
 * Reference vectors (RFC 3492 / IDNA2003):
 *   "中文"      -> "fiq228c"
 *   "münchen"   -> "mnchen-3ya"
 *   "中文.com"  -> "xn--fiq228c.com"
 *   "münchen.de"-> "xn--mnchen-3ya.de"
 */

#include "unity.h"
#include "idn.h"

#include <string.h>
#include <stdlib.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ====== idn_contains_non_ascii tests ====== */

/* Test pure ASCII strings: should return 0 */
void test_idn_contains_non_ascii_pure_ascii(void) {
    TEST_ASSERT_EQUAL_INT(0, idn_contains_non_ascii("example.com"));
    TEST_ASSERT_EQUAL_INT(0, idn_contains_non_ascii("192.168.1.1"));
    TEST_ASSERT_EQUAL_INT(0, idn_contains_non_ascii("hello-world"));
    TEST_ASSERT_EQUAL_INT(0, idn_contains_non_ascii(""));
}

/* Test strings containing non-ASCII characters: should return 1 */
void test_idn_contains_non_ascii_with_unicode(void) {
    /* "中文.com" contains Chinese characters */
    TEST_ASSERT_EQUAL_INT(1, idn_contains_non_ascii("中文.com"));
    /* "例え.jp" contains Japanese characters */
    TEST_ASSERT_EQUAL_INT(1, idn_contains_non_ascii("例え.jp"));
}

/* Test NULL input: should return 0 */
void test_idn_contains_non_ascii_null(void) {
    TEST_ASSERT_EQUAL_INT(0, idn_contains_non_ascii(NULL));
}

/* ====== idn_convert_host_to_ascii tests ====== */

/* Test ASCII domain not converted: input "example.com" stays "example.com" */
void test_idn_convert_host_ascii_domain(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("example.com", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("example.com", output);
}

/* Test IPv4 address not converted: input "192.168.1.1" stays "192.168.1.1" */
void test_idn_convert_host_ipv4(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("192.168.1.1", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1", output);
}

/* Test IPv6 address not converted: input "[::1]" stays "[::1]" */
void test_idn_convert_host_ipv6_bracketed(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("[::1]", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("[::1]", output);
}

/* Test IPv6 address (without brackets) not converted */
void test_idn_convert_host_ipv6_plain(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    /* IPv6 address with multiple colons is copied directly */
    int ret = idn_convert_host_to_ascii("2001:db8::1", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("2001:db8::1", output);
}

/* Test hostname with port: input "example.com:8080" stays "example.com:8080" */
void test_idn_convert_host_with_port_ascii(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("example.com:8080", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("example.com:8080", output);
}

/* Test IPv4 address with port */
void test_idn_convert_host_ipv4_with_port(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("192.168.1.1:443", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("192.168.1.1:443", output);
}

/* Test bracketed IPv6 address with port */
void test_idn_convert_host_ipv6_with_port(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("[::1]:8080", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("[::1]:8080", output);
}

/*
 * Test Chinese domain conversion to Punycode.
 * Per RFC 3492 / IDNA2003, "中文.com" must convert to "xn--fiq228c.com".
 * The non-ASCII label "中文" is encoded to "fiq228c" and prefixed with "xn--",
 * while the ASCII label "com" passes through unchanged.
 */
void test_idn_convert_host_chinese_domain(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("中文.com", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("xn--fiq228c.com", output);
}

/*
 * Test Chinese domain with port.
 * Per RFC, "中文.com:8080" must convert to "xn--fiq228c.com:8080".
 */
void test_idn_convert_host_chinese_with_port(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("中文.com:8080", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("xn--fiq228c.com:8080", output);
}

/*
 * Test German umlaut domain conversion to Punycode.
 * Per RFC 3492 / IDNA2003, "münchen.de" must convert to "xn--mnchen-3ya.de".
 * The label "münchen" encodes basic ASCII "mnchen" first, then appends the
 * delimiter '-' and the encoded non-basic code point 'ü' (U+00FC) as "3ya".
 */
void test_idn_convert_host_umlaut_domain(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("münchen.de", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("xn--mnchen-3ya.de", output);
}

/*
 * Test multi-label IDN: each non-ASCII label is encoded independently.
 * "中文.中文.com" -> "xn--fiq228c.xn--fiq228c.com"
 */
void test_idn_convert_host_multi_label_idn(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("中文.中文.com", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("xn--fiq228c.xn--fiq228c.com", output);
}

/*
 * Test trailing dot (root domain) is preserved.
 * "中文.com." -> "xn--fiq228c.com."
 */
void test_idn_convert_host_trailing_dot(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_host_to_ascii("中文.com.", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("xn--fiq228c.com.", output);
}

/* Test idn_convert_host_to_ascii with NULL arguments */
void test_idn_convert_host_null_args(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_host_to_ascii(NULL, output, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_host_to_ascii("example.com", NULL, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_host_to_ascii("example.com", output, 0));
}

/* ====== idn_convert_url_to_ascii tests ====== */

/* Test URL conversion: input "ws://example.com/path" stays "ws://example.com/path" */
void test_idn_convert_url_ascii(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("ws://example.com/path", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("ws://example.com/path", output);
}

/* Test wss protocol URL conversion */
void test_idn_convert_url_wss(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("wss://api.example.com/v1/ws", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("wss://api.example.com/v1/ws", output);
}

/* Test URL with port conversion */
void test_idn_convert_url_with_port(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("ws://example.com:8080/path", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("ws://example.com:8080/path", output);
}

/* Test URL without scheme conversion */
void test_idn_convert_url_no_scheme(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("example.com/path", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("example.com/path", output);
}

/* Test URL with query parameters conversion */
void test_idn_convert_url_with_query(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("ws://example.com/path?token=abc", output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("ws://example.com/path?token=abc", output);
}

/*
 * Test URL containing Chinese domain.
 * Per RFC, "ws://中文.com/path" must convert to "ws://xn--fiq228c.com/path".
 * Only the host portion is IDN-encoded; scheme and path are preserved.
 */
void test_idn_convert_url_chinese(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_convert_url_to_ascii("ws://中文.com/path", output, sizeof(output));

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("ws://xn--fiq228c.com/path", output);
}

/* Test idn_convert_url_to_ascii with NULL arguments */
void test_idn_convert_url_null_args(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_url_to_ascii(NULL, output, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_url_to_ascii("ws://example.com", NULL, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_convert_url_to_ascii("ws://example.com", output, 0));
}

/* ====== idn_punycode_encode tests ====== */

/* Test idn_punycode_encode parameter validation */
void test_idn_punycode_encode_null_args(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    TEST_ASSERT_EQUAL_INT(-1, idn_punycode_encode(NULL, 0, output, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_punycode_encode("test", 4, NULL, sizeof(output)));
    TEST_ASSERT_EQUAL_INT(-1, idn_punycode_encode("test", 4, output, 0));
}

/*
 * Test idn_punycode_encode with pure ASCII input.
 * Pure ASCII input is copied directly to output without a delimiter.
 */
void test_idn_punycode_encode_ascii_only(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    int ret = idn_punycode_encode("hello", 5, output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("hello", output);
}

/*
 * Test idn_punycode_encode with Chinese characters.
 * Per RFC 3492, "中文" (U+4E2D U+6587) must encode to "fiq228c".
 */
void test_idn_punycode_encode_chinese(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    const char *input = "中文";
    size_t input_len = strlen(input);  /* UTF-8: 6 bytes */

    int ret = idn_punycode_encode(input, input_len, output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("fiq228c", output);
}

/*
 * Test idn_punycode_encode with mixed ASCII and non-ASCII.
 * Per RFC 3492, "münchen" must encode to "mnchen-3ya":
 * basic code points "mnchen" are emitted first, followed by the delimiter
 * '-', then the non-basic code point 'ü' (U+00FC) encoded as "3ya".
 */
void test_idn_punycode_encode_mixed_ascii(void) {
    char output[IDN_MAX_OUTPUT_LEN];
    const char *input = "münchen";
    size_t input_len = strlen(input);

    int ret = idn_punycode_encode(input, input_len, output, sizeof(output));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("mnchen-3ya", output);
}

int main(void) {
    UNITY_BEGIN();

    /* idn_contains_non_ascii tests */
    RUN_TEST(test_idn_contains_non_ascii_pure_ascii);
    RUN_TEST(test_idn_contains_non_ascii_with_unicode);
    RUN_TEST(test_idn_contains_non_ascii_null);

    /* idn_convert_host_to_ascii - ASCII/IP tests */
    RUN_TEST(test_idn_convert_host_ascii_domain);
    RUN_TEST(test_idn_convert_host_ipv4);
    RUN_TEST(test_idn_convert_host_ipv6_bracketed);
    RUN_TEST(test_idn_convert_host_ipv6_plain);
    RUN_TEST(test_idn_convert_host_with_port_ascii);
    RUN_TEST(test_idn_convert_host_ipv4_with_port);
    RUN_TEST(test_idn_convert_host_ipv6_with_port);

    /* idn_convert_host_to_ascii - IDN tests (exact Punycode vectors) */
    RUN_TEST(test_idn_convert_host_chinese_domain);
    RUN_TEST(test_idn_convert_host_chinese_with_port);
    RUN_TEST(test_idn_convert_host_umlaut_domain);
    RUN_TEST(test_idn_convert_host_multi_label_idn);
    RUN_TEST(test_idn_convert_host_trailing_dot);

    /* idn_convert_host_to_ascii - parameter validation */
    RUN_TEST(test_idn_convert_host_null_args);

    /* idn_convert_url_to_ascii tests */
    RUN_TEST(test_idn_convert_url_ascii);
    RUN_TEST(test_idn_convert_url_wss);
    RUN_TEST(test_idn_convert_url_with_port);
    RUN_TEST(test_idn_convert_url_no_scheme);
    RUN_TEST(test_idn_convert_url_with_query);
    RUN_TEST(test_idn_convert_url_chinese);
    RUN_TEST(test_idn_convert_url_null_args);

    /* idn_punycode_encode tests */
    RUN_TEST(test_idn_punycode_encode_null_args);
    RUN_TEST(test_idn_punycode_encode_ascii_only);
    RUN_TEST(test_idn_punycode_encode_chinese);
    RUN_TEST(test_idn_punycode_encode_mixed_ascii);

    return UNITY_END();
}
