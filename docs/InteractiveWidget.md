# 交互 Widget 设计实现架构（以 ClipPlane 剖切控件为例）

> 本文基于 `Moon/Interactive/EventWidget.*`、`Moon/Interactive/Im3DRenderer.*`、`Moon/Interactive/GizmoBehaviour.*`、`Moon/Interactive/Widgets/ClipPlane.cpp` 与 `Moon/Interactive/Im3DType.*` 的实际代码整理。

---

## 1. 概述

交互 Widget 是编辑器里的**可交互 3D 控件**：固定屏幕尺寸的 gizmo 网格 + 鼠标拾取 + 状态机驱动的拖拽/旋转。典型例子：

- `ClipPlane`：剖切平面控件（本文主线示例）——沿轴拖、在平面内拖、绕轴旋转，实时驱动 GPU 截面；
- `AxisTranslationWidget` / `ArrowRotateWidget` / `PadTaskWidget`：建模工具的平移/旋转手柄。

一个 Widget 由四层协作完成：

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 事件层 | `EventWidget` + `RenderWindowInteractor` | 把 Qt 鼠标/键盘事件分发给 widget 的虚函数 |
| 绘制层 | `ImRenderer` | 立即模式 3D 绘制（网格/线/点）与 gizmo 拾取 |
| 几何层 | `GizmoBehaviour` | 射线与轴/平面/旋转平面的求交算法 |
| 业务层 | `ClipPlane` 等子类 | 状态机、控件外观、把交互结果写回场景/渲染器 |

现有控件可以归为两种风格，绘制与拾取方式不同：

| 风格 | 代表 | 几何载体 | 绘制路径 | 拾取方式 |
| --- | --- | --- | --- | --- |
| ClipPlane 风格 | `ClipPlane` | `PolygonMesh`（块结构） | `drawOneMesh` → `drawMeshList`（FIXED_SCALE） | GPU 颜色编码（`PickingRenderPass`） |
| WidgetViewData 风格 | `AxisTranslationWidget` / `ArrowRotateWidget` / `PadTaskWidget` | `TriangleFace` / `Edge` / `VertexPoint` | `drawTriangleList` → `vertexData`（立即模式） | CPU 射线求交（`hitFace` / `hitEdge` / `hitPoint`） |

![交互 Widget 整体架构与数据流](images/widget_architecture.svg)

---

## 2. 每帧驱动

编辑器每帧按以下顺序驱动：

```cpp
ImRenderer::newFrame(sceneView);   // 清空上一帧绘制列表、记录当前视图
// ... 业务层每帧渲染场景 ...
ImRenderer::endFrame();
//   └─ drawWidgets()：遍历 mGizmoWidgets，逐个调用 widget->update()
//   └─ drawMesh()：提交 drawMeshList（含 gizmo 网格）
```

`EventWidget::update()`：

```cpp
void EventWidget::update()
{
    mCurrentFrame = (mCurrentFrame + 1) % 1000000;   // 帧计数，用于鼠标事件节流
    if (mActive && mVisible)
    {
        onUpdate();                                   // 子类绘制控件 + 更新逻辑
    }
}
```

`EventWidget` 构造时把自己注册进 `ImRenderer::instance()`（`addGizmoWidget`），因此所有 widget 共享同一个绘制器与拾取缓冲。

---

## 3. 事件系统

### 3.1 Qt → RenderWindowInteractor

`RenderWindowInteractor::ReceiveEvent(QEvent*)` 接收 Qt 鼠标事件，转换成内部事件并广播：

```cpp
if (t == QEvent::MouseButtonPress || t == QEvent::MouseButtonRelease ||
    t == QEvent::MouseButtonDblClick || t == QEvent::MouseMove || t == QEvent::HoverMove)
{
    QMouseEvent* e2 = static_cast<QMouseEvent*>(e);
    SetEventInformationFlipY(e2->x(), e2->y(), ctrl, shift, 0, dblClick);
    if (t == QEvent::MouseMove || t == QEvent::HoverMove)
        InvokeEvent(ExecuteCommand::MouseMoveEvent, e2);
    else if (t == QEvent::MouseButtonPress)
        // switch(e2->button()) → InvokeEvent(LeftButtonPressEvent / ...)
}
```

`RenderWindowInteractor` 是**全局单例事件中心**：它维护一张观察者表（`EventObject::AddObserver(event, command, priority)`），`InvokeEvent` 按事件类型和优先级分发。

### 3.2 Interactor → EventWidget

`EventWidget` 构造时把自己挂到 Interactor 上，并通过 `CallbackMapper` 建立「事件 → 回调」映射：

| ExecuteCommand 事件 | WidgetEvent | 触发虚函数 |
| --- | --- | --- |
| `LeftButtonPressEvent` | `Select` | `onLeftMousePressed()` |
| `LeftButtonReleaseEvent` | `Select3D` | `onLeftMouseReleased()` |
| `RightButtonPressEvent` | `EndSelect` | `onRightMousePressed()` |
| `RightButtonReleaseEvent` | `Completed` | `onRightMouseReleased()` |
| `MouseMoveEvent` | `Move3D` | `onMouseMove()` |
| `KeyPressEvent` / `KeyReleaseEvent` | — | `onKeyPress()` / `onKeyRelease()` |

鼠标移动做了**帧节流**：静态入口 `EventWidget::MouseMove` 只在 `mCurrentFrame != mPreFrame` 时才调用 `onMouseMove()`，避免同一帧内重复处理。

### 3.3 直接订阅观察者

