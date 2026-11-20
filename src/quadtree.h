#ifndef QUADTREE_H
#define QUADTREE_H

#ifdef __cplusplus
extern "C" {
#endif

#define QT_NODE_CAPACITY 4
#define QT_MAX_DEPTH 8
#define QT_MAX_ITEMS 4096

/* 错误码：全部为负。qt_query / qt_insert 成功时返回非负值。 */
typedef enum QtStatus {
    QT_OK = 0,
    QT_ERR_FAIL = -1,      /* 其他失败：空指针、容量、越界、内存不足 */
    QT_ERR_EMPTY = -2,     /* 区域为空：w <= 0 或 h <= 0 */
    QT_ERR_NONFINITE = -3, /* 坐标含 NaN / Inf */
    QT_ERR_DEPTH = -4      /* max_depth 不在 [1, QT_MAX_DEPTH] */
} QtStatus;

/* 查询标志 */
#define QT_QUERY_PRUNE 0u    /* 默认：阶段一按区域与节点包围盒求交剪枝 */
#define QT_QUERY_NO_PRUNE 1u /* 关闭剪枝：访问全部节点，用于对照与测试 */

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
    int cap;
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

/* 单次查询的统计量。 */
typedef struct QtQueryStats {
    long long nodes_visited; /* 阶段一进入的节点数（含被剪掉的节点） */
    long long nodes_pruned;  /* 阶段一因包围盒不相交而整棵跳过的节点数 */
    long long items_tested;  /* 阶段二逐项判定次数 */
    long long items_hit;     /* 相交元素总数，不受 max_ids 截断影响 */
    int hits_written;        /* 实际写入 out_ids 的个数 */
} QtQueryStats;

int qt_init(Quadtree *qt, Rect bounds, int max_depth, int max_items);
void qt_free(Quadtree *qt);
int qt_insert(Quadtree *qt, int id, Rect rect);
int qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids);
int qt_query_ex(Quadtree *qt, Rect area, int *out_ids, int max_ids, unsigned flags,
                QtQueryStats *stats);
int qt_contains(Quadtree *qt, int id, float px, float py);
void qt_stats(const Quadtree *qt, QtStats *out);

int rect_intersects_public(Rect a, Rect b);

#ifdef __cplusplus
}
#endif

#endif
