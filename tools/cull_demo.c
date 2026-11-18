#include "../src/quadtree.h"

#include <stdio.h>
#include <stdlib.h>

/* 固定种子生成一批精灵，统计「全量遍历」与「四叉树查询」的候选数差异。 */
int main(void) {
    enum { N = 2000 };
    Rect world = {0.0f, 0.0f, 1024.0f, 1024.0f};
    Quadtree qt;
    if (qt_init(&qt, world, 6, N * 2) != 0) {
        printf("init failed\n");
        return 1;
    }

    unsigned seed = 12345u;
    int inserted = 0;
    for (int i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        float x = (float)((seed >> 8) % 1000);
        seed = seed * 1103515245u + 12345u;
        float y = (float)((seed >> 8) % 1000);
        Rect r = {x, y, 8.0f, 8.0f};
        if (qt_insert(&qt, i + 1, r) == 0) inserted++;
    }

    Rect view = {100.0f, 100.0f, 200.0f, 200.0f};
    int out[N];
    int n = qt_query(&qt, view, out, N);

    QtStats stats;
    qt_stats(&qt, &stats);
    printf("inserted=%d query_hits=%d nodes=%d leaves=%d depth=%d\n", inserted, n, stats.nodes,
           stats.leaves, stats.max_depth);
    printf("brute_force_candidates=%d quadtree_candidates=%d\n", N, n);
    qt_free(&qt);
    return 0;
}