除了 CallbackMapper，widget 也可以直接向 Interactor 注册自己的回调。ClipPlane 用它来选择被剖切的模型：

```cpp
clickObserver = mSelf->Interactor->AddObserver(
    ExecuteCommand::LeftButtonReleaseEvent,
    this, &ClipPlane::ClipPlaneInternal::onMouseLeftClick, 0.0f);
```

`onMouseLeftClick` 拾取当前选中的 Actor，若带 `Model Renderer`，则用其世界包围盒初始化 widget 的位置与大小（`setupBox`）。

---

## 4. 绘制系统

### 4.1 两条绘制路径（灵活混用）

widget 的绘制并不局限于一种方式：**同一个 widget 里可以自由混用两条路径**，它们在本帧结束时分别提交。

| 路径 | 调用方式 | 数据去向 | 提交时机 |
| --- | --- | --- | --- |
| 网格路径 | `drawOneMesh(...)` / `drawOneFixScaleMesh(...)` | `drawMeshList`（预构建 `PolygonMesh`） | `endFrame → drawMesh()` |
| 立即模式路径 | `drawLine / drawPoint / drawTriangleList` | 顶点列表 `vertexData[0] / [1]`（按图层 + 图元类型） | `endFrame → drawSort() / drawUnsort()` |
| 2D / 覆盖层 | ImGui 等 | 独立 UI 上下文 | 独立提交 |

`endFrame()` 的提交顺序为 `runDrawTask → drawWidgets → drawMesh → drawSort → drawUnsort`：gizmo 网格先画，立即模式图元后画（排序 / 未排序两层，支持透明度与深度测试）。

以 ClipPlane 为例，同一个 `onUpdate()` 里就同时用到了三种方式：

- **网格路径**：`drawOneMesh(center, rotation, {0.1,0.1,0.1}, "TransformAxis")` 绘制三轴手柄 / 旋转环（固定屏幕尺寸）；
- **立即模式路径**：`drawLine` 画旋转圆弧、包围盒线框、圆心连线，`drawAlignedBox` 画盒线；
- **2D 覆盖**：`ImGui::GetForegroundDrawList()->AddText(...)` 显示旋转角度。

所以“有的部分是立即绘制、其它部分是 PolygonMesh”正是这套设计有意为之：需要固定缩放 / 深度 / 排序的实体手柄走 mesh 列表，临时路径与装饰线走立即模式，两者在 `onUpdate` 里按需混用。

### 4.2 立即模式 API

`ImRenderer` 提供类似立即模式 GUI 的 3D 绘制接口，widget 在 `onUpdate()` 里直接调用：

- `drawLine(a, b, size, color)` / `drawPoint(pos, size, color)` / `drawTriangleList(...)`：基础图元；
- `drawOneMesh(translation, rotation, scale, "TransformAxis")`：按名字绘制预构建的 gizmo 网格；
- `pushColor / popColor`、`pushSize / popSize`、`pushMatrix / popMatrix`：绘制状态栈；
- `getFrameParam()`：返回 `FrameParam`（eye、rayOrigin、rayDirection、cursor、viewport 尺寸、投影类型等），供几何计算使用。

立即模式调用按“图层 + 图元类型”写入本帧的顶点列表，`endFrame()` 里由 `drawSort()` / `drawUnsort()` 统一提交；网格调用进入 `drawMeshList`，由 `drawMesh()` 提交。所有列表在 `newFrame()` 清空。

### 4.3 Gizmo 网格的块结构

gizmo 网格（如 `TransformAxis()`，见 `Im3DType.cpp`）是一个 `PolygonMesh`，由若干**块（block）**组成：

```cpp
poly.addModel(cil, Identity, color);
poly.switchNextBlock({1,0,0,1}, "XAxis");   // 开启新块并命名
poly.addModel(cil, ...);
// ...
poly.addCell(cell);                          // 程序化多边形面
poly.switchNextBlock({0,1,0,1}, "YPlane");
```

- 每个块有一个 **blockId**（在块内顶点的 `w` 分量里），用于拾取解码；
- `getBlockId(name)` 按块名查 id，`setBlockColor(id, color)` 改颜色（悬停/激活高亮）；
- 整个网格有一个 **polygonId**（`setId`），`isSelectPolygon` 用它区分不同 gizmo。

### 4.4 固定屏幕尺寸（FIXED_SCALE）

gizmo 用 `GizmoCell.ovfx` 的 `FIXED_SCALE` 特性绘制，保证控件在任意相机距离下占用恒定像素数。原理与公式的详细推导见 [ClipPlane.cpp](../Moon/Interactive/Widgets/ClipPlane.cpp) 中 `ComputeGizmoFixedScaleRatio` 上方的注释：缩放后 1 个网格单位恒等于 200 像素。

这也是 ClipPlane 旋转圆弧必须用同一个 ratio 缩放的原因——否则控件固定大小、圆弧却随距离缩放，两者会脱节。

WidgetViewData 风格控件不走 FIXED_SCALE 着色器，需要在 C++ 侧用同一个 ratio 手动缩放（见 4.5）。

### 4.5 WidgetViewData 风格控件（以 AxisTranslationWidget 为例）

`WidgetViewData` 是另一套控件几何容器：`TriangleFace`（本地三角形 + 颜色 + 4×4 model 矩阵）、`Edge`（线段）、`VertexPoint`（点）。`AxisTranslationWidget` 用它承载倒圆角半径箭头：

