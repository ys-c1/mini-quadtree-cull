#ifndef QUADTREE_H
#define QUADTREE_H

#ifdef __cplusplus
extern "C" {
#endif

#define QT_NODE_CAPACITY 4
#define QT_MAX_DEPTH 8
#define QT_MAX_ITEMS 4096

typedef struct Rect {
    float x;
    float y;
    float w;
    float h;
} Rect;

typedef struct QuadItem {
    int id;
    Rect rect;
} QuadItem;

typedef struct QuadNode {
    Rect bounds;
    int depth;
    int count;
    QuadItem *items;
    struct QuadNode *children[4];
} QuadNode;

typedef struct Quadtree {
    QuadNode *root;
    int max_depth;
    int max_items;
    int stored;
    int last_query_candidates;
} Quadtree;

typedef struct QtStats {
    int stored;
    int nodes;
    int leaves;
    int max_depth;
} QtStats;

/* qt_compact 返回码：所有异常都显式报错，不静默吞掉。 */
#define QT_COMPACT_OK 0
#define QT_COMPACT_ERR_ARG -1            /* qt 为 NULL */
#define QT_COMPACT_ERR_ZERO_NODES -2     /* 树里一个节点都没有 */
#define QT_COMPACT_ERR_DEPTH -3          /* 节点深度与实际层深不一致 */
#define QT_COMPACT_ERR_MERGE_CONFLICT -4 /* 合并冲突：孩子越出父边界或元素 id 重复 */
#define QT_COMPACT_ERR_COUNT -5          /* 树上元素数与 stored 计数对不上 */
#define QT_COMPACT_ERR_NOMEM -6          /* 内存分配失败 */

typedef struct QtCompactReport {
    int nodes_before;         /* 压缩前节点数 */
    int nodes_after_merge;    /* 第一段（剪空节点+合并单孩子）之后 */
    int nodes_after_rebuild;  /* 第二段（按新结构重建索引）之后 */
    int items;                /* 重建时实际搬运的元素数 */
    int merged;               /* 合并掉的单孩子节点数 */
    int pruned;               /* 剪掉的空节点数 */
    double merge_seconds;     /* 第一段耗时 */
    double rebuild_seconds;   /* 第二段耗时 */
} QtCompactReport;

/* 两段式压缩：先合并只含单个孩子的节点，再按新结构重建索引。
 * 重建前后任意查询的命中集合逐项相同；出错返回上面的负值错误码。 */
int qt_compact(Quadtree *qt, QtCompactReport *out);

int qt_init(Quadtree *qt, Rect bounds, int max_depth, int max_items);
void qt_free(Quadtree *qt);
int qt_insert(Quadtree *qt, int id, Rect rect);
int qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids);
int qt_contains(Quadtree *qt, int id, float px, float py);
void qt_stats(const Quadtree *qt, QtStats *out);

int rect_intersects_public(Rect a, Rect b);

#ifdef __cplusplus
}
#endif

#endif
