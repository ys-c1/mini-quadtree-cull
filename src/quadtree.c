#include "quadtree.h"

#include <stdlib.h>
#include <string.h>

/* 两个矩形是否相交。接触（边贴边、角贴角）算相交：
   只有严格分离（>）才算不相交。 */
static int rect_intersects(Rect a, Rect b) {
    if (a.x > b.x + b.w) return 0;
    if (b.x > a.x + a.w) return 0;
    if (a.y > b.y + b.h) return 0;
    if (b.y > a.y + a.h) return 0;
    return 1;
}

int rect_intersects_public(Rect a, Rect b) {
    return rect_intersects(a, b);
}

/* r 是否完整包含 item（贴边也算包含）。 */
static int rect_contains_rect(Rect r, Rect item) {
    return item.x >= r.x && item.x + item.w <= r.x + r.w && item.y >= r.y &&
           item.y + item.h <= r.y + r.h;
}

static int rect_contains_point(Rect r, float px, float py) {
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

static QuadNode *node_new(Rect bounds, int depth) {
    QuadNode *n = (QuadNode *)calloc(1, sizeof(QuadNode));
    if (!n) return NULL;
    n->bounds = bounds;
    n->depth = depth;
    n->items = (QuadItem *)calloc(QT_NODE_CAPACITY, sizeof(QuadItem));
    if (!n->items) {
        free(n);
        return NULL;
    }
    return n;
}

static void node_free(QuadNode *n) {
    if (!n) return;
    for (int i = 0; i < 4; i++) node_free(n->children[i]);
    free(n->items);
    free(n);
}

/* 往节点本地数组追加一个元素，容量不够就扩容。失败返回 0。 */
static int node_add_item(QuadNode *n, QuadItem item) {
    if (n->count >= QT_NODE_CAPACITY) {
        QuadItem *grown =
            (QuadItem *)realloc(n->items, sizeof(QuadItem) * (size_t)(n->count + 1));
        if (!grown) return 0;
        n->items = grown;
    }
    n->items[n->count++] = item;
    return 1;
}

/* 分裂出四个子节点。任一分配失败则回滚，返回 0。 */
static int node_split(QuadNode *n) {
    float hw = n->bounds.w * 0.5f;
    float hh = n->bounds.h * 0.5f;
    QuadNode *c[4];
    c[0] = node_new((Rect){n->bounds.x, n->bounds.y, hw, hh}, n->depth + 1);
    c[1] = node_new((Rect){n->bounds.x + hw, n->bounds.y, hw, hh}, n->depth + 1);
    c[2] = node_new((Rect){n->bounds.x, n->bounds.y + hh, hw, hh}, n->depth + 1);
    c[3] = node_new((Rect){n->bounds.x + hw, n->bounds.y + hh, hw, hh}, n->depth + 1);
    for (int i = 0; i < 4; i++) {
        if (!c[i]) {
            for (int k = 0; k < 4; k++) node_free(c[k]);
            return 0;
        }
    }
    for (int i = 0; i < 4; i++) n->children[i] = c[i];
    return 1;
}

static int node_insert(QuadNode *n, QuadItem item, int max_depth);

/* 分裂后把节点里已存的元素重新归位：能被某个子节点完整容纳的下沉，
   跨切分线的留在本节点。 */
static void node_redistribute(QuadNode *n, int max_depth) {
    QuadItem existing[QT_NODE_CAPACITY];
    int n_existing = n->count;
    memcpy(existing, n->items, sizeof(QuadItem) * (size_t)n_existing);
    n->count = 0;
    for (int k = 0; k < n_existing; k++) {
        node_insert(n, existing[k], max_depth);
    }
}

static int node_insert(QuadNode *n, QuadItem item, int max_depth) {
    if (!rect_intersects(n->bounds, item.rect)) return 0;

    if (n->children[0]) {
        /* 已有子节点：只有被某个子节点完整容纳才下沉，
           否则留在本节点，保证每个元素只存一份。 */
        for (int i = 0; i < 4; i++) {
            if (n->children[i] && rect_contains_rect(n->children[i]->bounds, item.rect)) {
                return node_insert(n->children[i], item, max_depth);
            }
        }
        return node_add_item(n, item);
    }

    /* 叶子：容量未满，或已到深度上限（溢出存放，不丢元素）。 */
    if (n->count < QT_NODE_CAPACITY || n->depth >= max_depth) {
        return node_add_item(n, item);
    }

    /* 容量已满且未到深度上限：分裂后重新归位，再插入新元素。
       分裂失败（内存不足）时退化为节点内溢出存放。 */
    if (!node_split(n)) {
        return node_add_item(n, item);
    }
    node_redistribute(n, max_depth);
    return node_insert(n, item, max_depth);
}

static void node_query(QuadNode *n, Rect area, int *out, int max, int *count) {
    if (!rect_intersects(n->bounds, area)) return;
    for (int i = 0; i < n->count; i++) {
        if (rect_intersects(n->items[i].rect, area)) {
            if (*count < max) out[(*count)++] = n->items[i].id;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (n->children[i]) node_query(n->children[i], area, out, max, count);
    }
}

static int node_has_point(QuadNode *n, Rect area, int id) {
    if (!rect_intersects(n->bounds, area)) return 0;
    for (int i = 0; i < n->count; i++) {
        if (n->items[i].id == id && rect_intersects(n->items[i].rect, area)) return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (n->children[i] && node_has_point(n->children[i], area, id)) return 1;
    }
    return 0;
}

int qt_init(Quadtree *qt, Rect bounds, int max_depth, int max_items) {
    if (!qt) return -1;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return -1;
    if (max_depth < 1 || max_depth > QT_MAX_DEPTH) return -1;
    memset(qt, 0, sizeof(*qt));
    qt->root = node_new(bounds, 0);
    if (!qt->root) return -1;
    qt->max_depth = max_depth;
    qt->max_items = max_items;
    return 0;
}

void qt_free(Quadtree *qt) {
    if (!qt) return;
    node_free(qt->root);
    qt->root = NULL;
}

int qt_insert(Quadtree *qt, int id, Rect rect) {
    if (!qt || !qt->root) return -1;
    if (rect.w <= 0.0f || rect.h <= 0.0f) return -1;
    if (!rect_intersects(qt->root->bounds, rect)) return -1;
    if (qt->stored >= qt->max_items) return -1;
    QuadItem item;
    item.id = id;
    item.rect = rect;
    if (!node_insert(qt->root, item, qt->max_depth)) return -1;
    qt->stored++;
    return 0;
}

int qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids) {
    if (!qt || !qt->root || !out_ids || max_ids <= 0) return 0;
    int count = 0;
    node_query(qt->root, area, out_ids, max_ids, &count);
    qt->last_query_candidates = count;
    return count;
}

int qt_contains(Quadtree *qt, int id, float px, float py) {
    if (!qt || !qt->root) return 0;
    if (!rect_contains_point(qt->root->bounds, px, py)) return 0;
    Rect area = {px, py, 0.0f, 0.0f};
    return node_has_point(qt->root, area, id);
}

void qt_stats(const Quadtree *qt, QtStats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!qt || !qt->root) return;
    out->stored = qt->stored;
    out->max_depth = qt->max_depth;

    QuadNode *stack[512];
    int top = 0;
    stack[top++] = qt->root;
    while (top > 0) {
        QuadNode *n = stack[--top];
        out->nodes++;
        int has_child = 0;
        for (int i = 0; i < 4; i++) {
            if (n->children[i]) {
                has_child = 1;
                if (top < 512) stack[top++] = n->children[i];
            }
        }
        if (!has_child) out->leaves++;
    }
}
