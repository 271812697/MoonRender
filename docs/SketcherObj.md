# SketcherObj：工作职责、功能清单与开发计划

> 本文基于 `Moon/Sketcher/` 的实际实现整理，并对照 FreeCAD 的
> `src/Mod/Sketcher/App/SketchObject*`（App 侧）与 `src/Mod/Sketcher/Gui/ViewProviderSketch`（交互侧）
> 说明本项目完成了其中哪些、还缺哪些。

---

## 0. 2026-10 重构：数据与编辑面拆开了

文中的行为描述仍然有效，但**职责已经拆成两个类**（这就是 FreeCAD 的那条线）：

| 现在在哪 | 文件 | 负责 |
| --- | --- | --- |
| `SketcherObj` | `SketcherObj.h` / `SketcherObj.cpp` | 草图**数据**：几何增删改、外部参考、约束增删改、GCS 求解、采样缓存（`mGeoSegment`）、平面与变换、`toShape()/makeDone()`、网格/吸附/隐藏这些随文档保存的显示数据。**不再是 `EventWidget`**，不依赖渲染器，可以在没有 GUI 的情况下加载/求解/写出。 |
| `SketcherObjWidget : EventWidget` | `SketcherObjWidget.h` + `SketcherObjWidgetInteraction.cpp`（事件、拾取、吸附、拖拽、选择）+ `SketcherObjWidgetDraw.cpp`（绘制、网格与轴、尺寸标注与命中、约束图标） | 草图**编辑面**：选择/预选、拖拽、拾取、吸附、绘制、标注布局与交互。它持有 `SketcherObj*`（不拥有），所有几何/约束改动都走 `SketcherObj` 的接口。 |
| 共用类型 | `SketcherTypes.h` | `SelectGeoId` / `NoGeoId` / `DrawOption`（颜色与线宽）。 |

所有权：`SketcherFeature` 持有草图数据（`shared_ptr`），编辑 widget 在**第一次打开草图编辑时创建**
（`SketcherFeature::ensureSketcherWidget()`），在此之前（例如刚读取文档）不存在——所以“没有 widget”
就是“没在编辑”的正常状态。查找入口：`SketcherObjManager::GetCurrentActiveSketcherWidget()`。

依赖方向是单向的（下层不知道上层）：

- `SketcherObj` **不 include widget 头、不持有 widget 指针、不调用 widget，也没有 friend**。它对上面的
  世界只发布两样东西：只读数据访问（`geometries() / constraints() / geometryCount() / plane() /
  isConstructionGeometry() / findSegment() / segmentOf()` 等）和 solver 的拖拽接口
  （`beginMove() / moveGeometries() / takeSolvedGeometry()`）。
- 反过来，它用两个**变更计数**告诉外面“有什么变了”：`planeRevision()`（`setPlane` 时 +1）与
  `constraintRevision()`（约束增删、几何删除连带删约束时 +1）。这只是数据，模型并不关心谁在读。
- `SketcherObjWidget` 在每帧 `onUpdate()` 里 `syncWithSketch()` 比对自己上次看到的计数：平面变了就
  重新把相机对到平面上，约束集合变了就 `pruneConstraintLayout()` 清掉失效的标注布局。结束编辑也由
  widget 自己发起（`finishEdit()` = `SketcherObj::makeDone()` + `leaveEdit()`），不需要模型回调过来。

> 下面章节里的 `![...](images/*.svg)` 示意图画的是重构**之前**一个类包办全部职责的样子，尚未重画；
> 表里标注了每个成员现在归谁。

---

## 1. 一句话总览

**`SketcherObj` 是草图的 2D 数据与求解**：它持有草图平面、2D 几何与约束，把约束交给 GCS 求解，
并在完成编辑时把草图几何输出为可用于 3D 建模的拓扑（wire / compound）。鼠标/键盘事件、选择、
拖动与绘制由 **`SketcherObjWidget`**（一个 `EventWidget`）承担，它把“选择 → 拖动 → 求解 → 重绘”
翻译成对 `SketcherObj` 的调用。

FreeCAD 中这一套逻辑被拆在多个类里（`SketchObject` 管数据与求解、`ViewProviderSketch` 管拾取绘制、
`DrawSketchHandler*` 管工具交互、`Constraint` 管约束类型）；本项目对应 `SketcherObj` +
`SketcherObjWidget` + `Moon/Interactive/Widgets/DrawSketchHandler*`。

