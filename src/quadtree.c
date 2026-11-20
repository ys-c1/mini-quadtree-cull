#include "quadtree.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 两个矩形是否相交。接触（边贴边、角贴角）算相交。 */
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

/* 矩形坐标是否全部有限（排除 NaN / Inf）。 */
static int rect_is_finite(Rect r) {
    return isfinite(r.x) && isfinite(r.y) && isfinite(r.w) && isfinite(r.h);
}

/* outer 是否完整包含 inner（贴边也算包含）。 */
static int rect_contains_rect(Rect outer, Rect inner) {
    return inner.x >= outer.x && inner.x + inner.w <= outer.x + outer.w &&
           inner.y >= outer.y && inner.y + inner.h <= outer.y + outer.h;
}

static int rect_contains_point(Rect r, float px, float py) {
    return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
}

static QuadNode *node_new(Rect bounds, int depth) {
    QuadNode *n = (QuadNode *)calloc(1, sizeof(QuadNode));
    if (!n) return NULL;
    n->bounds = bounds;
    n->depth = depth;
    n->cap = QT_NODE_CAPACITY;
    n->items = (QuadItem *)calloc((size_t)n->cap, sizeof(QuadItem));
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

/* 在节点本地追加一个元素；容量不足时倍增。跨切分线的元素会一直
   留在父节点，因此本地数组可能超过 QT_NODE_CAPACITY。 */
static int node_add_item(QuadNode *n, QuadItem item) {
    if (n->count >= n->cap) {
        int new_cap = n->cap * 2;
        QuadItem *grown = (QuadItem *)realloc(n->items, sizeof(QuadItem) * (size_t)new_cap);
        if (!grown) return 0;
        n->items = grown;
        n->cap = new_cap;
    }
    n->items[n->count++] = item;
    return 1;
}

/* 分裂出四个子节点；任一失败则整体回滚并返回 0。 */
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

/* 分裂后把已存元素重新归位。 */
static void node_redistribute(QuadNode *n, int max_depth) {
    int n_existing = n->count;
    /* 正常路径下 n_existing == QT_NODE_CAPACITY；仍按任意 count 处理，
       避免日后容量策略改变时越界。 */
    QuadItem stack_buf[QT_NODE_CAPACITY];
    QuadItem *existing = stack_buf;
    if (n_existing > QT_NODE_CAPACITY) {
        existing = (QuadItem *)malloc(sizeof(QuadItem) * (size_t)n_existing);
        if (!existing) return; /* 内存不足：放弃重排，元素仍在本节点 */
    }
    memcpy(existing, n->items, sizeof(QuadItem) * (size_t)n_existing);
    n->count = 0;
    for (int k = 0; k < n_existing; k++) {
        node_insert(n, existing[k], max_depth);
    }
    if (existing != stack_buf) free(existing);
}

/*
 * 元素只存一份：只有被某个子节点完整包含时才下沉，否则留在当前节点。
 * 这保证「元素一定落在其所在节点的包围盒内」，阶段一按节点包围盒剪枝
 * 才不会漏掉跨切分线的元素。
 */
static int node_insert(QuadNode *n, QuadItem item, int max_depth) {
    if (!rect_intersects(n->bounds, item.rect)) return 0;

    if (n->children[0]) {
        for (int i = 0; i < 4; i++) {
            if (n->children[i] && rect_contains_rect(n->children[i]->bounds, item.rect)) {
                return node_insert(n->children[i], item, max_depth);
            }
        }
        /* 跨切分线：留在本节点。 */
        return node_add_item(n, item);
    }

    /* 叶子：容量未满，或已到深度上限（溢出存放，不丢元素）。 */
    if (n->count < QT_NODE_CAPACITY || n->depth >= max_depth) {
        return node_add_item(n, item);
    }

    /* 容量已满且未到深度上限：分裂、重排、再插入。 */
    if (!node_split(n)) return node_add_item(n, item);
    node_redistribute(n, max_depth);
    return node_insert(n, item, max_depth);
}

/*
 * 两段式查询：
 *   阶段一：区域与节点包围盒求交，不相交则整棵跳过（剪枝）；
 *           关闭剪枝时照常访问全部节点，只在统计上反映代价。
 *   阶段二：对访问到的节点内元素逐项做精确定义的交判定。
 *
 * 除根节点外，元素都被保证完整落在所在节点的包围盒内，因此按包围盒
 * 剪枝不会漏元素。根节点例外：允许部分超出根范围的元素存在（只要求与
 * 根相交），所以根节点的元素总是逐项判定，只有它的子节点按包围盒剪枝。
 */
static void node_query(QuadNode *n, Rect area, int *out, int max, int *count, QtQueryStats *st,
                       int prune) {
    st->nodes_visited++;

    int hit = rect_intersects(n->bounds, area);
    if (prune && !hit && n->depth > 0) {
        /* 本节点及整棵子树都不可能命中。 */
        st->nodes_pruned++;
        return;
    }

    for (int i = 0; i < n->count; i++) {
        st->items_tested++;
        if (rect_intersects(n->items[i].rect, area)) {
            st->items_hit++;
            if (*count < max) out[(*count)++] = n->items[i].id;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (n->children[i]) {
            node_query(n->children[i], area, out, max, count, st, prune);
        }
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
    if (!qt) return QT_ERR_FAIL;
    if (!rect_is_finite(bounds)) return QT_ERR_NONFINITE;
    if (bounds.w <= 0.0f || bounds.h <= 0.0f) return QT_ERR_EMPTY;
    if (max_depth < 1 || max_depth > QT_MAX_DEPTH) return QT_ERR_DEPTH;
    memset(qt, 0, sizeof(*qt));
    qt->root = node_new(bounds, 0);
    if (!qt->root) return QT_ERR_FAIL;
    qt->max_depth = max_depth;
    qt->max_items = max_items;
    return QT_OK;
}

void qt_free(Quadtree *qt) {
    if (!qt) return;
    node_free(qt->root);
    qt->root = NULL;
}

int qt_insert(Quadtree *qt, int id, Rect rect) {
    if (!qt || !qt->root) return QT_ERR_FAIL;
    if (!rect_is_finite(rect)) return QT_ERR_NONFINITE;
    if (rect.w <= 0.0f || rect.h <= 0.0f) return QT_ERR_EMPTY;
    if (!rect_intersects(qt->root->bounds, rect)) return QT_ERR_FAIL;
    if (qt->stored >= qt->max_items) return QT_ERR_FAIL;
    QuadItem item;
    item.id = id;
    item.rect = rect;
    if (!node_insert(qt->root, item, qt->max_depth)) return QT_ERR_FAIL;
    qt->stored++;
    return QT_OK;
}

int qt_query_ex(Quadtree *qt, Rect area, int *out_ids, int max_ids, unsigned flags,
                QtQueryStats *stats) {
    QtQueryStats local;
    memset(&local, 0, sizeof(local));
    if (stats) *stats = local;

    if (!qt || !qt->root) return QT_ERR_FAIL;
    if (!out_ids || max_ids <= 0) return QT_ERR_FAIL;
    /* 先判非有限，否则 NaN 会把下面的空区域判断绕过。 */
    if (!rect_is_finite(area)) return QT_ERR_NONFINITE;
    if (area.w <= 0.0f || area.h <= 0.0f) return QT_ERR_EMPTY;

    int count = 0;
    node_query(qt->root, area, out_ids, max_ids, &count, &local,
               (flags & QT_QUERY_NO_PRUNE) ? 0 : 1);
    local.hits_written = count;
    qt->last_query_candidates = (int)local.items_hit;
    if (stats) *stats = local;
    return count;
}

int qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids) {
    return qt_query_ex(qt, area, out_ids, max_ids, QT_QUERY_PRUNE, NULL);
}

int qt_contains(Quadtree *qt, int id, float px, float py) {
    if (!qt || !qt->root) return 0;
    if (!isfinite(px) || !isfinite(py)) return 0;
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
