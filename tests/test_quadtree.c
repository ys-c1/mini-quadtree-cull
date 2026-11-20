#include "../src/quadtree.h"

#include <math.h>
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

/* 逐对象手算：直接拿每个元素与区域判定，作为标准答案。 */
static int brute_count(const QuadItem *items, int n, Rect area) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (rect_intersects_public(items[i].rect, area)) c++;
    }
    return c;
}

static int brute_ids(const QuadItem *items, int n, Rect area, int *out) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (rect_intersects_public(items[i].rect, area)) out[c++] = items[i].id;
    }
    return c;
}

/* 两个结果集是否一致（顺序不保证，按集合比较；假定 id 唯一）。 */
static int same_set(const int *a, int na, const int *b, int nb) {
    if (na != nb) return 0;
    for (int i = 0; i < na; i++) {
        if (!ids_contain(b, nb, a[i])) return 0;
    }
    return 1;
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

/* 跨切分线的元素：只按节点包围盒剪枝时不能漏。 */
static void test_straddling_item(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 4, 64) == 0, "初始化失败");
    /* 这根长条横跨 x=32 的切分线。 */
    CHECK(qt_insert(&qt, 1, (Rect){0.0f, 0.0f, 40.0f, 10.0f}) == 0, "长条插入失败");
    for (int i = 0; i < 8; i++) {
        CHECK(qt_insert(&qt, i + 2, (Rect){(float)i * 4.0f, 0.0f, 2.0f, 2.0f}) == 0,
              "填充元素 %d 插入失败", i + 2);
    }
    int out[32];
    int n = qt_query(&qt, (Rect){33.0f, 0.0f, 7.0f, 10.0f}, out, 32);
    CHECK(n == 1 && ids_contain(out, n, 1), "长条右半段应命中 id=1，实际命中 %d 个", n);

    /* 元素部分超出根节点、只与根相交时也要能查到。 */
    CHECK(qt_insert(&qt, 100, (Rect){-10.0f, 40.0f, 20.0f, 10.0f}) == 0, "越界元素插入失败");
    n = qt_query(&qt, (Rect){-10.0f, 40.0f, 5.0f, 10.0f}, out, 32);
    CHECK(n == 1 && ids_contain(out, n, 100), "越界元素应命中，实际 %d", n);
    qt_free(&qt);
}

/* 随机场景：四叉树结果必须与逐对象手算完全一致；剪枝开/关也必须一致。 */
static void test_random_against_brute(void) {
    enum { N = 3000 };
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 1024.0f, 1024.0f}, 6, N * 2) == 0, "初始化失败");

    QuadItem *items = (QuadItem *)malloc(sizeof(QuadItem) * N);
    int stored = 0;
    unsigned seed = 12345u;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 8) % 1000);
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 8) % 1000);
        /* 尺寸有大有小，制造大量跨线元素。 */
        float w = 4.0f + (float)((seed >> 8) % 40);
        Rect r = {x, y, w, w};
        if (qt_insert(&qt, i + 1, r) == 0) {
            items[stored].id = i + 1;
            items[stored].rect = r;
            stored++;
        }
    }
    CHECK(stored == N, "应插入 %d 个，实际 %d", N, stored);

    int mismatches = 0;
    for (int q = 0; q < 200; q++) {
        seed = seed * 1103515245u + 12345u;
        float ax = (float)((seed >> 8) % 1000);
        seed = seed * 1103515245u + 12345u;
        float ay = (float)((seed >> 8) % 1000);
        seed = seed * 1103515245u + 12345u;
        float aw = 8.0f + (float)((seed >> 8) % 200);
        Rect area = {ax, ay, aw, aw};

        int want[N], got[N], got_noprune[N];
        int nw = brute_ids(items, stored, area, want);
        int ng = qt_query(&qt, area, got, N);
        int nn = qt_query_ex(&qt, area, got_noprune, N, QT_QUERY_NO_PRUNE, NULL);
        if (!same_set(got, ng, want, nw) || !same_set(got_noprune, nn, want, nw)) {
            if (mismatches++ < 5) {
                printf("  区域 %.1f,%.1f %.1fx%.1f: brute=%d prune=%d noprune=%d\n", area.x,
                       area.y, area.w, area.h, nw, ng, nn);
            }
        }
    }
    CHECK(mismatches == 0, "四叉树与逐对象手算不一致的区域查询有 %d 个", mismatches);
    free(items);
    qt_free(&qt);
}