**构造**：加载 `Arrow_Translate` 模型，把顶点乘以 `Scaling(10)` 写入 `TriangleFace`（本地坐标，绕 gizmo 原点分布），再调用 `setTriangleFace("Arrow", f, color)` 注册。

**绘制**：`onUpdate` 对每个 `TriangleFace` 压入 model 矩阵后提交：

```cpp
renderer->pushMatrix(faces[i].model);            // model = R | t
renderer->drawTriangleList(faces[i].faces, 1.0, faces[i].color);
renderer->popMatrix();
```

`model` 的旋转与平移由外部设置：`setUpOrigin` / `setUpDir`（旋转矩阵 `RotationMatrixZ(normal)`）/ `setLength` 更新平移列；`setUpScale` 通过 `setTriangleFaceScale` 直接缩放本地顶点（模型相对大小）。

**固定屏幕尺寸**：与 ClipPlane 相同的 ratio 公式，在 C++ 侧手动缩放：

```cpp
const float ratio = ComputeFixedScaleRatio(*m_sceneView, mInternal->center);
if (mInternal->mRefScaleRatio < 0.0f) mInternal->mRefScaleRatio = ratio;  // 首帧锁定基准
const float scale = ratio / mInternal->mRefScaleRatio;                    // 之后屏幕尺寸恒定
Eigen::Matrix4f scaleMat = Eigen::Matrix4f::Identity();
scaleMat(0, 0) = scaleMat(1, 1) = scaleMat(2, 2) = scale;
renderer->pushMatrix(faces[i].model * scaleMat);                          // 绕 gizmo 原点缩放
```

`scale = ratio / mRefScaleRatio` 让控件锁定在“出现时”的大小，此后随相机距离等比缩放、屏幕尺寸不变；正交相机 ratio 为常量，scale 恒为 1。**绘制与拾取必须使用同一个 scale**（见 5.3），否则命中会错位。

---

## 5. 拾取系统

拾取分为「写入」与「查询」两步。

### 5.1 写入：拾取 Pass 颜色编码

`PickingRenderPass`（渲染顺序 `Last`）每帧把 gizmo 网格画进**独立的拾取 framebuffer**（`actorPickingFramebuffer`）。核心是 `ImRenderer::drawMeshPick()`：以 `PICKING_PASS` 特性渲染 `drawMeshList`，片元输出编码：

```glsl
// GizmoCell.ovfx（PICKING_PASS）
fResult = vec4(0, int(round(fs_in.pos.w)) / 255.0, polygonId / 255.0, 254.0 / 255.0);
//           R=0      G=blockId（顶点 w）          B=polygonId           A=254（gizmo 标记）
```

该 Pass 只在“非相机操作、非正在拖拽”时启用（`SceneView::Update`），因此平时不产生额外开销。

### 5.2 查询：读回像素 → 解码 → 命中

`SceneView::HandleActorPicking` 读取光标处像素并解码：

```cpp
// PickingRenderPass::ReadbackPickingResult
if (pixel[3] == 254)   // gizmo 命中
{
    uint32_t polygonID = pixel[2];
    uint32_t blockID   = pixel[1];
    gizmoInstance.selectPolygon(polygonID, blockID);   // 写回 ImRenderer
    isSelected = true;
}
```

`selectPolygon(pid, bid)` 把当前命中结果存进 `ImRenderer` 的 `selectPolygonId / selectBlockId`。widget 随后在 `onMouseMove` 里逐块查询：

```cpp
bool hit = renderer->isSelectPolygon("TransformAxis", table[i].blockName);
// 等价于：TransformAxis().getId() == selectPolygonId && getBlockId(blockName) == selectBlockId
```

![Gizmo 拾取流程](images/widget_picking.svg)

### 5.3 CPU 射线拾取（WidgetViewData 风格）

WidgetViewData 风格控件不用 GPU 颜色编码，而是在 `onMouseMove` 里直接对 CPU 端三角形做射线求交：

```cpp
auto ray = m_sceneView->GetMouseRay();                 // 世界空间鼠标射线
mInternal->curHitTarget = mInternal->viewData.hitFace(it, mInternal->mLastScale);
```

`WidgetViewData::hitFace(ray, scale)` 把每个 `TriangleFace` 的本地三角形用 `model * Scale(scale)` 变换到世界空间，逐三角形做 Möller–Trumbore 求交（`Intersect`），取最近命中并返回块的**名字**（如 `"Arrow"`）。控件拿到名字后驱动状态机：命中 `"Arrow"` → `Hot` → 按下进入 `AxisT`。

`scale` 参数与绘制时的缩放一致（见 4.5），保证“画在哪、命中就在哪”。`hitEdge` / `hitPoint` 则把线/点经视口矩阵投影到屏幕，按屏幕距离命中，适合 2D 手柄。

两种拾取方式对比：

| GPU 颜色编码（ClipPlane 风格） | CPU 射线求交（WidgetViewData 风格） |
| --- | --- |
| 每帧把 gizmo 画进拾取 framebuffer，读 1 像素解码 polygonId/blockId | 直接对 CPU 端三角形做射线求交，返回块名 |
| 适合复杂、多块的 `PolygonMesh` gizmo | 适合简单三角面控件，无额外 GPU 开销 |
| 命中信息是数字 id，需要 `isSelectPolygon` 再比对 | 命中即名字，直接驱动状态机 |

---

## 6. ClipPlane 状态机（示例）

ClipPlane 用五个状态管理交互，手柄与操作的对应关系由一张表定义：

