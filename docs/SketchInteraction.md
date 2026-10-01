# 草图交互：拾取、吸附与几何拖动

> 本文聚焦 `SketcherObj` 在“非绘制工具激活”状态下如何响应鼠标：
> 悬停预选 → 点选/框选 → 拖动求解。几何创建工具的事件链另见
> [SketchModelingWidget.md](SketchModelingWidget.md)。

## 1. 拾取：`pickGeo` → `testSelect`

### 1.1 屏幕判定

拾取与吸附都先把草图 2D 坐标经
`getplaneTransform()`（平面 → 世界）和相机视口矩阵转成屏幕坐标，再用**像素距离**
判断。默认阈值：关键点 5px、曲线本体 15px（`getPickGeoIndex`）或 5px（`testSelect`
第二遍扫曲线时统一 5px），窗口缩放不改变手感。

### 1.2 扫描顺序

`testSelect(pos)` 按“元素越小越具体越优先”的顺序扫，命中即停：

```text
⓪ 原点：屏幕距离 ≤10px 直接返回 {HAxis, start}（即根点 RtPnt）
① 关键点：比较 mGeoSegment[geo].sepoints（每条曲线的 start/end/mid 等）
           命中 → 返回 {GeoId, PointPos}，点类型保留
② 曲线本体：比较离散折线 point[] 到鼠标的线段距离
           命中 → 返回 {GeoId, PointPos::none}（整条曲线）
③ 坐标轴：按“无限长直线”算垂距（几何本身只从原点伸出一个长度）
           命中 → 返回 {VAxis/HAxis, none}
```

候选集是「草图自身曲线 + 外部几何（含两条轴）」，顺序上前者在前，所以同距离时
草图自己的元素先被取到。

**③ 放在最后是必须的**：轴是无限长的，鼠标落在轴上任意一点都算"在线上"，如果和
曲线一起比较距离，一条画在 Y 轴上的直线（两者只差零点几像素）会永远抢不过轴，
变成"只能选中 Y 轴"。FreeCAD 也是先测几何、最后测坐标十字。
另外轴不参与 ①——它的定义段从原点伸到 500，那个远端不是任何特征点，
把它当拾取目标会给出一个屏幕上根本没画的点；轴上的点只有原点算数，
由 ⓪ 负责。

因此：

- 点线端点/圆心 → 得到 `start/end/mid`，拖动时执行“端点语义”；
- 点到曲线中部 → 得到 `none`，拖动时执行“移动/改半径语义”；
- 点到坐标轴 → 得到整条轴（`none`），可以拿去做约束对象。

隐藏几何（`mHiddenGeoIds`）与构造几何的处理：

- 隐藏几何两遍都不参与拾取；
- 构造几何默认**可以被点选**（编辑构造辅助线），但不会进入框选与吸附。

## 2. 吸附链：`snapPoint`

工具创建曲线和普通悬停共用一条链，命中即停：

```text
① 显式关键点  start/end/mid（含构造几何跳过）
② 原点、X 轴、Y 轴（屏幕距离 ≤10px）
③ 曲线本体（点到折线段投影）
④ 网格交叉点（m_drawGrid && m_snapToGrid，最低优先级）
```

注意：

- 构造几何（`mConstructionGeoIds`）不参与吸附，避免辅助点把鼠标吸走；
- 网格吸附步长与 `drawBackground()` 自适应步长一致（都来自 `SketcherObj::gridView()`，
  因此相机斜视时也一致），判定仍按 10px；
- 吸附会把 `onSketchPosMove` 直接改写为吸附点，因此后续创建/拖动的坐标是
  “吸附后的坐标”。

## 3. 选择与拖动状态机

只有 `!isHaveActiveHandler && isInEdit` 时状态机才运行：

```text
Stop ──悬停命中──► Hot ──左键按下──► OperationGeo ──松开──► Hot
  │                  │                                  │
  └──空白处按下──► DragRect ──松开──► Stop（框选结果）   └──移开无命中──► Stop
```

辅助键：

- `Ctrl` 按住 = `AppendSelect`（追加选择）；
- 松开 Ctrl = `OverrideSelect`（再次左键会替换当前选择）；
- `Delete` = 删除选中几何并自动清理/平移约束，随后 `solve()`。

框选规则（`DragRect`）：

