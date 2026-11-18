#include "../src/quadtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        g_checks++;                                                   \
        if (!(cond)) {                                                \
            g_failures++;                                             \
            printf("FAIL %s:%d  ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

static int ids_contain(const int *ids, int n, int want) {
    for (int i = 0; i < n; i++) {
        if (ids[i] == want) return 1;
    }
    return 0;
}

/* 贴边相交必须被判定为相交。 */
static void test_touching_edges(void) {
    Rect a = {0.0f, 0.0f, 10.0f, 10.0f};
    Rect right = {10.0f, 0.0f, 5.0f, 10.0f};
    Rect below = {0.0f, 10.0f, 10.0f, 5.0f};
    Rect corner = {10.0f, 10.0f, 3.0f, 3.0f};
    CHECK(rect_intersects_public(a, right) == 1, "右边贴边应相交");
    CHECK(rect_intersects_public(a, below) == 1, "下边贴边应相交");
    CHECK(rect_intersects_public(a, corner) == 1, "角贴角应相交");

    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 4, 64) == 0, "初始化失败");
    CHECK(qt_insert(&qt, 1, (Rect){0.0f, 0.0f, 32.0f, 32.0f}) == 0, "插入失败");
    int out[16];
    int n = qt_query(&qt, (Rect){32.0f, 0.0f, 8.0f, 8.0f}, out, 16);
    CHECK(n == 1 && ids_contain(out, n, 1), "查询区贴左边应命中，实际命中 %d 个", n);
    qt_free(&qt);
}

/* 正好落在切分线上的元素不能丢。 */
static void test_split_line(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 4, 128) == 0, "初始化失败");
    for (int i = 0; i < 8; i++) {
        Rect r = {(float)i * 8.0f, 0.0f, 8.0f, 8.0f};
        CHECK(qt_insert(&qt, i + 1, r) == 0, "插入第 %d 个失败", i + 1);
    }
    for (int i = 0; i < 8; i++) {
        Rect area = {(float)i * 8.0f, 0.0f, 8.0f, 8.0f};
        int out[32];
        int n = qt_query(&qt, area, out, 32);
        CHECK(ids_contain(out, n, i + 1), "第 %d 个元素查不到（命中 %d 个）", i + 1, n);
    }

    QtStats stats;
    qt_stats(&qt, &stats);
    CHECK(stats.stored == 8, "统计里应有 8 个，实际 %d", stats.stored);
    qt_free(&qt);
}

/* 超过节点容量必须分裂，元素一个都不能丢。 */
static void test_capacity_split(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 128.0f, 128.0f}, 5, 512) == 0, "初始化失败");
    for (int i = 0; i < 200; i++) {
        float x = (float)(i % 16) * 8.0f;
        float y = (float)(i / 16) * 8.0f;
        int rc = qt_insert(&qt, i + 1, (Rect){x, y, 4.0f, 4.0f});
        CHECK(rc == 0, "第 %d 个插入失败", i + 1);
    }
    int out[512];
    int n = qt_query(&qt, (Rect){0.0f, 0.0f, 128.0f, 128.0f}, out, 512);
    CHECK(n == 200, "全区域查询应命中 200 个，实际 %d", n);

    QtStats stats;
    qt_stats(&qt, &stats);
    CHECK(stats.nodes > 1, "应发生分裂，实际节点数 %d", stats.nodes);

    /* 抽几个元素做点包含检查。 */
    CHECK(qt_contains(&qt, 1, 1.0f, 1.0f) == 1, "id=1 应包含点 (1,1)");
    CHECK(qt_contains(&qt, 1, 100.0f, 100.0f) == 0, "id=1 不应包含点 (100,100)");
    qt_free(&qt);
}

/* 深度上限处溢出存放，不能静默丢弃。 */
static void test_depth_limit_overflow(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 8.0f, 8.0f}, 1, 256) == 0, "初始化失败");
    for (int i = 0; i < 20; i++) {
        Rect r = {0.5f, 0.5f, 1.0f, 1.0f};
        int rc = qt_insert(&qt, i + 1, r);
        CHECK(rc == 0, "深度上限处第 %d 个应能溢出存放，实际返回 %d", i + 1, rc);
    }
    int out[64];
    int n = qt_query(&qt, (Rect){0.0f, 0.0f, 8.0f, 8.0f}, out, 64);
    CHECK(n == 20, "深度上限处应查到 20 个，实际 %d", n);
    qt_free(&qt);
}

int main(void) {
    test_touching_edges();
    test_split_line();
    test_capacity_split();
    test_depth_limit_overflow();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        printf("TESTS FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
