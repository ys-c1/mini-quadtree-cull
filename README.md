# mini-quadtree-cull

2D 精灵剔除用的四叉树。精灵带矩形包围盒，按区域查询时需要拿到所有与该区域
相交的精灵，贴边（边贴边、角贴角）也算相交。

## 目录

```
src/quadtree.h          对外接口
src/quadtree.c          四叉树实现
tests/test_quadtree.c   行为测试（含与逐对象手算的随机对拍）
tools/cull_demo.c       基准：一万/十万两档规模的查询统计与耗时
```

## 构建与测试

```sh
make test
make demo    # 10k / 100k 两档，剪枝开与关各跑一遍
```

## 接口

```c
int  qt_init(Quadtree *qt, Rect bounds, int max_depth, int max_items);
void qt_free(Quadtree *qt);
int  qt_insert(Quadtree *qt, int id, Rect rect);     /* 0 成功，负数为错误码 */
int  qt_query(Quadtree *qt, Rect area, int *out_ids, int max_ids);
int  qt_query_ex(Quadtree *qt, Rect area, int *out_ids, int max_ids,
                 unsigned flags, QtQueryStats *stats);
int  qt_contains(Quadtree *qt, int id, float px, float py);
void qt_stats(const Quadtree *qt, QtStats *out);
```

约定：

- 矩形宽高必须为正，否则 `qt_insert` 返回 `QT_ERR_EMPTY`
- 元素可以部分超出根节点范围，只要与根节点相交就能插入
- 节点元素数超过 `QT_NODE_CAPACITY` 时分裂；到 `max_depth` 后在该节点内溢出存放
- 能被某个子节点完整容纳的元素才下沉；跨切分线的元素留在父节点，
  保证「元素完整落在其所在节点的包围盒内」（根节点例外，允许部分超出）
- 查询结果顺序不保证；`max_ids` 不够时只写入前 `max_ids` 个

### 错误码

`qt_init` / `qt_insert` / `qt_query` / `qt_query_ex` 失败时返回负数：

| 错误码 | 值 | 触发条件 |
| --- | --- | --- |
| `QT_ERR_EMPTY` | -2 | 矩形 `w <= 0` 或 `h <= 0`（空区域/空元素） |
| `QT_ERR_NONFINITE` | -3 | 任一坐标或宽高为 NaN / Inf（优先于空区域判定） |
| `QT_ERR_DEPTH` | -4 | `max_depth` 不在 `[1, QT_MAX_DEPTH]` |
| `QT_ERR_FAIL` | -1 | 空指针、缓冲区参数非法、元素越界、超出 `max_items`、内存不足 |

### 两段式查询

1. **阶段一（节点级剪枝）**：查询区域与节点包围盒求交，不相交则整棵子树跳过。
   根节点例外——允许存在部分超出根范围的元素，因此根节点自身总是展开。
2. **阶段二（逐项判定）**：对访问到的节点里已存的元素逐个做精确相交判定，
   包括跨切分线、因此留在内部节点上的元素。

`QT_QUERY_PRUNE`（默认）开启剪枝，`QT_QUERY_NO_PRUNE` 关闭：关闭时遍历全部
节点，结果与开启时完全一致，只是访问与判定次数变成全量，用于对照和测试。

### 查询统计

`qt_query_ex` 通过 `QtQueryStats` 返回：

| 字段 | 含义 |
| --- | --- |
| `nodes_visited` | 阶段一进入的节点数 |
| `nodes_pruned` | 因包围盒不相交而整棵跳过的节点数 |
| `items_tested` | 阶段二逐项判定次数 |
| `items_hit` | 相交元素总数（不受 `max_ids` 截断影响） |
| `hits_written` | 实际写入 `out_ids` 的个数 |

## 基准

`make demo` 会构造均匀小对象、大对象与聚簇混合的场景，对一万与十万两档规模
分别查询，并与「逐对象手算」（对全部元素线性扫描）的结果核对命中数。
