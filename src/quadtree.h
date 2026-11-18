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