![SketcherObj 职责与数据流](images/sketcherobj_architecture.svg)

---

## 2. 工作职责（按模块）

### 2.1 数据持有

| 成员 | 类型 | 职责 | 对应 FreeCAD |
| --- | --- | --- | --- |
| `mPlane` | `SketcherPlane2D` | 草图平面（origin / xAxis / yAxis / normal） | `SketchObject.Placement` + 平面属性 |
| `planeTransform` | `Base::Matrix4D` | 2D 草图坐标 → 3D 世界坐标的基矩阵 | `Placement` 作用在几何上 |
| `mGeoList` | `vector<unique_ptr<Part::Geometry>>` | 草图几何（线/圆弧/圆/样条/点），平面局部坐标 | `SketchObject.Geometry` |
| `mConstraintList` | `vector<Constraint*>`（裸指针） | 约束列表 | `SketchObject.Constraints` |
| `solvedSketch` | `Sketcher::Sketch` | GCS 求解器封装 | `Sketcher::Sketch`（内部求解器） |
| `mGeoSegment` | `unordered_map<Geometry*, CurveSegment>` | 每条曲线的离散折线 + 参数 + Start/End/Center 关键点缓存（**留在 `SketcherObj`**：这是数据，绘制/拾取/吸附都读它） | `ViewProviderSketch` 内部的显示缓存 |
| `selectIds / preSelectGeoId` | `vector<SelectGeoId>` / `SelectGeoId` | 选中项与悬停预选项（GeoId + 点类型）→**现在在 `SketcherObjWidget`** | `ViewProviderSketch` 选择逻辑 |
| `basedTopoShape / doneFaceShape` | `Part::TopoShape` | 编辑前基准 / 编辑完成后输出的拓扑（**仍在 `SketcherObj`**） | `SketchObject` 的 Shape 输出 |

### 2.2 职责分解

1. **坐标转换**：`updateTransform()` 用平面基向量构造列主序矩阵；
   `getMouseHitSketchPlanePoint()` 用鼠标射线打平面得到 2D 草图坐标（量化到 0.01）；
   `testSelect()` / `snapPoint()` 把 2D 坐标经 `planeTransform × 视口矩阵` 变成屏幕坐标做距离判定。
2. **选择交互状态机**：`Stop / Hot / OperationGeo / DragRect` 四个状态在
   `onMouseMove / onLeftMousePressed / onLeftMouseReleased` 中转移；`Ctrl` 切换
   `AppendSelect / OverrideSelect`，`DELETE` 删除选中。
3. **拖动几何**：`moveGeo()` 按 `PointPos`（None/StartP/EndP/CenterP）修改线端点、圆弧范围、
   圆半径/圆心等；拖动后触发 `solve()`。
4. **约束求解**：`solve()` 把几何与约束喂给 GCS，`retrieveSolverDiagnostics()` 记录冲突/冗余/过约束，
   成功后从求解器重建全部几何。
5. **几何管理 API**：`addGeometry` / `deleteGeometry(s)` / `replaceGeometry(ies)` / `trim` /
   `fillet` / `addSymmetric` / `getSymmetric`，索引为 `GeoId`。
6. **输出拓扑**：`toShape()` 把边连成 wire（`BRepBuilderAPI_MakeWire` + `ShapeFix_Wire`），
   多 wire 合成 compound，再挂 `planeTransform`。
7. **绘制反馈**：`draw()` 画平面坐标轴、所有曲线的离散折线、关键点与选中高亮。
8. **生命周期**：`setPlane` / `fitCamera` / `beginEdit` / `makeDone` 控制进入/退出草图编辑
   （进入时切正交相机并摆正视角）。
9. **与工具协作**：`onUpdate()` 检测激活的 `DrawSketchHandler`，有激活工具时本对象不抢事件。

---

## 3. 核心流程

### 3.1 编辑生命周期

```text
setPlane(plane) ──► fitCamera()          // 正交相机、看向平面法线
beginEdit() ──────► isInEdit = true      // 进入编辑
    │  … 几何/约束/拖动/求解循环 …
makeDone() ───────► toShape() → doneFaceShape   // 输出拓扑、恢复旋转、停用工具
```