| 手柄块（blockName） | 交互类型 | 触发几何行为 |
| --- | --- | --- |
| `XArrow` / `YArrow` / `ZArrow` | 沿轴平移（`AxisT`） | `GizmoAxisTranslate` |
| `XPlane` / `YPlane` / `ZPlane` | 平面内平移（`PlaneT`） | `GizmoPlaneTranslate` |
| `XAxis` / `YAxis` / `ZAxis`（圆柱） | 绕轴旋转（`AxisR`） | `GizmoAxisRotate` |

![ClipPlane 交互状态机](images/widget_state_machine.svg)

### 6.1 状态转移

**Stop（初始）**

`onMouseMove` 遍历 9 个手柄块，`isSelectPolygon` 命中任一 → `mPickMesh = i`、`mState = Hot`、`setBlockColor(hotColor)` 高亮。

**Hot（悬停）**

- 再次移动：若不再命中任何块 → 回到 `Stop`，恢复原色；
- 左键按下：根据 `table[mPickMesh].meshId` 决定进入哪个拖拽状态，并初始化对应 gizmo 行为：

```cpp
// 沿轴拖：把轴与起点交给 transLatePick
m_internal->transLatePick.startPick(axis, m_internal->center);
mState = AxisT;

// 平面内拖：构造平面方程（法线 n，过 center）
float w = -m_internal->center.dot(normal);
m_internal->planeTPick.startPick({n.x, n.y, n.z, w}, m_internal->center);
mState = PlaneT;

// 绕轴旋转：绑定旋转轴 + 参考方向 + 参考点
m_internal->axisRPick.startPick(rotationAxis, m_internal->center, refDir, center + refDir);
mState = AxisR;
```

**AxisT / PlaneT / AxisR（拖拽中）**

`onMouseMove` 用 `getFrameParam()` 的射线原点/方向驱动 gizmo 行为，并把结果写回内部状态：

```cpp
// 平移：直接改 center；旋转：更新 xAxis/yAxis/zAxis 之一并正交化
m_internal->transLatePick.apply(rayDir, rayOrigin, m_internal->center);
m_internal->updateEngineUbo = true;    // 标记：下一帧同步平面到渲染器
```

旋转状态下 `onUpdate` 额外绘制角度文字、旋转路径圆弧（半径用 FIXED_SCALE ratio 缩放，见 §4.3）。

**松开（→ Hot）**

`onLeftMouseReleased` 恢复高亮色，并按 `updateFlag` 条件调用 `updateSection()` 刷新截面。

### 6.2 与截面渲染的联动

ClipPlane 只负责“交互”，不直接生成几何。拖拽时置 `updateEngineUbo = true`，`onUpdate` 中把平面写入渲染器：

```cpp
auto& feature = m_sceneView->GetRenderer().GetFeature<EngineBufferRenderFeature>();
feature.SetClipPlane(zAxis.x, zAxis.y, zAxis.z, -zAxis.dot(center));
```

GPU 侧（`SectionCapRenderPass` / `SectionContourRenderPass`）每帧读 `ubo_plane` 生成截面与截线——交互与渲染解耦，这也是“拖动即实时更新”的原因。原理见 [SectionRendering.md](SectionRendering.md)。

---

## 7. WidgetViewData 风格控件：使用与状态机

### 7.1 使用流程（以 FilletTask 使用 AxisTranslationWidget 为例）

倒圆角任务为两条边各创建一个半径箭头，拖拽箭头改变 `feature->radius`：

```cpp
axisBehaviour1 = new AxisTranslationWidget("fillet");
axisBehaviour1->setUpScale(feature->len);                       // 相对模型大小的基数
axisBehaviour1->setLength(feature->radius);                     // 当前长度 = 半径
axisBehaviour1->setUpOrigin(origin1.x, origin1.y, origin1.z);   // 锚点
axisBehaviour1->setUpDir(dir1.x, dir1.y, dir1.z);               // 轴向（箭头朝向）
axisBehaviour1->AddObserver(AxisTranslationEvent::LengthChange,
                            self, &FilletTask::onWidgetLengthInvoke1);
```

事件回调里读取长度并同步业务与 UI：

```cpp
void FilletTask::onWidgetLengthInvoke1()
{
    mInternal->feature->radius = mInternal->axisBehaviour1->getLength();
    mInternal->radiusProp->updateWidgetValue(mInternal->feature->radius);
}
```

反向同步（属性面板改半径 → 更新手柄）：`setParamValue("Fillet:Radius", v)` 里设置 `feature->radius` 后调用 `axisBehaviour1->setLength(v)` / `axisBehaviour2->setLength(v)`。

通用使用模式：

1. 构造（自动注册到 `ImRenderer` 与 `RenderWindowInteractor`）；
2. `setUpOrigin` / `setUpDir` / `setUpScale` / `setLength`（或 `setAngle`）设置初始几何；
3. `AddObserver(XXXEvent::YYYChange, ...)` 订阅交互结果；
4. 交互中/结束时在回调里用 `getLength()` / `getAngle()` 读值；
5. 业务侧改值后用 `setLength` / `setAngle` 反向同步手柄。

### 7.2 状态机

三个控件共用轻量状态模式：**Stop（未命中）→ Hot（悬停）→ 操作态（按下拖拽）→ Hot（松开）**。命中检测用 §5.3 的 CPU 射线拾取（`hitFace` / `hitPoint`）。

