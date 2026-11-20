#define _POSIX_C_SOURCE 199309L /* clock_gettime */
#include "../src/quadtree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * 区域查询基准：
 *   - 一万 (10,000) 与十万 (100,000) 两档对象规模
 *   - 每档统计：阶段一访问/剪枝节点数、阶段二逐项判定次数、命中次数、单次查询耗时
 *   - 「逐对象手算」标准答案即对全部元素做一次线性扫描，用它校验命中数
 *   - 剪枝与关闭剪枝各跑一遍，确认结果一致
 */

enum { NLARGE = 100000 };

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

typedef struct Scenario {
    Quadtree qt;
    QuadItem *items;
    int n;
    int stored;
    char name[48];
} Scenario;

static int brute_intersect(const QuadItem *items, int n, Rect area, long long *tested) {
    int c = 0;
    for (int i = 0; i < n; i++) {
        (*tested)++;
        if (rect_intersects_public(items[i].rect, area)) c++;
    }
    return c;
}

static int build(Scenario *sc, const char *name, int n, unsigned seed, int max_depth) {
    snprintf(sc->name, sizeof(sc->name), "%s", name);
    sc->n = n;
    sc->stored = 0;
    sc->items = (QuadItem *)malloc(sizeof(QuadItem) * (size_t)n);
    if (!sc->items) return -1;
    if (qt_init(&sc->qt, (Rect){0.0f, 0.0f, 4096.0f, 4096.0f}, max_depth, n + 16) != 0) {
        free(sc->items);
        return -1;
    }

    /* 三种分布混合：均匀大对象、均匀小对象、聚簇，逼出跨切分线的元素。 */
    for (int i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        unsigned kind = (seed >> 8) % 10;
        float x, y, w;
        seed = seed * 1103515245u + 12345u;
        float a = (float)((seed >> 8) % 4000);
        seed = seed * 1103515245u + 12345u;
        float b = (float)((seed >> 8) % 4000);
        if (kind < 6) { /* 小对象 */
            x = a;
            y = b;
            w = 4.0f + (float)((seed >> 12) % 12);
        } else if (kind < 9) { /* 大对象，容易跨线 */
            x = a;
            y = b;
            w = 48.0f + (float)((seed >> 12) % 160);
        } else { /* 聚簇 */
            x = 1800.0f + (float)(a / 40.0f);
            y = 1800.0f + (float)(b / 40.0f);
            w = 8.0f;
        }
        Rect r = {x, y, w, w};
        if (qt_insert(&sc->qt, i + 1, r) == 0) {
            sc->items[sc->stored].id = i + 1;
            sc->items[sc->stored].rect = r;
            sc->stored++;
        }
    }
    return 0;
}

static void report(Scenario *sc, Rect area, const char *label, int repeat) {
    int *out = (int *)malloc(sizeof(int) * (size_t)sc->stored);

    QtQueryStats st;
    /* 预热一次，避免首次缺页影响计时。 */
    qt_query_ex(&sc->qt, area, out, sc->stored, QT_QUERY_PRUNE, &st);

    double t0 = now_us();
    for (int r = 0; r < repeat; r++) {
        qt_query_ex(&sc->qt, area, out, sc->stored, QT_QUERY_PRUNE, &st);
    }
    double t_pruned = (now_us() - t0) / (double)repeat;

    QtQueryStats st_full;
    t0 = now_us();
    for (int r = 0; r < repeat; r++) {
        qt_query_ex(&sc->qt, area, out, sc->stored, QT_QUERY_NO_PRUNE, &st_full);
    }
    double t_full = (now_us() - t0) / (double)repeat;

    long long brute_tested = 0;
    int brute = brute_intersect(sc->items, sc->stored, area, &brute_tested);

    QtStats tree;
    qt_stats(&sc->qt, &tree);

    printf("  [%s] area=(%.0f,%.0f %.0fx%.0f) repeats=%d\n", label, area.x, area.y, area.w,
           area.h, repeat);
    printf("    brute force : tested=%lld hits=%d\n", brute_tested, brute);
    printf("    pruned      : nodes_visited=%lld nodes_pruned=%lld items_tested=%lld "
           "hits=%lld written=%d  %.3f us/query\n",
           st.nodes_visited, st.nodes_pruned, st.items_tested, st.items_hit, st.hits_written,
           t_pruned);
    printf("    no-prune    : nodes_visited=%lld nodes_pruned=%lld items_tested=%lld "
           "hits=%lld written=%d  %.3f us/query\n",
           st_full.nodes_visited, st_full.nodes_pruned, st_full.items_tested, st_full.items_hit,
           st_full.hits_written, t_full);
    printf("    tree        : nodes=%d leaves=%d depth=%d stored=%d\n", tree.nodes, tree.leaves,
           tree.max_depth, tree.stored);
    int ok = (st.items_hit == brute) && (st_full.items_hit == brute);
    printf("    check       : %s\n", ok ? "hits match per-object brute force" : "MISMATCH");
    free(out);
}

static void run_tier(const char *name, int n, int max_depth, unsigned seed, Rect area,
                     const char *label, int repeat) {
    Scenario sc;
    if (build(&sc, name, n, seed, max_depth) != 0) {
        printf("%s: build failed\n", name);
        return;
    }
    printf("%s: requested=%d stored=%d\n", sc.name, n, sc.stored);
    report(&sc, area, label, repeat);
    qt_free(&sc.qt);
    free(sc.items);
}

int main(void) {
    printf("== quadtree region query benchmark ==\n");

    run_tier("10k objects", 10000, 6, 12345u, (Rect){1000.0f, 1000.0f, 400.0f, 400.0f}, "medium",
             200);
    run_tier("10k objects", 10000, 6, 12345u, (Rect){2050.0f, 2050.0f, 16.0f, 16.0f}, "tiny", 200);

    run_tier("100k objects", NLARGE, 8, 777u, (Rect){1000.0f, 1000.0f, 400.0f, 400.0f}, "medium",
             30);
    run_tier("100k objects", NLARGE, 8, 777u, (Rect){2050.0f, 2050.0f, 16.0f, 16.0f}, "tiny", 30);
    return 0;
}