/* 剪枝确实减少访问：小区域应远少于总节点数；关闭剪枝则应访问全部节点。 */
static void test_pruning_statistics(void) {
    enum { N = 4000 };
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 1024.0f, 1024.0f}, 6, N * 2) == 0, "初始化失败");
    QuadItem *items = (QuadItem *)malloc(sizeof(QuadItem) * N);
    for (int i = 0; i < N; i++) {
        Rect r = {(float)((i * 37) % 1024), (float)((i * 101) % 1024), 4.0f, 4.0f};
        CHECK(qt_insert(&qt, i + 1, r) == 0, "插入失败");
        items[i].id = i + 1;
        items[i].rect = r;
    }
    QtStats all;
    qt_stats(&qt, &all);

    int out[N];
    QtQueryStats pruned, full;
    Rect small = {100.0f, 100.0f, 16.0f, 16.0f};
    qt_query_ex(&qt, small, out, N, QT_QUERY_PRUNE, &pruned);
    qt_query_ex(&qt, small, out, N, QT_QUERY_NO_PRUNE, &full);

    CHECK(full.nodes_visited == all.nodes, "关闭剪枝应访问全部 %d 个节点，实际 %lld", all.nodes,
          full.nodes_visited);
    CHECK(full.nodes_pruned == 0, "关闭剪枝不应有剪枝计数，实际 %lld", full.nodes_pruned);
    CHECK(pruned.nodes_pruned > 0, "小区域应有剪枝，实际 %lld", pruned.nodes_pruned);
    CHECK(pruned.nodes_visited < full.nodes_visited, "剪枝应减少访问节点数：%lld vs %lld",
          pruned.nodes_visited, full.nodes_visited);
    CHECK(pruned.items_tested < full.items_tested, "剪枝应减少逐项判定：%lld vs %lld",
          pruned.items_tested, full.items_tested);
    CHECK(pruned.items_hit == full.items_hit, "剪枝不应改变命中总数：%lld vs %lld",
          pruned.items_hit, full.items_hit);
    CHECK(pruned.items_hit == brute_count(items, N, small), "命中总数应与逐对象手算一致：%lld vs %d",
          pruned.items_hit, brute_count(items, N, small));
    free(items);
    qt_free(&qt);
}

/* 输出缓冲区不足：命中数照常统计，只截断写入。 */
static void test_truncation(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 4, 64) == 0, "初始化失败");
    for (int i = 0; i < 10; i++) {
        CHECK(qt_insert(&qt, i + 1, (Rect){(float)i * 4.0f, 0.0f, 4.0f, 4.0f}) == 0, "插入失败");
    }
    int out[3];
    QtQueryStats st;
    int n = qt_query_ex(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, out, 3, QT_QUERY_PRUNE, &st);
    CHECK(n == 3, "写入应被截断到 3，实际 %d", n);
    CHECK(st.items_hit == 10, "命中总数应为 10，实际 %lld", st.items_hit);
    CHECK(st.hits_written == 3, "写入数应为 3，实际 %d", st.hits_written);
    qt_free(&qt);
}

/* 三类非法输入必须逐个报错，且互不混淆。 */
static void test_error_codes(void) {
    Quadtree qt;
    CHECK(qt_init(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 4, 64) == 0, "初始化失败");
    CHECK(qt_insert(&qt, 1, (Rect){0.0f, 0.0f, 8.0f, 8.0f}) == 0, "插入失败");
    int out[8];

    /* 空区域：w<=0 或 h<=0。 */
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, 0.0f, 8.0f}, out, 8) == QT_ERR_EMPTY,
          "w=0 应报空区域");
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, 8.0f, 0.0f}, out, 8) == QT_ERR_EMPTY,
          "h=0 应报空区域");
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, -1.0f, 8.0f}, out, 8) == QT_ERR_EMPTY,
          "w<0 应报空区域");

    /* 非有限坐标：NaN / Inf，任何字段都算。 */
    float nan_v = NAN, inf_v = INFINITY;
    CHECK(qt_query(&qt, (Rect){nan_v, 0.0f, 8.0f, 8.0f}, out, 8) == QT_ERR_NONFINITE,
          "x=NaN 应报非有限");
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, inf_v, 8.0f}, out, 8) == QT_ERR_NONFINITE,
          "w=Inf 应报非有限");
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, 8.0f, -inf_v}, out, 8) == QT_ERR_NONFINITE,
          "h=-Inf 应报非有限（优先于空区域）");
    CHECK(qt_insert(&qt, 2, (Rect){nan_v, 0.0f, 8.0f, 8.0f}) == QT_ERR_NONFINITE,
          "插入 NaN 应报非有限");

    /* 深度超上限 / 下限。 */
    Quadtree bad;
    CHECK(qt_init(&bad, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, QT_MAX_DEPTH + 1, 64) == QT_ERR_DEPTH,
          "深度超上限应报深度错误");
    CHECK(qt_init(&bad, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, 0, 64) == QT_ERR_DEPTH,
          "深度为 0 应报深度错误");
    CHECK(qt_init(&bad, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, -3, 64) == QT_ERR_DEPTH,
          "深度为负应报深度错误");
    CHECK(qt_init(&bad, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, QT_MAX_DEPTH, 64) == 0,
          "深度正好等于上限应成功");
    qt_free(&bad);

    /* 合法查询仍返回命中数（非负）。 */
    CHECK(qt_query(&qt, (Rect){0.0f, 0.0f, 64.0f, 64.0f}, out, 8) == 1, "合法查询应命中 1 个");
    qt_free(&qt);
}

int main(void) {
    test_touching_edges();
    test_split_line();
    test_capacity_split();
    test_depth_limit_overflow();
    test_straddling_item();
    test_random_against_brute();
    test_pruning_statistics();
    test_truncation();
    test_error_codes();

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures) {
        printf("TESTS FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