| 控件 | 命中手柄 | 操作态 | 拖拽几何 | 交互事件 |
| --- | --- | --- | --- | --- |
| `AxisTranslationWidget` | Arrow（箭头） | `AxisT` | 沿轴平移 center（`transLatePick`） | `LengthChange` |
| `ArrowRotateWidget` | Arrow（箭头） | `Rotate` | 绕轴旋转 curPos（`rotatePick.applyPos`） | `AngleChange` |
| `PadTaskWidget` | Arrow（箭头）/ Point（点） | `AxisT` / `Rotate` | 平移 center / 旋转 rotDir | `LengthChange` / `AngleChange` |

![WidgetViewData 风格控件状态机](images/widget_widgetdata_states.svg)

**AxisTranslationWidget**：`Stop` 下 `hitFace` 命中 `"Arrow"` → `Hot`（红色高亮）；`Hot` 下移出 → `Stop`，按下 → `AxisT`（品红 + `transLatePick.startPick`）；`AxisT` 中 `onMouseMove` 用 `transLatePick.apply` 更新 center 并刷新箭头位置，`mImInvoke` 为真时每帧发 `LengthChange`；松开 → `Hot`（恢复白色），非 ImInvoke 时补发 `LengthChange`。

**ArrowRotateWidget**：`Stop`/`Hot` 同上；`Hot` 按下 → `Rotate`（`rotatePick.startPick(axis, center, dir, curPos)`）；`Rotate` 中 `applyPos` 让 curPos 绕 center/axis 旋转，箭头朝向跟随；松开 → `Hot` 并发 `AngleChange`。外部用 `setUpRotateCenter/Axis/OriginPos` 设置，`setAngle` / `getAngle` 读写角度。

**PadTaskWidget**：箭头与 Point 两个手柄。`Stop` 下先 `hitFace` 判 Arrow，未命中再 `hitPoint` 判 Point；`Hot` 下都未命中 → `Stop`；按下 Arrow → `AxisT`（平移 center），按下 Point → `Rotate`（旋转 rotDir，Point 沿圆环移动）；分别发 `LengthChange` / `AngleChange`。`onUpdate` 每帧把 Point 放到缩放后的圆上（见 4.5），保证拾取与绘制一致。

### 7.3 事件与取值约定

- `mImInvoke`（默认 true）：为真时拖拽中每帧触发事件（实时预览）；`setImmediateInvoke(false)` 则只在松开时触发一次；
- 读：`getLength()`（center 相对锚点的距离）、`getAngle()`（旋转角）；写：`setLength` / `setAngle`（UI 反向同步）；
- 业务侧不要直接改 widget 内部状态，统一通过事件回调 + getter 取值，避免与拖拽中的更新互相覆盖。

---

## 8. 几何交互算法（GizmoBehaviour）

### 8.1 GizmoAxisTranslate（沿轴拖）

把鼠标射线投影到轴上，取射线上离轴最近的点，维护按下时的初始偏移：

```cpp
// 射线 P(t) = eye + t * ray，轴 center + s * axis
// 求 min|P(t) - (center + s*axis)| 得到 t，再投影出轴上点
```

### 8.2 GizmoPlaneTranslate（平面内拖）

射线与平面求交：

```cpp
float t = -(eye.dot(n) + d) / ray.dot(n);   // n 为平面法线，d 为平面常数
pos = eye + ray * t;
```

### 8.3 GizmoAxisRotate（绕轴旋转）

1. **平面投影**：把射线与过中心的旋转平面求交，交点相对中心的向量投影到平面并归一化（`computePlaneProj`）；
2. **增量角**：相邻两帧投影方向夹角，用 `atan2(axis·(a×b), a·b)` 带符号累计（`computeAngle`）；
3. **累积角 + 吸附**：`m_totalAngle` 累加后按 1° 吸附（`enableSnap`）；
4. **输出**：`Eigen::AngleAxisf(snapAngle, axis)` 旋转参考方向/参考位置；
5. **圆弧**：`getRotationArc()` 在参考方向与当前方向之间插值出一圈弧点，供 `onUpdate` 绘制旋转路径。

---

## 9. 2D 覆盖层控件层：ScreenWidget

前面几节的控件都在 3D 空间里：要么走拾取 Pass 的颜色编码，要么用 CPU 射线求交。另有一类控件是**纯 2D 覆盖层**：图案画在 ImGui 的 draw list 上，点击用**屏幕空间相交测试**判定，完全不碰 3D 拾取——ViewCube 四周的旋转箭头、视口内的 HUD 按钮和滑条都属于这一类。

`ScreenWidget : public EventWidget` 是这类控件的基类。它把最容易写错、也最容易在每个控件里重复写错的部分收拢到一处：

| 层 | 文件 | 职责 |
| --- | --- | --- |
| 布局 | `Moon/Interactive/Screen/ScreenLayout.h` | 锚点 + 偏移 + 尺寸 + `uiScale` → `ScreenRect` |
| 形状与命中 | `Moon/Interactive/Screen/HitShape.h` | 绘制与命中共用同一份几何 |
| 状态机 / 基类 | `Moon/Interactive/Screen/ScreenWidget.h` | 光标、hover / press / drag、捕获、绘制入口 |
| 归属仲裁 | `Moon/Interactive/Screen/ScreenOverlayRegistry.h` | 让场景拾取、导航立方体、相机知道光标已被占用 |

事件订阅、`setActive` 开关、`InvokeEvent` 通知业务层这些能力直接继承自 `EventWidget`，所以 2D 控件和 3D 控件的生命周期、工具栏开关方式完全一致。

### 9.1 坐标与布局

