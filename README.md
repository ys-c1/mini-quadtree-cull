# mini-quadtree-cull

2D 精灵剔除用的四叉树。精灵带矩形包围盒，按区域查询时需要拿到所有与该区域
相交的精灵，贴边（边贴边、角贴角）也算相交。

## 目录

```
src/quadtree.h      对外接口
src/quadtree.c      四叉树实现
tests/test_quadtree.c  行为测试
tools/cull_demo.c   命令行示例（统计随机场景下的候选数）
```

## 构建与测试

```sh
make test
```

## 接口

```c
int  qt_init(Quadtree *qt, Rect bounds, int max_depth, int max_items);
void qt_free(Quadtree *qt);
int  qt_insert(Quadtree *qt, int id, Rect rect);     /* 0 成功，-1 失败 */
int  qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids);
int  qt_contains(Quadtree *qt, int id, float px, float py);
void qt_stats(const Quadtree *qt, QtStats *out);
```

约定：

- 矩形宽高必须为正，否则 `qt_insert` 返回 -1
- 元素可以部分超出根节点范围，只要与根节点相交就能插入
- 节点元素数超过 `QT_NODE_CAPACITY` 时分裂；到 `max_depth` 后在该节点内溢出存放
- 查询结果顺序不保证

## 示例

```sh
make demo
```
