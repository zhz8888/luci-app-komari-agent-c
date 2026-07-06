/*
 * test_v2.c - v2 protocol runtime state unit tests
 *
 * Covers:
 *   - v2_state_init / v2_state_cleanup lifecycle
 *   - v2_add_seen_event capacity expansion order (m-6 regression)
 *   - v2_add_ack_event / v2_snapshot_ack_ids (M-5 concurrency-safe snapshot)
 *   - v2_note_attempt_result / v2_should_fallback_to_v1 fallback threshold
 */

#include "unity.h"
#include "v2.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

void setUp(void) {
}

void tearDown(void) {
}

/* ====== v2_state_init / v2_state_cleanup ====== */

void test_v2_state_init_succeeds(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));
    TEST_ASSERT_EQUAL_INT(0, state.fail_count);
    TEST_ASSERT_EQUAL_INT(0, state.seen_count);
    TEST_ASSERT_EQUAL_INT(0, state.ack_count);
    v2_state_cleanup(&state);
}

void test_v2_state_init_null_returns_error(void) {
    TEST_ASSERT_EQUAL_INT(-1, v2_state_init(NULL));
}

void test_v2_state_cleanup_null_is_safe(void) {
    v2_state_cleanup(NULL);
}

/* ====== v2_add_seen_event capacity expansion order (m-6) ====== */

/* Fill the seen-events ring past V2_SEEN_EVENTS_MAX and verify:
 *   1. seen_count never exceeds V2_SEEN_EVENTS_MAX (oldest is evicted)
 *   2. seen_capacity does not grow unbounded (no wasted realloc when the
 *      limit is reached; the fix checks the limit before expanding)
 */
void test_v2_add_seen_event_respects_limit(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    char buf[32];
    for (int i = 0; i < V2_SEEN_EVENTS_MAX + 50; i++) {
        snprintf(buf, sizeof(buf), "evt-%d", i);
        TEST_ASSERT_EQUAL_INT(0, v2_add_seen_event(&state, buf));
    }

    TEST_ASSERT_EQUAL_INT(V2_SEEN_EVENTS_MAX, state.seen_count);

    /* The oldest 50 entries ("evt-0" .. "evt-49") should have been evicted,
     * so "evt-50" is the oldest remaining and must be marked as seen. */
    TEST_ASSERT_TRUE(v2_is_event_seen(&state, "evt-50"));
    TEST_ASSERT_FALSE(v2_is_event_seen(&state, "evt-49"));

    /* Capacity must not exceed the next power of two above the limit.
     * The pre-fix code could trigger one extra realloc when seen_count
     * reached a power-of-two capacity equal to the limit. */
    TEST_ASSERT_TRUE(state.seen_capacity <= V2_SEEN_EVENTS_MAX * 2);

    v2_state_cleanup(&state);
}

/* Adding the same event twice must deduplicate (seen_count stays at 1). */
void test_v2_add_seen_event_deduplicates(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    TEST_ASSERT_EQUAL_INT(0, v2_add_seen_event(&state, "dup"));
    TEST_ASSERT_EQUAL_INT(0, v2_add_seen_event(&state, "dup"));
    TEST_ASSERT_EQUAL_INT(1, state.seen_count);

    v2_state_cleanup(&state);
}

/* ====== v2_add_ack_event / v2_snapshot_ack_ids (M-5) ====== */

void test_v2_snapshot_ack_ids_returns_copy(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    TEST_ASSERT_EQUAL_INT(0, v2_add_ack_event(&state, 100));
    TEST_ASSERT_EQUAL_INT(0, v2_add_ack_event(&state, 200));
    TEST_ASSERT_EQUAL_INT(0, v2_add_ack_event(&state, 300));

    int buf[8];
    int count = -1;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&state, buf, 8, &count));
    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_INT(100, buf[0]);
    TEST_ASSERT_EQUAL_INT(200, buf[1]);
    TEST_ASSERT_EQUAL_INT(300, buf[2]);

    /* Mutating the snapshot buffer must not affect internal state. */
    buf[0] = 9999;
    int buf2[8];
    int count2 = -1;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&state, buf2, 8, &count2));
    TEST_ASSERT_EQUAL_INT(100, buf2[0]);

    v2_state_cleanup(&state);
}

void test_v2_snapshot_ack_ids_truncates_to_max(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_EQUAL_INT(0, v2_add_ack_event(&state, i));
    }

    int buf[4];
    int count = -1;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&state, buf, 4, &count));
    TEST_ASSERT_EQUAL_INT(4, count);
    TEST_ASSERT_EQUAL_INT(0, buf[0]);
    TEST_ASSERT_EQUAL_INT(1, buf[1]);
    TEST_ASSERT_EQUAL_INT(2, buf[2]);
    TEST_ASSERT_EQUAL_INT(3, buf[3]);

    /* Internal state still holds all 10 entries. */
    TEST_ASSERT_EQUAL_INT(10, state.ack_count);

    v2_state_cleanup(&state);
}

void test_v2_snapshot_ack_ids_empty_state(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    int buf[4];
    int count = -1;
    TEST_ASSERT_EQUAL_INT(0, v2_snapshot_ack_ids(&state, buf, 4, &count));
    TEST_ASSERT_EQUAL_INT(0, count);

    v2_state_cleanup(&state);
}