```cpp
struct ScreenRect { float x, y, w, h; /* 左上原点，场景视口逻辑像素 */ };

struct ScreenLayout
{
    EScreenAnchor anchor;   // 9 宫格锚点
    ImVec2 offset;          // 距锚定边的距离
    ImVec2 size;            // 控件尺寸
    float  scale = 1.0f;    // 全局 uiScale
    ScreenRect Resolve(int viewportWidth, int viewportHeight) const;
};
```

**整套 2D 层只有一套坐标空间**：左上原点、y 向下、场景视口逻辑像素。它同时是交互器光标、`FrameParam::cursor`、`ImGui::GetForegroundDrawList()` 和 `ScreenLayout` 的空间，所以布局 → 命中 → 绘制之间不需要任何翻转；需要左下原点的只有 `glViewport()`，由 `ComputeViewCubeLayout()` 在边界处换算。

锚点算术只有 `ScreenLayout::Resolve()` 一份实现。`Im3DType.cpp` 里的 `ComputeViewCubeLayout()` 也是用它描述的——"立方体渲染到哪"和"按钮锚到哪"因此不可能漂移。

### 9.2 光标：从 `Interactor` 取

`Interactor` 是 `EventWidget` 的基类成员（`InteractorObserver::Interactor`），光标就在这里，不必绕到 `SceneView::getInutState()` 或 `FrameParam`：

```cpp
Interactor->GetEventPositionFlipY();   // 本次事件的光标，左上原点
Interactor->GetLastEventPosition();    // 上一个事件的光标，拖拽 delta
Interactor->GetSize(size);             // 视口尺寸（与翻转同源）
Interactor->IsCursorInsideViewport();  // Enter / Leave 维护
Interactor->GetControlKey() / GetShiftKey() / GetAltKey();
```

`GetEventPosition()` 保留 VTK 的左下原点约定，`GetEventPositionFlipY()` 是补上的左上原点版本。这样做的三个好处：

1. **按下位置是精确的**：Qt 事件进入 `ReceiveEvent()` 时先 `SetEventInformationFlipY()` 再 `InvokeEvent()`，所以 `onLeftMousePressed()` 里读到的是按下那一刻的光标，不存在"用上一帧光标"的问题；
2. **hover、press、drag 共用一份数据**，不会出现两个来源不一致；
3. **翻转和布局用同一个 `Size`**（`ViewerWidget::resizeEvent` 同时更新 `SceneView` 和交互器），不会出现"光标按交互器尺寸翻、布局按 SceneView 尺寸算"的错配。

`Enter` / `Leave` 事件用来维护 `CursorInsideViewport`：鼠标移出视口后控件必须退出 hover，否则光标停在最后位置、按钮一直亮着。

### 9.3 形状与命中测试

`HitShape` 把"画"和"测"绑在同一个描述上：

```cpp
struct HitShape
{
    enum class EType { Rect, Circle, RingArc, Triangle, Polygon };
    EType type;   int action;        // action 交给 OnAction 解释
    ImVec2 center, halfExtent;       // Rect
    float radius, bandHalfWidth;     // Circle / RingArc
    float startAngleDeg, sweepAngleDeg;
    std::vector<ImVec2> points;      // Triangle / Polygon（凸、局部坐标）
    float pad;                       // 命中外扩，像素

    bool Contains(const ImVec2& local) const;
    void Draw(ImDrawList*, const ImVec2& offset, ImU32 fill, ImU32 outline, float width) const;
};
```

一旦绘制和命中是两份几何描述，改按钮形状就会漏改命中区，点击范围会悄悄和看到的图案错开，所以这里强制共用。`pad` 是统一的"命中区比图案大一点"：

- 矩形 / 圆：直接放大尺寸或半径；
- 三角形 / 多边形：围绕**质心**等比放大（`1 + pad / maxRadius`），不依赖绕序或顶点顺序；
- 环带：径向加 `pad`，角向把 `pad` 用 `atan2(pad, r)` 换算成角度，保证弧上任意位置的外扩都是同样的像素宽度。

点与凸多边形的关系用"三个叉积同号"判断，同样不依赖绕序：

```cpp
bool PointInConvexOutline(p, outline) {
    bool hasNeg = false, hasPos = false;
    for (每条边 a→b) {
        float side = Cross2D(a, b, p);
        hasNeg |= side < 0; hasPos |= side > 0;
    }
    return !(hasNeg && hasPos);
}
```

### 9.4 状态机

```cpp
enum class EScreenState { Stop, Hot, Pressed, Dragging };
```

| 当前 | 条件 | 动作 | 下一状态 |
| --- | --- | --- | --- |
| Stop | 光标落到某个形状 | 记录 `mHotShape` | Hot |
| Hot | 光标离开全部形状 | 清空 `mHotShape` | Stop |
| Hot | 左键按下，`WantsCapture()==false` | **立即** `OnAction(action)` | Pressed |
| Hot | 左键按下，`WantsCapture()==true` | 捕获光标，`OnDrag()` | Dragging |
| Pressed | 左键松开 | 重新按当前光标判定 | Hot / Stop |
| Dragging | 每次鼠标移动 | `OnDrag()`（光标出界也继续） | Dragging |
| Dragging | 左键松开 | 释放捕获，重新判定 | Hot / Stop |
| 任意 | 光标离开视口 / 控件被禁用 | 清空状态 | Stop |

三个约定：