### 3.2 交互状态机

![SketcherObj 选择/编辑状态机](images/sketcherobj_state_machine.svg)

关键点：

- 只有 `!isHaveActiveHandler && isInEdit` 时状态机才运转（避免和画线/画圆弧工具冲突）；
- `OperationGeo` 状态下拖动走 GCS DogLeg：`initMove` + `moveGeometries`，每次成功用
  `extractGeometry()` 原位替换几何（圆/弧本体拖半径时圆心作为锚点）；
- 框选（`DragRect`）判定：整条曲线的离散点都在框内才选中整条，否则只选中框内的关键点。

### 3.3 solve 流程（当前实现）

```text
solvedSketch.resetInitMove()
  → setUpSketch(GeoList, mConstraintList)
  → retrieveSolverDiagnostics()          // 冲突 / 冗余 / 过约束标记
  → 若正常：solvedSketch.solve()
  → 若成功（err==0）：清段缓存 → mGeoList.clear()
            → extractGeometry() 原位回填（mConstraintList 不参与重建）
  → 返回 err（0 / -1..-5）
```

注意：约束与选择仍依赖“GeoId 顺序不变”的隐含假设；求解结果按原索引回填，
因此顺序必须由 `Sketch::extractGeometry()` 保持。

### 3.4 吸附（snapPoint）优先级

`snapPoint()` 是工具（`DrawSketchHandler`）与拾取共用的一条吸附链，全部判定都在**屏幕像素
距离阈值 10px** 内完成，命中即返回、不再继续尝试后面的优先级：

```text
① 离散关键点    曲线/圆弧/圆的 Start/End/Center（圆心为 PointPos::mid）等 mGeoSegment.sepoints
② 原点与坐标轴  原点 → X 轴 → Y 轴（屏幕距离 <= 10px）
③ 曲线本体      点到离散折线段的最小距离（点会投影到曲线上）
④ 网格交叉点    仅当 m_drawGrid && m_snapToGrid 且离网格交点 <= 10px 时吸附（最低优先级）
```

网格吸附要点：

- 网格步长与 `drawBackground()` 完全一致：两者都取 `SketcherObj::gridView()` 里的 `step`
  （按当前相机算“约 40px 一条次网格线”的目标间距，再向上取到 1/2/5×10ⁿ 的“好看”步长），
  所以画出来的格子和吸附到的点永远是同一个点阵；
- 候选点为 `round(pos / step) * step` 的交叉点，是否吸附只按该点与鼠标的屏幕像素距离判断，
  不随缩放改变手感；
- 由草图面板的 **Snap To Grid** 开关控制（默认关），状态存于 `SketcherObj::m_snapToGrid`；
- 实现位于 `SketcherObjDraw.cpp::snapToGridPoint()`，调用挂在 `snapPoint()` 所有既有规则之后。

---

## 4. 功能清单（当前实现状态）

| 模块 | 功能 | 状态 | 说明 |
| --- | --- | --- | --- |
| 生命周期 | setPlane / beginEdit / makeDone / fitCamera | ✅ 完整 | fitCamera 硬编码 `SetSize(100)`，不按草图包围盒 |
| 几何 | addGeometry / getGeometry / deleteGeometry(s) | ✅ 完整 | unique_ptr 持有，无泄漏 |
| 几何 | replaceGeometry(ies) | ✅ 完整 | 同步维护 mGeoSegment |
| 几何 | trim（两参数裁剪/删除） | ✅ 基本完整 | 依赖 `GeomCurve::createArc` 支持 |
| 几何 | fillet / chamfer（含 seekTrimPoints） | ✅ 基本完整 | 支持线-线；其它组合待验证 |
| 几何 | addSymmetric / getSymmetric | ✅ 完整 | |
| 几何 | isClosedCurve | ✅ 完整 | 圆/椭圆/周期样条 |
| 拖动 | moveGeo：线/圆弧/圆/样条 | ✅ 完整 | |
| 拖动 | moveGeo：GeomArcOfConic | ❌ 占位 | 分支为空，圆锥弧拖不动 |
| 约束 | addConstraint / findConstraint / setDatum / 删除几何联动清理 | ✅ 基本完整 | 尺寸约束编辑失败自动回滚 |
| 约束 | 析构释放 mConstraintList | ✅ 完整 | 析构统一 delete |
| 求解 | solve + 诊断（冲突/冗余/过约束） | ✅ 完整 | 返回 err（0 / -1..-5），成功原位回填几何 |
| 选择 | 点选 / 框选 / 悬停 / 追加选择 / DELETE | ✅ 完整 | |
| 选择 | End 状态 / hasClickSelected / getPickGeoIndex | ❌ 冗余 | 已定义未使用 |
| 吸附 | 端点/圆心/原点/坐标轴/曲线上/网格点 | ✅ 基本 | 网格吸附可开关、最低优先级；无中点/交点等 |
| 输出 | toShape → wire/compound + transform | ✅ 完整 | 不生成 Face；闭合校验弱 |
| 绘制 | 平面轴 / 曲线 / 选中高亮 / 框选矩形 / 构造虚线 / 约束与尺寸标注 | ✅ 基本 | 详见 [SketchRendering.md](SketchRendering.md) |
| 撤销 | undo / redo | ❌ 缺失 | |