void test_v2_snapshot_ack_ids_null_args(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    int count;
    TEST_ASSERT_EQUAL_INT(-1, v2_snapshot_ack_ids(NULL, NULL, 0, NULL));
    TEST_ASSERT_EQUAL_INT(-1, v2_snapshot_ack_ids(&state, NULL, 4, &count));
    TEST_ASSERT_EQUAL_INT(-1, v2_snapshot_ack_ids(&state, (int *)0x1, 4, NULL));

    v2_state_cleanup(&state);
}

void test_v2_clear_acks_resets_count(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    v2_add_ack_event(&state, 1);
    v2_add_ack_event(&state, 2);
    TEST_ASSERT_EQUAL_INT(2, state.ack_count);

    v2_clear_acks(&state);
    TEST_ASSERT_EQUAL_INT(0, state.ack_count);

    /* Buffer is retained for reuse, so capacity is unchanged. */
    TEST_ASSERT_TRUE(state.ack_capacity >= 2);

    v2_state_cleanup(&state);
}

/* ====== v2_note_attempt_result / v2_should_fallback_to_v1 ====== */

void test_v2_fallback_threshold(void) {
    v2_state_t state;
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&state));

    TEST_ASSERT_FALSE(v2_should_fallback_to_v1(&state));

    v2_note_attempt_result(&state, 0);
    v2_note_attempt_result(&state, 0);
    TEST_ASSERT_FALSE(v2_should_fallback_to_v1(&state));

    v2_note_attempt_result(&state, 0);
    TEST_ASSERT_TRUE(v2_should_fallback_to_v1(&state));

    /* Success resets the failure counter. */
    v2_note_attempt_result(&state, 1);
    TEST_ASSERT_FALSE(v2_should_fallback_to_v1(&state));

    v2_state_cleanup(&state);
}

/* ====== Concurrency test ====== */

/* Stress test: multiple producer threads call v2_add_ack_event and
 * v2_add_seen_event while a consumer thread calls v2_snapshot_ack_ids and
 * v2_clear_acks. Run under ASan/TSan to detect data races or use-after-free
 * in the mutex-protected state. */

#define CONCURRENCY_PRODUCERS 4
#define CONCURRENCY_ITERATIONS 2000

static volatile int g_concurrency_running = 1;
static v2_state_t g_concurrency_state;

static void *concurrency_producer(void *arg) {
    int tid = (int)(long)arg;
    char event_id[32];
    for (int i = 0; i < CONCURRENCY_ITERATIONS; i++) {
        if (!g_concurrency_running) break;
        snprintf(event_id, sizeof(event_id), "evt-%d-%d", tid, i);
        v2_add_ack_event(&g_concurrency_state, tid * 100000 + i);
        v2_add_seen_event(&g_concurrency_state, event_id);
    }
    return NULL;
}

static void *concurrency_consumer(void *arg) {
    (void)arg;
    int buf[256];
    int count;
    for (int i = 0; i < CONCURRENCY_ITERATIONS; i++) {
        if (!g_concurrency_running) break;
        v2_snapshot_ack_ids(&g_concurrency_state, buf, 256, &count);
        if (i % 500 == 0) {
            v2_clear_acks(&g_concurrency_state);
        }
    }
    return NULL;
}

void test_v2_concurrent_access_no_crash(void) {
    TEST_ASSERT_EQUAL_INT(0, v2_state_init(&g_concurrency_state));
    g_concurrency_running = 1;

    pthread_t producers[CONCURRENCY_PRODUCERS];
    pthread_t consumer;

    for (int i = 0; i < CONCURRENCY_PRODUCERS; i++) {
        pthread_create(&producers[i], NULL, concurrency_producer, (void *)(long)i);
    }
    pthread_create(&consumer, NULL, concurrency_consumer, NULL);

    for (int i = 0; i < CONCURRENCY_PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }
    g_concurrency_running = 0;
    pthread_join(consumer, NULL);

    /* After all threads finish, the state must still be consistent:
     * ack_count >= 0 and <= V2_ACK_IDS_MAX, seen_count >= 0. */
    TEST_ASSERT_TRUE(g_concurrency_state.ack_count >= 0);
    TEST_ASSERT_TRUE(g_concurrency_state.ack_count <= V2_ACK_IDS_MAX);
    TEST_ASSERT_TRUE(g_concurrency_state.seen_count >= 0);

    v2_state_cleanup(&g_concurrency_state);
}

int main(void) {
    UNITY_BEGIN();

    /* Lifecycle */
    RUN_TEST(test_v2_state_init_succeeds);
    RUN_TEST(test_v2_state_init_null_returns_error);
    RUN_TEST(test_v2_state_cleanup_null_is_safe);

    /* m-6: capacity expansion order */
    RUN_TEST(test_v2_add_seen_event_respects_limit);
    RUN_TEST(test_v2_add_seen_event_deduplicates);

    /* M-5: snapshot ACK interface */
    RUN_TEST(test_v2_snapshot_ack_ids_returns_copy);
    RUN_TEST(test_v2_snapshot_ack_ids_truncates_to_max);
    RUN_TEST(test_v2_snapshot_ack_ids_empty_state);
    RUN_TEST(test_v2_snapshot_ack_ids_null_args);
    RUN_TEST(test_v2_clear_acks_resets_count);

    /* Fallback threshold */
    RUN_TEST(test_v2_fallback_threshold);

    /* Concurrency */
    RUN_TEST(test_v2_concurrent_access_no_crash);

    return UNITY_END();
}