1. **按下即触发**（`Pressed` 只是锁存）。2D 按钮是离散命令，不需要"按下-拖动-松开"的过程，所以语义在 `onLeftMousePressed()` 里就跑掉；锁存的作用是防止同一次按住重复触发，并让渲染端在这段时间里持续认为"光标属于控件"。
2. **只有需要连续值的控件才捕获**（`WantsCapture()` 返回 true 的滑条、手柄）。捕获后 `OnDrag()` 每帧都收到光标，即使拖出控件矩形甚至拖出视口。
3. **`EventWidget` 的三个回调被 `final` 封口**。子类改成实现 `BuildLayout()` / `BuildShapes()` / `DrawContent()` / `OnAction()` / `OnDrag()` / `OnStateChanged()` / `IsInteractionEnabled()`，就不会有人漏掉三处状态刷新（每帧兜底、移动即刷新、按下前用实时光标）。

### 9.5 光标归属：ScreenOverlayRegistry

这一层解决"2D 控件和 3D 交互抢同一个点击"的问题。注意**不能靠拦截事件来做**：相机（`CameraController::HandleInputs`）和拾取（`SceneView::HandleActorPicking`）都是直接读 `InputState` 的，根本不经过 widget 的事件分发。所以归属必须是**可查询的状态**：

```cpp
ScreenOverlayRegistry::Instance().BlocksSceneCursor(x, y); // 场景让位（拾取、悬停高亮）
ScreenOverlayRegistry::Instance().HitsShape(x, y);         // HUD 让位（导航立方体的面点击）
ScreenOverlayRegistry::Instance().IsCapturing();           // 相机让位（拖拽中不许 orbit/pan）
```

两个设计决定：

- **按需查询，不做每帧快照**。`CameraController` 跑在 `SceneView::Update()` 里，比控件的 `onUpdate()`（在 `Render()` 里）更早，快照方案会让相机拿到上一帧的归属状态。`BuildLayout()` 是视口尺寸的纯函数、命中是解析式测试，任何时刻现算都是对的。
- **重叠用显式 `zOrder` 仲裁**。`drawWidgets()` 遍历的是 `unordered_map`，顺序不确定，谁在上层不能靠遍历顺序决定。

`BlocksSceneCursor` 和 `HitsShape` 的区别是有意保留的：ViewCube 的整块 125×125 矩形都要挡住场景拾取（否则会选中立方体背后的模型），但只有 4 个箭头"算命中"（否则立方体自己的面点击会让位，功能就没了）。

### 9.6 ViewCubeWidget：一个完整例子

`ViewCubeWidget` 现在只剩三件事，其余全在基类：

```cpp
ScreenLayout BuildLayout() const override       // TopRight，尺寸 = kViewCubeSize
void BuildShapes(std::vector<HitShape>&) const  // 4 个三角形，往 4 个方向
void DrawContent(ImDrawList&, const ScreenRect&) // DrawShapes()，样式由基类给
void OnAction(int action)                        // 触发 90° 旋转
```

按钮表把"形状"和"语义"分开，加按钮只改数据：

```cpp
static const ButtonDefinition definitions[] = {
    { EAction::OrbitUp,    0.0f, -1.0f },   // 屏幕 y 向下
    { EAction::OrbitRight, 1.0f,  0.0f },
    { EAction::OrbitDown,  0.0f,  1.0f },
    { EAction::OrbitLeft, -1.0f,  0.0f }
};
```

细节约定：

- **旋转语义**：绕 `SceneView::GetRoaterCenter()` 做 90° 转台旋转，保持距离 / 中心 / 缩放不变；轴与符号完全对齐 `CameraController::HandleCameraOrbit()`（左右绕世界 up、上下绕相机 right），所以"点箭头"和"把鼠标朝同方向拖 90°"等价。若已有 `MoveToPose` 动画在跑，用 `TryGetPendingPose()` 取动画目标位姿继续累加，连点 3 次正好 270°。
- **草图模式**：`IsInteractionEnabled()` 跟随 `CameraController::IsRotateEnabled()`（进入草图时被置 false），此时不绘制、不刷新状态、不抢光标。
- **与立方体的分工**：立方体本体（6 面 / 8 角 / 12 棱）仍然由 `ImRenderer::drawSort()` 绘制和 3D 命中，点击后 `FitToSelectedActor/FitToScene`；箭头只是叠在上面的 2D 覆盖层。`drawSort()` 通过 `HitsShape()` 让位给箭头，这一条对任何未来的 2D 控件都成立。

### 9.7 SplitScreen：捕获与拖拽的例子

`ViewCubeWidget` 只用到"按下即触发"；`SplitScreen`（路径追踪的分屏分割线）是**捕获拖拽**路径的例子：两个端点手柄 + 一个中点手柄，拖中点整体平移。

```cpp
ScreenLayout BuildLayout() const override {          // 铺满视口
    layout.anchor = EScreenAnchor::TopLeft;
    layout.size = GetViewportSize();                 // 于是局部坐标 == 屏幕坐标
    return layout;
}
void BuildShapes(std::vector<HitShape>& out) const override {
    // 3 个 Circle，action = Start / End / Middle
}
bool WantsCapture() const override { return true; }  // 按下即捕获
void OnDragBegin(const ImVec2& cursor) override;     // GetActiveShape() 拿到抓住的是哪个手柄
void OnDrag(const ImVec2& cursor) override;          // 移动手柄 + MarkGeometryDirty()
```

四个要点：