---

## 5. Plan to do（按优先级）

### P0 —— 正确性与资源

- [x] `solve()` 返回真实错误码（过约束 -4 / 冲突 -3 / 冗余 -2 / 求解失败 -1 / 畸形 -5）；
- [x] `~SketcherObj()` 释放 `mConstraintList`；
- [x] 拖动改走 GCS DogLeg（`initMove`/`moveGeometries`），成功用 `extractGeometry()` 原位回填，
      不再每帧全量删除重建。
- [ ] 求解失败时把冲突/冗余约束反馈到 UI（当前只有字段与返回码）。

### P1 —— 功能补全（对照 FreeCAD）

- [ ] `moveGeo()` 补齐 `GeomArcOfConic`（椭圆弧端点/圆心拖动）；
- [ ] `toShape()` 增加闭合校验，闭合草图生成 Face（供 Pad 直接使用），并接入
      `ShapeAnalysis_FreeBounds::ConnectEdgesToWires` 这类更稳的连线方案；
- [ ] `fitCamera()` 按 `mGeoList` 包围盒自适应视口尺寸与中心；
- [ ] 吸附增强：中点、交点、曲线端点自动约束（对照 FreeCAD `autoConstraint` / `GeoEnum`）；
- [ ] 约束编辑 UI 接入（距离/角度/共线/相切等，当前只有数据层）。

### P2 —— 体验与清理

- [x] 网格吸附：自适应步长 + 10px 像素阈值 + UI 开关（SketchTaskDialog → Snap To Grid）；
- [ ] 去掉 `End` / `hasClickSelected` / `getPickGeoIndex` 等未用字段；
- [ ] 交互坐标去掉 0.01 量化或改为可配置网格吸附；
- [x] 绘制补约束符号、尺寸、构造线样式（construction）——见
      [SketchRendering.md](SketchRendering.md)；
- [ ] undo/redo 栈（FreeCAD 用 Property + Transaction，本项目可先做快照式）；
- [ ] 与 `DrawSketchHandler` 的提交回调节点整理（当前靠 `isHaveActiveHandler` 隐式协作）。

---

## 6. 参考源码

- 本类：`Moon/Sketcher/SketcherObj.h`、`Moon/Sketcher/SketcherObj.cpp`
- 交互框架：`Moon/Interactive/EventWidget.*`、`Moon/Interactive/Widgets/DrawSketchHandler*`
- 工具 UI：`Moon/editor/Toolbar/sketchToolbar.cpp`
- FreeCAD 对照：
  `D:\Project\C++\FreeCAD\src\Mod\Sketcher\App\SketchObject.h`（及
  `SketchObjectGeometry.cpp / SketchObjectConstraints.cpp / SketchObjectOperations.cpp`）、
  `D:\Project\C++\FreeCAD\src\Mod\Sketcher\Gui\ViewProviderSketch.cpp`、
  `D:\Project\C++\FreeCAD\src\Mod\Sketcher\Gui\DrawSketchHandler*.h`
- 关联文档：`docs/SketchModelingWidget.md`、`docs/SketchModelingWidgetArchitecture.md`、
  `docs/SketchWidgets/DrawSketchHandler*.md`
