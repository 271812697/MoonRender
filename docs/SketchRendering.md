# 草图绘制：曲线、点与约束标注

> 本文讲“已经提交进 `SketcherObj` 的几何怎么画到视口”，即
> `SketcherObj::draw()` / `SketcherObjDraw.cpp` 这条路径。
> 正在创建的预览线（`DrawSketchHandler*` 的黄色即时预览）走的是另一条路径，见
> [SketchModelingWidget.md](SketchModelingWidget.md) 第 8 节。

## 1. 绘制入口与两套画布

`SketcherObj::draw()` 每帧会画两类内容：

1. **世界空间立即模式**（Im3D `renderer`）：背景网格/坐标轴、几何曲线、离散点；
2. **屏幕前景 ImDrawList**（`ImGui::GetForegroundDrawList()`）：约束图标、尺寸标注、
   约束序号等 2D 覆盖层。

两者共用一份几何数据：`mGeoSegment[geo]` 里的离散折线（`CurveSegment`）和关键点。

```text
SketcherObj::draw()
  ├─ drawBackground()            // 网格、无限 X/Y 轴、原点
  ├─ pushSize(curveLineWidth)
  ├─ 遍历 mGeoList：
  │     推 select/preselect/curve/construction 颜色
  │     实线画 mGeoSegment 折线；构造几何用虚线
  │     弹出颜色
  ├─ 再遍历 mGeoList：画关键点（保证点在线上层）
  ├─ 画选中点（最上层）
  ├─ drawConstraintLabels()      // 尺寸/角度等数值标注
  ├─ drawTangentIcons()          // 相切小图标
  └─ drawConstraintIcons()       // 重合/水平/垂直/平行/垂直约束图标等
```

关键文件：`Moon/Sketcher/SketcherObjDraw.cpp`。

## 2. 几何曲线怎么变成屏幕上的线

### 2.1 离散化缓存 `mGeoSegment`

每条几何加入 `mGeoList` 时，`addGeometry()` 会调用 `getCurveSegment()` 生成：

```text
CurveSegment {
    point[]    // OCC 曲线按参数等分 50 段得到的点（草图 2D 坐标）
    params[]   // 每个点对应的 OCC 参数 u
    sepoints[] // 关键点：线的 start/end；弧/圆的 start/end/center；...
}
```

绘制时把 `point[i]→point[i+1]` 用 `mPlane.valueEigen(...)` 抬到 3D 世界坐标，
再交给 Im3D `drawLine`。所以草图里的曲线本质是“50 段直线近似”，拾取、吸附、
绘制都基于同一份折线，误差一致。

### 2.2 颜色与选中态

颜色集中定义在 `DrawOption`（`SketcherTypes.h`，`SketcherObjWidget` 持有实例；重构前是 `SketcherObj::DrawOption`），按 ABGR 字节序（`{A,B,G,R}`）存放，
UI 面板（SketchTaskDialog）用 `ColorPickerProperty` 修改：

| 字段 | 含义 |
| --- | --- |
| `curveColor` | 普通曲线 |
| `constructionColor` | 构造几何（虚线 + 该颜色，默认绿色） |
| `pointColor` | 普通关键点 |
| `preselectColor` | 悬停高亮 |
| `selectColor` | 选中高亮 |
| `constraintColor` | 约束图标/序号颜色 |
| `externalColor` | 外部几何（另一 feature 投影进来的参考曲线），原点标记 |
| `xAxisColor` / `yAxisColor` | 草图 X/Y 轴（默认红/绿） |

绘制顺序决定优先级：先画普通颜色，再在**曲线循环里**判断选中/悬停覆盖颜色；
点标记单独循环（画在曲线上层）；选中点最后一层，保证不被其它点盖住。

轴的高亮按**元素**而不是按曲线：整条轴只在 `pointPos == none` 的选中/悬停下变色，
原点（`(HAxis, start)`，即 `GeoEnum::RtPnt`）只由它自己的选中驱动。否则点选一次
原点就会把整条无限长的 X 轴点亮。原点只由水平轴那一次绘制（`RtPnt` 与 `HAxis`
共用 id `-1`），两条轴各画一次会互相盖掉高亮。

### 2.3 构造几何虚线

判断“是否构造”同时看两个来源：

- `mConstructionGeoIds`（按 GeoId 维护的集合）；
- `Part::Geometry::getConstruction()`（几何体自带的构造标志，随 copy/clone 传播）。

构造曲线用 `drawDashedSketchPolyline()`：

```text
把折线逐段投影到屏幕像素坐标
按“实线 8px / 间隔 6px”切分
只把实线段转回世界坐标调用 drawLine
```

图案按屏幕像素计算，因此缩放相机时虚线疏密不变；段间相位连续，圆弧不会出现
“每小段重新起头”的麻点效果。

### 2.4 背景：网格与 X/Y 轴

`drawBackground()` 自适应网格。相机可以任意转动，所以网格必须**在草图平面
自己的 (u, v) 里算可见范围**——这一点是整个实现的关键。视图描述统一放在
`SketchPicking::gridView()`，绘制和网格吸附都从它取，保证看到与吸到的是同一个点阵：