- 整条曲线的**所有离散点**都在框内 → 选中整条（`PointPos::none`）；
- 否则只把落在框内的显式关键点加入选择；
- 同一坐标只允许被一个点选中（防止重合端点被选两份）。

## 4. 拖动几何：不同“抓点”对应不同编辑语义

### 4.1 三条拖动路径

`SketcherObj::onMouseMove()` 在 `OperationGeo` 下会先判断单选圆/弧：

| 场景 | 路径 | 效果 |
| --- | --- | --- |
| 单选圆/弧**本体**（`PointPos::none`） | solver 半径拖动：`initMove` 锚住圆心 + `moveGeometries(..., 鼠标绝对点, false)` | 圆心不动，半径跟随鼠标；约束联动（槽帽等） |
| 单选圆/弧的**端点/圆心** | 直接 `moveGeo()` 参数语义 + `solve()` | 端点沿原圆滑动、圆心拖动即移动圆心 |
| 其它（线端点/整线/多选） | solver 相对平移：`moveGeometries(..., 累计位移, true)` | 被拖元素（组）整体刚性平移，约束参与求解 |

### 4.2 几何类型 × 抓点语义（`moveGeo` 直接路径）

| 几何 | 抓点 | 直接语义 |
| --- | --- | --- |
| 直线 | start/end | 只移动该端点（另一个端点不变） |
| 直线 | none/mid | 整条平移 |
| 圆 | none | 半径 = `dist(鼠标, 圆心)`，圆心不变 |
| 圆 | mid | 圆心移到鼠标 |
| 圆弧 | none | 半径 = `dist(鼠标, 圆心)`，圆心/角度不变 |
| 圆弧 | mid | 圆心移到鼠标 |
| 圆弧 | start/end | 计算“鼠标相对圆心角度 - 当前端点角度”的连续差量，加到原角度参数；圆心与半径不变 |
| BSpline | none/mid | 整体平移 |
| GeomPoint | start | 移动点 |

圆弧端点不用“鼠标绝对角度覆盖参数”，而用**差量更新**：

```text
delta = wrapPi(atan2(mouse-center) - atan2(端点-center))
新角度参数 = 旧角度参数 + delta
```

这样跨过 ±180°/0° 时不会产生参数跳变，也不会把弧扫成反向区间。

### 4.3 求解器路径与直接路径怎么协作

正常约束草图走 solver 路径，保证拖动结果满足全部约束；只有下面两类例外：

1. **单选圆/弧的端点/圆心**直接按几何参数编辑，是因为 solver 的“相对平移”
   会把整条曲线连圆心一起带走，不符合“拖端点=在圆上滑”的交互语义；
2. solver `initMove()` 失败（冲突/畸形）时回退到 `moveGeo()` 直接改参数，
   随后补一次 `solve()`，尽量保持约束一致性。

拖动结束后：

- 松开左键 → `selectState = Hot`，`resetInitMove()`，清掉拖动临时约束；
- 拖动中每次成功都会 `extractGeometry()` 原位回填 `mGeoList` 并重建
  `mGeoSegment`，因此拾取/绘制缓存始终和求解结果一致。

## 5. 常见问题与排查点

| 现象 | 检查点 |
| --- | --- |
| 拖圆弧端点心却动 | 确认 `selectIds[0].pointPos` 是 start/end（不是 none）；看是否走了 solver 相对平移分支 |
| 拖弧本体圆心漂移 | 该路径应锚住圆心；若仍漂移，检查弧是否被其它约束牵扯、或 `initMove` 失败走了 `moveGeo` |
| 吸附不到曲线 | 确认几何不是 construction（吸附会跳过）且没被隐藏 |
| 点选总选到端点 | `sepoints` 5px 阈值优先于曲线本体，靠近端点会被判为端点 |
| 框选后剩下辅助点 | 构造几何不进框选；若确实要删，请先删引用它的约束/几何 |

## 6. 参考源码

- `Moon/Sketcher/SketcherInteraction.cpp`：`testSelect` / `pickGeo` / `snapPoint` /
  `moveGeo` / `onMouseMove`
- `Moon/Sketcher/SketcherObj.h`：`SelectGeoId`、选择状态、`CurveSegment`
- `Moon/Sketcher/SketcheTool2D.cpp`：折线离散化（拾取与吸附共用）
- 关联文档：[SketcherObj.md](SketcherObj.md)、[SketchConstraints.md](SketchConstraints.md)、
  [SketchRendering.md](SketchRendering.md)