1. **`SetRectBlocksCursor(false)`**：矩形铺满整个视口，但只有手柄算命中，否则整块视口都会挡住场景拾取。这也说明 `BlocksSceneCursor`（矩形 + 形状都算）和 `HitsShape`（只算形状）为什么要分开。
2. **抓取偏移**：`OnDragBegin()` 记下"手柄位置 - 按下时光标"，之后 `handle = cursor + offset`，手柄不会在按下瞬间跳到光标中心；中点手柄另外记下抓取时的半向量，拖动时整体平移而不是重新缩放。
3. **`GetActiveShape()`**：拖拽期间保持在被抓的形状上（光标离开手柄也一样），这是"一个控件多个手柄"知道自己在拖哪个的办法。
4. **几何脏标记**：位置变了要 `MarkGeometryDirty()`。绘制和命中用的是同一份形状，所以两者会一起更新，不会一边动一边没动。

另外，拖拽中若控件被停用（`setActive(false)`、切工具、相机被锁定），基类会主动调用 `OnDragEnd()` 释放捕获——否则 `IsCapturing()` 会一直是 true，**相机会被永久挡住**。

---

## 10. 如何扩展一个新 Widget

1. **继承 `EventWidget`**，构造时传入名字（自动注册到 `ImRenderer` 与 `RenderWindowInteractor`）；
2. **选择控件风格**：
   - ClipPlane 风格：在 `Im3DType` 构建 `PolygonMesh`（`switchNextBlock` 分块命名），`onUpdate` 用 `drawOneMesh`，`onMouseMove` 用 `isSelectPolygon` 拾取；
   - WidgetViewData 风格：用 `viewData.setTriangleFace(...)` 承载三角形，`onUpdate` 用 `pushMatrix + drawTriangleList`，`onMouseMove` 用 `viewData.hitFace(ray, scale)` 拾取（需要固定屏幕尺寸时按 4.5 计算 scale）；
   - 2D 覆盖层风格：**继承 `ScreenWidget` 而不是 `EventWidget`**，只实现 `BuildLayout()` / `BuildShapes()` / `DrawContent()` / `OnAction()`（需要连续值时再加 `WantsCapture()` + `OnDrag()`），光标、状态机、命中测试和光标归属都由基类处理（见第 9 节）。适合"永远正对相机、固定屏幕尺寸、不参与 3D 拾取"的按钮、滑条、HUD 类控件；
3. **重写 `onUpdate()`**：绘制控件，读取 `getFrameParam()` 做逻辑；
4. **重写 `onMouseMove()`**：做悬停检测，切换状态机；
5. **重写 `onLeftMousePressed()` / `onLeftMouseReleased()`**：进入/退出拖拽，初始化 `GizmoBehaviour`；
6. **把交互结果写回场景**（如 `SetClipPlane`、`InvokeEvent(自定义事件)` 通知业务层）；
7. 需要接收键盘时重写 `onKeyPress` / `onKeyRelease`。

---

## 11. 关键文件

| 文件 | 职责 |
| --- | --- |
| `Moon/Interactive/EventWidget.*` | widget 基类：事件回调映射、生命周期、帧节流 |
| `Moon/Interactive/Interactive/RenderWindowInteractor.*` | Qt 事件 → 内部事件分发中心 |
| `Moon/Interactive/Im3DRenderer.*` | 立即模式绘制、gizmo 网格提交、拾取查询 |
| `Moon/Interactive/Im3DType.*` | `PolygonMesh` 块结构、`TransformAxis()` 等 gizmo 构建 |
| `Moon/Interactive/GizmoBehaviour.*` | 轴/平面/旋转几何算法 |
| `Moon/Interactive/Widgets/ClipPlane.*` | 剖切控件：状态机、手柄表、截面联动（示例） |
| `Moon/Interactive/ViewData.*` | WidgetViewData 风格控件的几何容器与 CPU 拾取（`hitFace` / `hitEdge` / `hitPoint`） |
| `Moon/Interactive/Widgets/AxisTranslationWidget.*` | 沿轴拖拽控件：固定屏幕尺寸、CPU 拾取、`LengthChange` 事件 |
| `Moon/Interactive/Widgets/ArrowRotateWidget.*` | 旋转控件：箭头绕轴旋转、`AngleChange` 事件 |
| `Moon/Interactive/Screen/ScreenLayout.*` | 2D 布局：锚点 + 偏移 + 尺寸 + `uiScale` → `ScreenRect` |
| `Moon/Interactive/Screen/HitShape.*` | 2D 形状：`Rect/Circle/RingArc/Triangle/Polygon` 的绘制 + 相交测试（共用几何） |
| `Moon/Interactive/Screen/ScreenWidget.*` | 2D 控件基类：光标、`Stop/Hot/Pressed/Dragging` 状态机、捕获、绘制入口 |
| `Moon/Interactive/Screen/ScreenOverlayRegistry.*` | 光标归属仲裁：场景拾取 / 导航立方体 / 相机按需查询 |
| `Moon/Interactive/Widgets/ViewCubeWidget.*` | ViewCube 旋转箭头：`ScreenWidget` 的完整例子 |
| `Moon/Interactive/Widgets/SplitScreen.*` | 分屏分割线：`ScreenWidget` 的捕获 / 拖拽例子（`PathTraceRenderPass` 用它的线方程） |
| `Moon/Interactive/Widgets/PadTaskWidget.*` | 平板控件：箭头平移 + 圆环点旋转，`LengthChange` / `AngleChange` |
| `Moon/editor/UI/TaskPanel/FilletTask.cpp` | 倒圆角 UI：用两个 `AxisTranslationWidget` 拖动控制半径 |
| `Moon/renderer/PickingRenderPass.cpp` | 拾取 framebuffer、像素读回与 `selectPolygon` |
| `Moon/Interactive/Im3DRenderer.h` | `FrameParam`（射线/光标/视口信息） |