```text
GridView（gridView() 输出）：平面上的点 (u, v) 在视图空间的位置
    view(u, v) = (oX + u·uX + v·vX,  oY + u·uY + v·vY)
    (oX,oY) = 平面原点的视图空间坐标，(uX,uY)/(vX,vY) = 两条轴在视图空间的方向
    hx, hy  = 视口在视图空间的半宽/半高；step = 网格步长（草图单位）

可见区域 = 视口两条边界（±hx、±hy）切出的四边形：
    oX + u·uX + v·vX = ±hx ,  oY + u·uY + v·vY = ±hy
四条直线两两相交得到四个角，取 AABB 作为“要遍历的网格线编号范围”，
每条线再用这两个不等式把自由参数夹到可见区间内。
```

- **为什么不能按屏幕矩形反算**：正交相机沿自己的视线投影，平面被看到的形状不是
  (u, v) 里的矩形而是那个四边形；`det = uX·vY - vX·uY` 就是平面法向与视线夹角
  的量度。早期实现把视口四角反投影成 (u,v) 的 AABB，只在正对着看时成立，
  相机一斜，四边形的角就跑到 AABB 外面 → **网格缺角**。
- **步长**：一个草图单位沿某条轴占据的视图空间长度为 `|(uX,uY)|`、`|(vX,vY)|`
  （正对着看时都是 1）。目标间距是 `SketcherObj::gridTargetPx()`（默认 20px，草图
  面板可调），取两者中**较小的那个**：斜视时横跨被压缩那条轴的线族才是会堆叠的，
  按小尺度定格距能让它保持在目标间距、另一族自然疏开（早先用几何平均，等于约束两个
  方向间距的乘积，总有一族落到 `目标² / 另一族`，一斜就成一片密线）；再向上取整到
  `1/2 × 10ⁿ` 阶梯。阶梯里没有 5，所以每缩放一档格子边长按 ×5、×2 交替变化：
  1、0.2、0.1、0.02、0.01……都是整的十进制数（旧的 `1/2/5` 阶梯会给出 0.5、0.05 这种值）。
- **遮挡关系**：网格和轴属于**场景**，画在深度测试批次里（`pushEnableSorting(true)` →
  `drawSort()`，`GREATER_EQUAL`：reversed-Z 下「更近的胜出」），所以在草图平面**前方**
  的实体挡住它们、在**后方**的实体被它们盖住。它们以前画在无深度测试的叠加批次里，
  于是永远糊在整个视图上。
- **为什么不是简单地把深度拍平**：拍平（整块网格沿视线挪到同一个深度）能让网格不被
  近/远平面切掉，但那样它就没有自己的深度了，遮挡判断全部失去意义。正确做法是**只把
  超出视锥的那部分压到裁剪面上**：正交投影下沿视线移动不改变屏幕位置，所以
  `BackdropDepth` / `splitBackdropSegment()` 把一条线段拆成最多 3 段——本身（真实深度，
  由 GPU 裁剪到范围内）＋ 两端的超出段（各自压在近/远平面上继续画）。远平面之外没有
  任何几何会被画出来，所以「压在远平面上」得到的遮挡结果与真实深度**完全一致**，
  画面又不会被切出一道边。
- 整个背景沿视线朝相机挪 `1e-5 × 深度范围`：草图所在的那个面与平面共面，不挪的话两者
  会逐像素抢深度（speckle）。
- **退化情形**：`|det|` 极小（视线落在平面内，平面在屏幕上是条线）时退回固定
  范围网格；掠射时四边形会拉得很长，用 `kMaxLinesPerFamily = 2000` 截断，
  丢掉的是离视口最远的那些线。
- 每 5 个小格一条大格线（`(i % 5) == 0`）。大格用**强**色、小格用**弱**色，配色与线宽
  都在 `DrawOption` 里（`gridMajorColor` / `gridMinorColor` / `gridLineWidth`，面板可改）。
  默认两支是同一个灰（128），只用 alpha 区分（大格 200 / 小格 90）：视口背景是天空盒的
  渐变（顶 54、底 207），固定的一深一浅不可能到处都对，而同一颜色多给/少给 alpha 的
  主次关系与背景明暗无关。
- X/Y 轴按 `drawAxisSpanning()` 绘制为**无限长直线**（与视口矩形求交取可见段，
  不是画一条很长的线段），颜色 X 红 / Y 绿；原点画一个点标记。

## 3. 约束与尺寸标注

### 3.1 两类标注：图标 vs 数值标签

| 类型 | 入口 | 内容 |
| --- | --- | --- |
| 非尺寸约束图标 | `drawConstraintIcons()` | Coincident/H/V/Parallel/Perpendicular/Equal/Symmetric/PointOnObject/Block 等 |
| 相切图标 | `drawTangentIcons()` | Tangent 的小切线符号（单独函数处理） |
| 尺寸/角度标签 | `drawConstraintLabels()` | Distance/Length/Radius/Diameter/Angle/DistanceX/Y 的箭头 + 数值框，可拖动 |

两类都先过滤 `isVisible == false` 与错误态：`constraintInError()` 为真时用红色绘制。

### 3.2 锚点计算 `anchorOf`

每个图标都要在草图元素上找一个“锚点”：

```text
PointPos != none → 取该几何对应点（start/end/mid）
PointPos == none → 取 getGeometryCenterSketch()：
    圆/椭圆取圆心；线取中点；其它曲线取离散折线中点
```

图标是否画两次取决于约束引用了几个对象：Equal/Perpendicular/Parallel/
两点 H/V 会把图标画在**每个对象自己的锚点**，并各自标**同一个约束序号**，
便于看出“哪几个元素属于同一条约束”。

### 3.3 各约束的图标样式（当前实现）

| 约束 | 图标 |
| --- | --- |
| Coincident | 偏移 16px 的小圆 + 圆内两个点 |
| PointOnObject | 锚点西北方向的半圆弧 + 中点小点 |
| Horizontal（两点/线） | 每个锚点上方一条水平横线 + 序号 |
| Vertical（两点/线） | 每个锚点右侧一条竖直短线 + 序号 |
| Parallel | 每个锚点一组 `//` + 序号 |
| Perpendicular | 每个锚点一个直角符号 + 序号 |
| Equal | 每个锚点一组 `=` + 序号 |
| Symmetric | 中点竖线 + 左右两点 |
| Block | 锚点处小方框 |

序号 = 约束在 `mConstraintList` 里的下标（视口内与列表同序），绘制为放大 1.4 倍
的屏幕文字，完整图标颜色跟随 `DrawOption::constraintColor`。

### 3.4 尺寸标注的轨道

`drawConstraintLabels()` 内部先算“测量轨道”再放数值框：

- `DistanceX` / `DistanceY`：两点之间的水平/竖直轨道，数值沿轨道滑动
  （`m_straightDimOffsetSketch` 记尺寸线偏移，`m_labelManualParam` 记 0..1 位置）；
- 普通长度（`Distance`）：平行于被测线段、沿法线偏移的轨道；
- 角度：一段圆弧轨道，标签只能沿弧拖动（`m_angleLabelRadiusSketch` 记半径）；
- **半径 / 直径：两个端点由 `radiusDimShaft()` 统一给出** —— 圆心和圆周上一点，
  两者都是“草图坐标 → 屏幕”的投影结果，所以箭头两端必然落在圆心和圆上，
  相机怎么转都成立：
  - 方向：圆弧取**弧中点方向**（弧真正覆盖的那部分），圆取 45°；标签被拖到别处后，
    改用它拖到的方向（`sketchDirectionOfScreenVector()` 把屏幕方向换算成草图方向）。
    这里**不夹**到弧的扫掠范围内：夹住会让标签拖不动，而且落定后的标注会和放置时的预览
    对不上；代价是把标签拖到弧没覆盖的那半圆时，箭头会指向那个空位置；
  - 标签放在 `rim + 18px`、与箭头同一条线上；`Diameter` 用双箭头贯穿圆心。

> 圆心来自 `getGeometryCenterSketch()`：**圆弧要取它所在圆（基圆）的圆心，不是两端点的中点**。
> 早期实现里圆弧落到了“有界曲线”分支，返回的是**弦中点**，于是半径标注的起点不在圆心、
> 末端也不在同一圆周上，并且和预览对不上（预览走的是 sepoint 的 `mid`，那才是真正的圆心）。

偏移量一律存成**草图单位**（`...Sketch` 系列），绘制时用 `pixelsPerSketchUnit()`
换算成像素：标注跟着图纸一起缩放，而不是钉在屏幕上。

## 4. 交互相关绘制细节

### 4.1 尺寸标签可被拾取

`pickConstraintLabelAt()` 会把鼠标点与每个标签包围盒比较；命中的标签在悬停时
描边高亮，左键按住进入 `m_labelDrag`，拖动只改标签的屏幕偏移，不改几何。
双击标签会弹出数值编辑框，走 `editConstraintValue()` → `setDatum()` → `solve()`。

### 4.2 错误约束红色反馈

`constraintInError()` 检查 `lastConflicting/lastRedundant/lastMalformed` 等诊断
集合；错误约束的图标与标签会用红色绘制，为后续“点击高亮冲突约束”预留入口。

## 5. 参考源码

- `Moon/Sketcher/SketcherObjDraw.cpp`（本文主体）
- `Moon/Sketcher/SketcherObj.h`（`DrawOption`、`CurveSegment`）
- `Moon/Sketcher/SketcheTool2D.cpp`（`CurveConvert::toVector2D` 离散化）
- `Moon/Sketcher/SketcherObj.cpp`（`solve()` 后重建 mGeoSegment）
- 关联文档：[SketcherObj.md](SketcherObj.md)、[SketchConstraints.md](SketchConstraints.md)
