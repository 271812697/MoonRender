# 2D 覆盖层交互控件（ScreenWidget）架构设计

> 相关文档：[InteractiveWidget.md](InteractiveWidget.md)（3D 交互控件与事件层总览）、[ImguiArchitecture.md](ImguiArchitecture.md)（ImGui 集成与绘制后端）

本文说明视口 2D 覆盖层控件的设计：为什么需要单独一层、这一层由哪几块组成、光标和坐标怎么统一、命中与状态机怎么工作、以及"2D 控件和 3D 交互抢同一个点击"这件事是怎么解决的。

---

## 1. 定位与边界

### 1.1 两类交互控件

| | 3D 交互控件 | 2D 覆盖层控件（本文） |
| --- | --- | --- |
| 例子 | ClipPlane、ArrowRotateWidget、PrimitiveBox、各草图 Handler | ViewCube 旋转箭头、分屏分割线、HUD 按钮 / 滑条、colorbar |
| 空间 | 世界空间，随相机变化 | 屏幕空间，锚定视口 |
| 尺寸 | 需要自己算 `FIXED_SCALE` 才能保持屏幕大小恒定 | 天然恒定（逻辑像素） |
| 命中 | 拾取 Pass 的颜色编码 / CPU 射线求交 | 屏幕空间解析式相交测试 |
| 绘制 | `PolygonMesh` / `viewData` 提交给引擎 | ImGui draw list（立即模式） |
| 基类 | `EventWidget` | `ScreenWidget : EventWidget` |

两者共享同一套事件分发（`RenderWindowInteractor` → `EventWidget` 回调）和同一套开关（`setActive`、Passes 面板、工具栏），区别只在"怎么命中"和"怎么画"。

### 1.2 什么时候用，什么时候不要用

**适合**：贴在视口上的按钮、罗盘、滑条、图例（colorbar）、手柄、浮动提示——永远正对相机、尺寸恒定、不参与深度。

**不适合**：

- 本应放进 Qt dock 面板的常规表单控件（直接用 Qt，没必要走视口覆盖层）；
- 需要文本输入、滚动、多窗口的复杂控件（自己造代价大）；
- 需要跟随某个 3D 点的标注——正确做法是先 `worldToScreen()` 拿到屏幕坐标，**然后当成纯 2D 控件使用**。不要在同一个控件里同时维护世界坐标和屏幕坐标（ClipPlane 早期"圆弧半径与控件对不上"就是混了两套坐标）。

---

## 2. 分层架构

```
┌───────────────────────────────────────────────────────────────────────────┐
│ 4. 归属仲裁  ScreenOverlayRegistry                                         │
│    谁在光标下？谁捕获了鼠标？                                               │
│    ← SceneView 拾取 / ImRenderer 立方体 / CameraController 相机 查询         │
├───────────────────────────────────────────────────────────────────────────┤
│ 3. 状态机 + 基类  ScreenWidget : EventWidget                               │
│    Stop → Hot → Pressed / Dragging；捕获；绘制入口；输入与几何工具           │
├───────────────────────────────────────────────────────────────────────────┤
│ 2. 形状与命中  HitShape                                                    │
│    Rect / Circle / RingArc / Triangle / Polygon；绘制与命中共用一份几何      │
├───────────────────────────────────────────────────────────────────────────┤
│ 1. 布局  ScreenRect / ScreenLayout                                         │
│    锚点 + 偏移 + 尺寸 + uiScale → 屏幕矩形（左上原点、逻辑像素）             │
└───────────────────────────────────────────────────────────────────────────┘
        ↑ 全部挂在既有 EventWidget 体系上（事件订阅、生命周期、开关）
```

文件与职责：

| 文件 | 职责 |
| --- | --- |
| `Moon/Interactive/Screen/ScreenLayout.h/.cpp` | `ScreenRect`、`EScreenAnchor`、`ScreenLayout::Resolve()` |
| `Moon/Interactive/Screen/HitShape.h/.cpp` | `HitShape`：五种形状的 `Contains()` / `Draw()` / `pad` |
| `Moon/Interactive/Screen/ScreenPath.h/.cpp` | `ScreenPath`：任意轮廓（多环 / 带洞 / 开环）的包围盒、拟合、even-odd 命中、描边距离 |
| `Moon/Interactive/Screen/SketchPathBake.h/.cpp` | 烘焙：草图曲线 → 折线 → 串环 → `ScreenPath` |
| `Moon/Interactive/Screen/ScreenWidget.h/.cpp` | `EScreenState`、`ScreenShapeStyle`、`ScreenWidget` 基类 |
| `Moon/Interactive/Screen/ScreenOverlayRegistry.h/.cpp` | 光标归属查询（`BlocksSceneCursor` / `HitsShape` / `IsCapturing`） |
| `Moon/Interactive/Widgets/ViewCubeWidget.*` | 例子一：按下即触发（旋转箭头） |
| `Moon/Interactive/Widgets/SplitScreen.*` | 例子二：捕获拖拽（分屏分割线） |
| `Moon/Interactive/Widgets/PathShapeWidget.*` | 例子三：自定义（路径）形状，从草烘焙 |

这么分是因为三层的**变化频率不同**：布局经常改（加偏移、适配新视口），形状偶尔改，状态机几乎不改。混在一个类里，改布局就会碰到状态机。

---

## 3. 坐标系统一

**整套 2D 层只有一套坐标空间**：左上原点、y 向下、场景视口逻辑像素。

```
        (0,0)                                     (W,0)
          ┌─────────────────────────────────────────┐
          │   左上原点、y 向下、逻辑像素              │
          │                                         │
          │              视口 (SceneView)            │
          │                                         │
          └─────────────────────────────────────────┘
        (0,H)                                     (W,H)
```

这套空间同时是：

1. 交互器光标（`GetEventPositionFlipY()`）；
2. `FrameParam::cursor`；
3. `ImGui::GetForegroundDrawList()` 的绘制空间（`io.DisplaySize` = 视口逻辑尺寸）；
4. `ScreenLayout::Resolve()` 的输出空间。

所以 **布局 → 命中 → 绘制** 三步之间没有任何翻转，不用减 `-1`，也不用考虑 DPI。

### 3.1 唯一的两个例外

| 例外 | 处理 |
| --- | --- |
| `glViewport()` 需要左下原点 | `ComputeViewCubeLayout()` 在边界处换算：`glViewportY = viewportHeight - rect.y - rect.h` |
| 着色器里的 `gl_FragCoord` 是左下原点 | `SplitScreen::getLineEquation()` 输出前做一次 `y' = viewportHeight - y` |

**约定**：换算只允许出现在"给 GL 的最后一米"，控件内部一律左上原点。

### 3.2 逻辑像素 ≠ 后备像素

- `Interactor->GetSize()`、`SceneView::GetSafeSize()`、`ImGui io.DisplaySize` 都是 Qt 窗口的**逻辑尺寸**；
- 后备像素只出现在 `io.DisplayFramebufferScale`（= `devicePixelRatio()`）和 ImGui 渲染器内部的 `fb_width/height`；
- 2D 控件只用逻辑像素。想让 UI 在高 DPI 上大一点，用 `ScreenWidget::SetUiScale()`，不要乘 DPR。

---

## 4. 每帧时序

控件由两条独立路径驱动，理解它们是理解归属仲裁的前提。

### 4.1 事件路径（Qt 事件驱动，发生在两帧之间）

```
Qt 鼠标事件
  └─ ViewerWidget::event(QEvent*)
       ├─ SceneView::ReceiveEvent()            → 写 InputState（3D 拾取/相机读这个）
       └─ RenderWindowInteractor::ReceiveEvent()
            ├─ SetEventInformationFlipY()       ← 先记下本次事件的光标
            ├─ 维护 CursorInsideViewport（Enter / Leave）
            └─ InvokeEvent(LeftButtonPressEvent …)
                 └─ EventWidget 回调
                      └─ ScreenWidget::onLeftMousePressed()   ← 命中 + 状态机
```

关键点：**派发回调之前，交互器已经把本次事件的光标记下来了**，所以按下回调拿到的是按下那一刻的位置，不存在"用上一帧光标"的问题。

### 4.2 帧路径（`paintGL`）

```
ImRenderer::newImgui()                     ← 开启 ImGui 帧（覆盖层绘制的前提）
Im2DRender::newFrame()
ImRenderer::newFrame(sceneView)            ← 相机参数、清空顶点缓冲
SceneView::Update(dt)
  └─ CameraController::HandleInputs()      ← ① 查 IsCapturing()，捕获中不做 orbit/pan
SceneView::Render()
  ├─ HandleActorPicking()                  ← ② 查 BlocksSceneCursor()，占用中不拾取
  │     └─ 拾取 Pass（按需）
  └─ … pass 链 … ImRenderer::endFrame()
        ├─ runDrawTask()
        ├─ drawWidgets()                   ← ③ ScreenWidget::onUpdate()：状态兜底 + 绘制
        ├─ test() / drawMesh() / drawSort()
        │     └─ drawSort() 末尾           ← ④ 查 HitsShape()，让位给 2D 按钮
        └─ drawUnsort()
SceneView::Present()
debugImgui() / FPS overlay
ImRenderer::endImgui()                     ← 覆盖层最后合成 → 天然盖在场景之上
```

顺序上的三个事实：

1. **`drawWidgets()` 在 `drawSort()` 之前**，所以控件这一帧算好的状态可以被立方体的点击分支读到；
2. **相机在控件之前**（`Update` 早于 `Render`）；
3. **覆盖层最后合成**，永远盖在场景和 gizmo 之上，且不吃深度。

### 4.3 为什么归属用"按需查询"而不是"每帧快照"

第 2 条事实决定了归属不能做成快照：

```
Update  ──► 相机查询归属
                 ↑
                 └── 如果归属是控件在 drawWidgets()（Render 内）发布的快照，
                     相机拿到的永远是上一帧的状态 → 刚按下控件的这一帧相机照样会动
```

所以注册表的查询是**现算**的：`BuildLayout()` 是视口尺寸的纯函数，命中是解析式测试，任何时刻询问都能得到正确答案，不存在帧序依赖。

---

## 5. 光标：统一从 `Interactor` 取

`Interactor`（`RenderWindowInteractor`）是 `EventWidget` 的基类成员（`InteractorObserver::Interactor`），光标就在这里，不需要绕到 `SceneView::getInutState()` 或 `FrameParam`：

```cpp
int* GetEventPositionFlipY();    // 本次事件的光标，左上原点（2D 层用这个）
int* GetEventPosition();         // 同上，但保留 VTK 的左下原点约定
int* GetLastEventPosition();     // 上一个事件的光标（拖拽 delta）
void GetSize(int out[2]) const;  // 视口逻辑尺寸（与翻转同源）
bool IsCursorInsideViewport();   // Enter / Leave 维护
int  GetControlKey() / GetShiftKey() / GetAltKey();
```

### 5.1 为什么需要 `GetEventPositionFlipY()`

`ReceiveEvent()` 里所有鼠标事件先走：

```cpp
SetEventInformationFlipY(x, y, ctrl, shift, 0, dbl);
//   └─► SetEventPosition(x, Size[1] - y - 1)     ← 存的是左下原点
```

也就是 `EventPosition` 是 VTK 约定的左下原点坐标。2D 控件需要左上原点，所以补了一个对称的 `GetEventPositionFlipY()`。

**翻转用的是 `Size[1]`，所以布局也必须用同一个 `Size`**（`Interactor->GetSize()`）。否则会出现"光标按交互器尺寸翻转、布局按 SceneView 尺寸计算"的错配（两者当前同值，但必须约定成单一来源）。

### 5.2 `CursorInsideViewport`：hover 不能粘住

光标移出视口后 `EventPosition` 会停在最后一个位置。不处理的话控件会一直保持 hover（ViewCube 的箭头会一直亮着），甚至继续响应按下。所以：

- Qt 的 `Enter` / `Leave` 事件到达时维护 `CursorInsideViewport`；
- 光标不在视口内时基类清空 hover；
- 初值给 `true`（fail-open）：**误判成"在里面"最多多亮一下，误判成"在外面"会让控件完全点不动**。

---

## 6. 布局：锚点 + 偏移 + 尺寸

```cpp
struct ScreenRect { float x, y, w, h; };      // 左上原点、逻辑像素

enum class EScreenAnchor
{
    TopLeft, TopCenter, TopRight,
    CenterLeft, Center, CenterRight,
    BottomLeft, BottomCenter, BottomRight
};

struct ScreenLayout
{
    EScreenAnchor anchor = EScreenAnchor::TopRight;
    ImVec2 offset{ 0, 0 };    // 距锚定边的距离
    ImVec2 size{ 0, 0 };      // 控件尺寸
    float  scale = 1.0f;      // 全局 uiScale（SetUiScale 会乘进来）
    ScreenRect Resolve(int viewportWidth, int viewportHeight) const;
};
```

`Resolve()` 是**唯一**的锚点算术实现：`anchor` 转成"行 / 列"索引（0/1/2 = 起 / 中 / 末），两个轴各自 `PlaceAxis()`。控件的 `BuildLayout()` 只需要填这三个字段。

### 6.1 局部坐标

命中形状与绘制都用**控件局部坐标**（原点 = 控件矩形左上角）：

```cpp
ImVec2 ToLocal (const ImVec2& screenPoint) const;   // 屏幕 → 局部
ImVec2 ToScreen(const ImVec2& localPoint ) const;   // 局部 → 屏幕
```

于是"改锚点 / 改偏移"不会碰到任何形状坐标。`HitShape::Draw()` 也只接收一个 `offset`，由基类传 `rect.TopLeft()`。

### 6.2 尺寸的三种来源

1. **固定**：直接写 `size`（ViewCube：125×125）；
2. **跟随视口**：`size = GetViewportSize()`（SplitScreen：于是局部坐标 == 屏幕坐标）；
3. **由内容决定**：在 `BuildLayout()` 里量文字 / 量内容再填 `size`（colorbar 的宽度由刻度文字决定）。

`BuildLayout()` 是每帧调用的 `const` 虚函数，所以第 3 种不需要额外机制——但要注意：一旦它依赖字体度量，就等于依赖了当前绘制后端（见 12.4）。

### 6.3 与导航立方体共用同一份矩形

```
ComputeViewCubeLayout()
  ├─ ScreenRect rect = ScreenLayout{TopRight, offset(5,5), size(125,125)}.Resolve(W, H)
  ├─ centerX / centerY / halfSize   ← 立方体自己用
  └─ glViewportX / glViewportY      ← 给 glViewport()（左下原点换算）
```

这样"立方体渲染到哪"和"旋转箭头锚到哪"不可能漂移。

---

## 7. 形状与命中

```cpp
struct HitShape
{
    enum class EType { Rect, Circle, RingArc, Triangle, Polygon };

    EType type = EType::Rect;
    int   action = 0;                      // 语义 id，交给 OnAction 解释

    ImVec2 center, halfExtent;             // Rect
    float  radius;                         // Circle / RingArc（环带中心线半径）
    float  bandHalfWidth;                  // RingArc 环带半宽
    float  startAngleDeg, sweepAngleDeg;   // RingArc（屏幕空间，y 向下）
    std::vector<ImVec2> points;            // Triangle / Polygon（局部坐标）
    float  pad = 0.0f;                     // 命中外扩（像素）

    bool Contains(const ImVec2& local) const;
    void Draw(ImDrawList*, const ImVec2& offset, ImU32 fill, ImU32 outline, float width) const;
    ImVec2 LocalCenter() const;
};
```

### 7.1 为什么"画"和"测"必须挂在同一个结构上

只要绘制和命中是两份描述，改按钮形状就会漏改命中区，点击范围会慢慢和看到的图案错开（用户只觉得"点不准"，说不清哪里不对）。所以 `HitShape` 同时提供 `Contains()` 和 `Draw()`，基类的 `DrawShapes()` 也只画这一份几何。

**唯一例外**是装饰性图形（分割线、刻度、背景板）：它们不需要命中，直接写在 `DrawContent()` 里，不进入形状表。

### 7.2 `pad` 的统一语义

`pad` 只影响命中、不影响绘制；各形状放大方式不同，但都保证**屏幕上的外扩宽度一致**：

| 形状 | 实现 |
| --- | --- |
| Rect | 半尺寸各加 `pad` |
| Circle | 半径加 `pad` |
| RingArc | 径向加 `pad`；角向把 `pad` 用 `atan2(pad, r)` 换算成角度（弧上任意位置外扩都是同样像素宽度） |
| Triangle / Polygon | 绕**质心**按 `1 + pad / maxRadius` 等比放大（不依赖顶点顺序与绕序） |

### 7.3 点与凸多边形

```cpp
bool PointInConvexOutline(p, outline)
{
    bool hasNeg = false, hasPos = false;
    for (每条边 a→b) {
        float side = Cross2D(a, b, p);      // (b - a) × (p - a)
        hasNeg |= side < 0; hasPos |= side > 0;
    }
    return !(hasNeg && hasPos);             // 全部同侧 = 内部
}
```

好处是不依赖绕序（顺时针 / 逆时针都行），代价是**只对凸多边形成立**——这是当前形状层的已知边界（见 12.3）。

### 7.4 路径形状（自定义形状）

内置原语表达不了的形状用 `HitShape::EType::Path`：它引用一个 `ScreenPath`（任意轮廓），并自带拾取模式。

```cpp
struct ScreenPath
{
    std::vector<std::vector<ImVec2>> loops;      // 子路径：外环 / 洞 / 断开的折线
    std::vector<bool>  closed;                   // 每条子路径是否闭合
    ImVec2 boundsMin, boundsMax;                 // 缓存包围盒

    void  Transform(ImVec2 scale, float rotDeg, ImVec2 translation, bool flipY);
    void  FitInto(const ScreenRect& rect, bool flipY);      // 保比例拟合（并做 y 翻转）
    bool  ContainsPoint(const ImVec2& p) const;             // even-odd（跨所有环）
    bool  ContainsPointWithPad(const ImVec2& p, float pad) const;
    float DistanceToOutline(const ImVec2& p) const;
    bool  NearOutline(const ImVec2& p, float halfWidth) const;
};

// HitShape 里配套的字段
std::shared_ptr<const ScreenPath> path;      // 形状可被多个控件共享
HitShape::EPickMode pickMode;                // Fill / Stroke / FillOrStroke
float strokeWidth, strokePickSlack;          // Stroke 模式的线宽与额外拾取余量
```

三个要点：

1. **填充用 even-odd**（跨所有环的射线穿越计数），嵌套环天然变成洞；凹凸轮廓都正确。这正好补上 `Polygon` 只能凸的限制。
2. **描边用"到折线的距离"**：开放路径（折线、勾、曲线）没有内外之分，只能靠距离判定，阈值 = `strokeWidth / 2 + strokePickSlack + pad`，而且**必须在变换到像素空间之后算**，否则缩放后就不是"像素阈值"了。
3. **`pad` 对填充路径的含义**：任意轮廓没法像矩形那样"向外扩一圈"，所以填充模式的 `pad` 定义为"在内部，或者离轮廓不超过 pad"。

轮廓从哪里来有两种，走的是**同一条下游**（连面 → 离散 → even-odd）：

| 来源 | 接口 | 适用 |
| --- | --- | --- |
| 代码里用 OCCT 曲线画 | `ShapeBuilder`（见 12.8） | 固定形状：按钮、图标、装饰轮廓。改形状 = 改代码里的几个数 |
| 草图烘焙 | `BakeSketchFaces()`（见 12.6） | 需要交互式设计、或形状本来就来自建模草图 |

两者都汇合到 `BakeShapeFaces()`：入参是 `TopoDS_Shape`，出来是 `std::vector<ScreenPath>`。

### 7.5 性能

- 命中是 O(形状数) 的解析式测试，无分配、无 GPU 往返；
- `BuildShapes()` 只在几何脏标记置位时重建（位置变了要 `MarkGeometryDirty()`）；
- `EnsureGeometry()` 每次查询都会 `Resolve()` 一次矩形（几个浮点运算），换来的是"不必关心帧序"；
- 注册表查询是 O(控件数 × 形状数)，控件是"几个"的量级，不需要优化。

---

## 8. 状态机

```cpp
enum class EScreenState { Stop, Hot, Pressed, Dragging };
```

```
                    光标落到某个形状
        Stop ──────────────────────────────► Hot
          ▲                                   │
          │  光标离开全部形状                  │ 左键按下
          └───────────────────────────────────┤
                                              │
                      WantsCapture() == false │ WantsCapture() == true
                                              ▼
                                         Pressed ──── 左键松开 ───► Stop / Hot
                                         （锁存）      （重新判定）

                                              │
                                              ▼
                                        Dragging ──── 左键松开 ───► Stop / Hot
                                        （捕获）      （EndDrag → OnDragEnd）
```

### 8.1 状态语义

| 状态 | 含义 | 进入 | 退出 |
| --- | --- | --- | --- |
| `Stop` | 光标不在控件上 | 初始 / 光标离开 / 松开 | 光标落到形状上 |
| `Hot` | 光标在形状上，未按下 | `FindHotShape() >= 0` | 光标离开 / 按下 / 控件被禁用 |
| `Pressed` | 左键按下且动作已触发 | `Hot` + 按下 + 不需捕获 | 左键松开 / 控件被禁用 |
| `Dragging` | 捕获了光标，正在拖拽 | `Hot` + 按下 + `WantsCapture()` | 左键松开 / 控件被禁用（`EndDrag`） |

### 8.2 两个关键约定

**① 按下即触发，`Pressed` 只负责锁存。**

```cpp
void ScreenWidget::onLeftMousePressed()
{
    RefreshState();                         // 用实时光标重算，保证一致性
    if (mState != EScreenState::Hot || mHotShape < 0) return;

    if (WantsCapture()) {                   // 滑条 / 手柄
        SetState(EScreenState::Dragging);
        OnDragBegin(GetCursorLocal());
        OnDrag(GetCursorLocal());
    } else {                                // 普通按钮
        SetState(EScreenState::Pressed);
        OnAction(mShapes[mHotShape].action);
    }
}
```

2D 按钮是离散命令，不需要"按下-拖动-松开"的过程，所以语义在按下时就跑掉。`Pressed` 的作用是**防止同一次按住重复触发**，并让渲染端在这段时间持续认为"光标属于控件"。

**② 只有连续值控件才捕获。**

`WantsCapture()` 为 true 的控件（滑条、手柄）在按下时捕获光标：此后即使光标移出控件矩形、甚至移出视口，`OnDrag()` 每帧仍然收到光标。松手或被迫中断时基类一定调用 `OnDragEnd()`——那是"提交 / 丢弃值"的时机。

### 8.3 三个刷新点

| 时机 | 调用 | 为什么 |
| --- | --- | --- |
| 每帧 | `onUpdate()` | 兜底：鼠标没动、没有事件时状态也要跟上（例如视口缩放导致矩形变了） |
| 光标移动 | `onMouseMove()` | hover 反馈要跟手 |
| 左键按下 | `onLeftMousePressed()` | **必须重算**：按下前的最后一次 move 可能很久以前，甚至没发生过 |

这三处都在基类里。`EventWidget` 的四个回调在 `ScreenWidget` 里被 `final` 封口，就是为了避免有人漏写其中一处。

### 8.4 拖拽中途失效

拖拽过程中如果控件不可交互了（`setActive(false)`、切工具、相机被草图锁定），必须**主动释放捕获**，否则 `IsCapturing()` 永远为 true，**相机会被永久挡住**：

```cpp
if (!IsInteractive())
{
    if (mState == EScreenState::Dragging) EndDrag();    // → OnDragEnd + Stop
    mHotShape = -1;
    SetState(EScreenState::Stop);
    return;
}
```

---

## 9. 光标归属：ScreenOverlayRegistry

### 9.1 问题

同一个点击会被三处看到：

1. **场景拾取**（`SceneView::HandleActorPicking`）——读 `InputState`，决定选中哪个 actor；
2. **相机**（`CameraController::HandleInputs`）——读 `InputState`，决定 orbit / pan；
3. **3D gizmo**——各自命中。

点一个 2D 按钮时前两处必须让位，否则会"点按钮的同时把背后的模型选中 / 把相机转过去"。

### 9.2 为什么不能靠"拦截事件"

事件系统里确实有 AbortFlag 机制（`EventObject::InvokeEvent` 允许观察者中止传播），但**对这个场景没用**：相机和拾取根本不经过 widget 的事件分发，它们直接读 `InputState`（见 4.2 的顺序图）。所以归属只能是**可查询的状态**。

### 9.3 三个查询及其语义差别

```cpp
ScreenOverlayRegistry::Instance().BlocksSceneCursor(x, y);   // 场景让位
ScreenOverlayRegistry::Instance().HitsShape(x, y);           // 立方体 / HUD 让位
ScreenOverlayRegistry::Instance().IsCapturing();             // 相机让位
```

| 查询 | 使用者 | 语义 |
| --- | --- | --- |
| `BlocksSceneCursor` | `SceneView::HandleActorPicking()` | 光标在**控件矩形**内、或落在**形状**上、或正在拖拽 → 不做 3D 拾取、不悬浮高亮 |
| `HitsShape` | `ImRenderer::drawSort()`（导航立方体面点击） | 光标正落在某个**形状**上 → 立方体让位 |
| `IsCapturing` | `CameraController::HandleInputs()` | 有控件捕获了鼠标 → 不做 orbit / pan（缩放与键盘移动不受影响） |

`BlocksSceneCursor` 与 `HitsShape` 的差别是**有意**的，用 ViewCube 就能说明：

- 整块 125×125 矩形都要挡场景拾取，否则点立方体旁边会选中背后的模型 → 需要 `BlocksSceneCursor`；
- 但只有 4 个箭头算"命中"，否则立方体自己的面点击会让位给箭头，**点面 fit 的功能就没了** → 需要 `HitsShape`。

想要"只有形状挡场景、矩形不挡"，用：

```cpp
SetRectBlocksCursor(false);
```

SplitScreen 就是这个用法：矩形铺满整个视口（于是局部坐标 == 屏幕坐标），但只有三个手柄算命中。

### 9.4 重叠与 z-order

`drawWidgets()` 遍历的是 `std::unordered_map<unsigned int, EventWidget*>`，**顺序不确定**，所以重叠控件不能靠遍历顺序决定谁在上层：

```cpp
void ScreenWidget::SetZOrder(float p_zOrder);    // 越大越靠上
```

`GetOwnerAt()` 取 z 最大的那个；`FindHotShape()` 只在"别人 z 比我大"时让位（z 相等时保持旧行为，互不压制），因此单控件和重叠场景的语义都是确定的。

### 9.5 查询是现算的

```cpp
bool ScreenWidget::BlocksSceneCursor(float x, float y) const
{
    if (!IsInteractive()) return false;
    if (mState == EScreenState::Dragging) return true;     // 捕获中：出界也归我
    EnsureGeometry();
    if (mRectBlocksCursor && mRect.Contains({ x, y })) return true;
    return HitTestShapes(mRect.ToLocal({ x, y })) >= 0;
}
```

没有"每帧发布一次"的快照，理由见 4.3。

---

## 10. 生命周期与开关

### 10.1 注册与注销

```cpp
ScreenWidget::ScreenWidget(const std::string& name) : EventWidget(name)
{
    ScreenOverlayRegistry::Instance().Register(this);
}
ScreenWidget::~ScreenWidget()
{
    ScreenOverlayRegistry::Instance().Unregister(this);
}
```

注册 / 注销跟着构造 / 析构走（而不是跟着 `setActive`），查询时由 `IsInteractive()` 过滤。这样控件一被停用就**立刻**停止占用光标，不需要额外同步。

### 10.2 三个"能不能交互"的开关

```cpp
bool ScreenWidget::IsInteractive() const
{
    // 有意不读任何 ImGui 状态：交互层不能依赖"当前恰好负责绘制"的库
    return isActived() && isVisible() && IsInteractionEnabled();
}
```

| 开关 | 来源 | 用途 |
| --- | --- | --- |
| `isActived()` | `setActive(bool)`（EventWidget） | Passes 面板 / 工具栏开关；同时决定事件是否订阅 |
| `isVisible()` | `setVisible(bool)` | 逻辑上存在但不绘制（例如被别的模式接管） |
| `IsInteractionEnabled()` | 子类虚函数 | 临时禁用，例如相机被草图锁定时 ViewCube 自动隐藏 |

与绘制的关系：`onUpdate()` 先刷新状态，`!IsInteractive()` 时**直接不绘制**（ViewCube 在草图模式下整体消失）。想要"可见但灰掉"，需要子类自己画灰态——见 12.5。

### 10.3 注册到引擎

```cpp
// Moon/renderer/GizmoRenderPass.cpp
mWidgets["ViewCube"]    = new MOON::ViewCubeWidget("ViewCube");     // 常显
mWidgets["SplitScreen"] = new MOON::SplitScreen("SplitScreen");
mWidgets["SplitScreen"]->setActive(false);                          // 默认关闭
```

`EventWidget` 的构造函数会把自己注册进 `ImRenderer`，不需要额外步骤。开关有两条路：

1. **Passes 面板**：`GizmoPassComponent` 遍历所有 widget，每个生成一个 BoolProperty → `Settings → Passes → ImRenderer → <控件名>`；
2. **代码 / 工具栏**：`GizmoRenderPass::enableGizmoWidget(name, flag)`。

---

## 11. 写一个新的 2D 控件

### 11.1 步骤

1. **继承 `ScreenWidget`**（不是 `EventWidget`），需要常显时构造里 `setActive(true)`；
2. 实现 `BuildLayout()`：填锚点、偏移、尺寸；
3. 实现 `BuildShapes()`：把可点击部分写成 `HitShape`，每个给一个 `action`（纯展示控件留空）；
4. 实现 `DrawContent()`：画装饰（背景、线、刻度）+ 调 `DrawShapes()` 画可点击形状；
5. 实现 `OnAction(action)`：点击后做什么；
6. 需要连续值（滑条、手柄）：`WantsCapture() → true`，实现 `OnDragBegin / OnDrag / OnDragEnd`，位置变了记得 `MarkGeometryDirty()`；
7. 需要临时禁用：实现 `IsInteractionEnabled()`；
8. 注册进 `GizmoRenderPass`。

### 11.2 骨架

```cpp
class MyWidget : public ScreenWidget
{
public:
    MyWidget(const std::string& name) : ScreenWidget(name)
    {
        setActive(true);
        SetHoverCursor(ImGuiMouseCursor_Hand);      // 可选：hover 光标
    }

protected:
    ScreenLayout BuildLayout() const override
    {
        ScreenLayout layout;
        layout.anchor = EScreenAnchor::TopRight;
        layout.offset = ImVec2(16.0f, 16.0f);
        layout.size   = ImVec2(120.0f, 32.0f);
        return layout;
    }

    void BuildShapes(std::vector<HitShape>& p_out) const override
    {
        HitShape button;
        button.type = HitShape::EType::Rect;
        button.center = ImVec2(60.0f, 16.0f);       // 局部坐标
        button.halfExtent = ImVec2(60.0f, 16.0f);
        button.pad = 4.0f;
        button.action = kActionApply;
        p_out.push_back(button);
    }

    void DrawContent(ImDrawList& p_drawList, const ScreenRect& p_rect) override
    {
        DrawShapes(p_drawList, p_rect);             // 颜色 / 高亮由基类样式控制
    }

    void OnAction(int p_action) override { /* … */ }
};
```

### 11.3 两个现有控件对照

| | ViewCubeWidget | SplitScreen |
| --- | --- | --- |
| 用途 | 立方体四周的旋转箭头 | 路径追踪的分屏分割线 |
| 布局 | TopRight，125×125（与立方体同一块矩形） | TopLeft + 整个视口（局部 == 屏幕） |
| 形状 | 4 个 `Triangle`，方向朝外 | 3 个 `Circle`（起点 / 终点 / 中点） |
| 矩形是否挡场景 | 挡（默认 `SetRectBlocksCursor(true)`） | 不挡（`SetRectBlocksCursor(false)`） |
| 命中后 | `OnAction` → 立即转 90° | `WantsCapture` → 拖拽 |
| 用到的状态 | Stop / Hot / Pressed | Stop / Hot / Dragging |
| 额外开关 | `IsInteractionEnabled()` 跟随草图锁定 | — |
| 装饰绘制 | 无（形状本身就是全部） | 形状之间那条黄线 |

此外还有 `PathShapeWidget` 作为第三条路线：**形状来自自定义轮廓**（内置五角星或烘焙的草图），`pickMode = FillOrStroke` 同时启用"内部"和"贴近轮廓"两种判定，矩形不挡场景（`SetRectBlocksCursor(false)`），是验证路径形状管线的样板。

---

## 12. 现状与待补能力

### 12.1 已完成

- 布局层：锚点 / 偏移 / 尺寸 / `uiScale`，唯一实现；
- 形状层：5 种形状、统一 `pad`、绘制与命中共用一份几何；
- 状态机：四状态、锁存、捕获、拖拽生命周期、中途失效自动释放；
- 归属仲裁：三个查询、z-order、按需现算，三个消费者（拾取 / 立方体 / 相机）已接入；
- 光标：统一从 `Interactor` 取，含 Enter/Leave 与修饰键；
- 两个真实控件：ViewCubeWidget（按下即触发）、SplitScreen（捕获拖拽）。

### 12.2 交互层对 ImGui 的依赖只剩两处

```
ScreenWidget::onUpdate()
  ├─ ImGui::GetForegroundDrawList()    ← 取绘制目标
  └─ ImGui::SetMouseCursor(...)        ← hover 光标形状
```

命中、状态机、归属仲裁里**没有任何 ImGui 函数调用**（只用 `ImVec2` / `ImU32` 当数据类型）。这条边界的意义是：将来把绘制换成自研 2D 渲染时，要改的只有 `onUpdate()` 里那一小块，交互逻辑一行不动。

要彻底去掉这个依赖，做法是引入一个薄的 canvas 接口（`Rect / Circle / RingArc / Convex / Line / Text`），今天转发 `ImDrawList`，将来转发自研后端——控件代码不变。

### 12.3 形状能力的边界

| 想要 | 现状 |
| --- | --- |
| 圆角矩形 | 不支持（`Rect` 的 rounding 固定为 0） |
| 凹多边形 | `Path` 支持（even-odd 命中 + 自研 ear clipping 填充） |
| 带洞的形状 | 支持：even-odd 命中，填充用"桥接 + ear clipping"（洞先接到外环上再三角化） |
| 自交多边形 | 命中能算，填充结果不确定，烘焙时应校验并提示 |
| 旋转的矩形 / 胶囊 | 只能用 `Polygon` 手工生成旋转后的点集，或用 `ScreenPath::Transform()` 旋转 |
| 图标 / 纹理 | 不支持（形状层没有纹理） |
| 文本 | 不支持（见 12.4） |
| 完全任意（草图曲线轮廓） | `Path` 支持：`BakeScreenPathFromSketch()` 把草图烘成轮廓（见 12.6） |
| 按函数判定 | 只能整块重写 `DrawContent()`，此时命中要自己写，等于放弃"共用几何"的保证 |

补齐的性价比顺序：① `Rect` 加 `rounding`；② 加生成器助手（`MakeRoundRect / MakeCapsule / MakeRegularPolygon / ConvexHull`）；③ 逃生舱：`HitShape` 加可选的 `customHit` 回调。

### 12.4 文本与纹理（colorbar 这类控件的前提）

当前只能靠 ImGui 的 draw list 提供文本（`AddText` / `CalcTextSize`）。自研的 `MOON::Render2D::ImDrawList`（`Moon/Interactive/Im2DType.h`）已有直线 / 折线 / 圆角矩形 / 凹凸多边形，但**还没有文本、没有 `AddImage`、没有纹理绑定**，`Im2DRender` 的片元着色器也还没采样纹理。

所以"带标签 + 渐变"的 ColorBar 要分两步：

1. **短期**：色带用 N 段薄矩形拼（128~256 段即可，与 LUT 完全一致），文本走 ImGui 的 `AddText`；
2. **中期**：自研 2D 支持纹理后，色带换成 1×N 的 LUT 纹理，文本换成自建字体图集。

无论走哪条路，都需要补两个能力：

- **`Text` / `MeasureText`** 两个 canvas 操作 + 对齐方式（右对齐刻度、居中标题）；
- **数字格式化**（有效位数自适应、科学计数法），供刻度标签复用。

### 12.5 其他待补

| 项 | 说明 |
| --- | --- |
| hover 过渡动画 | 目前颜色硬切，没有渐变 / 光晕 |
| `disabled` 灰显 | 现在 `IsInteractionEnabled()` 为 false 时整体消失，需要"可见但灰掉" |
| 右键归属 | 状态机只处理左键；右键目前归相机（orbit），是否允许 2D 控件吃右键要先定规则 |
| `uiScale` 全局来源 | `SetUiScale()` 已就位但没有全局设置项 |
| 极小视口钳制 | `ScreenLayout::Resolve()` 不钳制，角上的控件在小窗口里会被裁掉 |

### 12.6 用草图画出控件形状

**一个面 = 一个 shape**。拓扑直接用草图自己的那套（和 `makeDone()` 里缓存的 `doneWireShape` / `doneFaceShape` 是同一件事）：

```
SketcherObj::toShape()
   │  OCCT BRepBuilderAPI_MakeWire + ShapeFix_Wire 串边成 wire，跳过构造几何
   ▼
wire（1 条，或若干条组成 compound）
   │  makeElementFace(nullptr, "Part::FaceMakerBullseye") 封闭 wire 连成面
   ▼
face（可能多个）          每个面：loops[0] = 外环，后面是它的洞
   │  BakeShapeFaces()：BRepTools_WireExplorer 沿 wire 取边
   │                    + GCPnts_QuasiUniformDeflection 按弦高容差离散
   │                    + 按边的 orientation 决定采样方向（见下）
   ▼
ScreenPath（草图坐标，y 向上）
   │  FitWiresInto(控件矩形, flipY = true)：整个烘焙共用一个变换 + y 翻转
   ▼
每个面一个 HitShape::Path（action = 面的序号）
```

`BakeSketchFaces()` 只是 `BakeShapeFaces(p_sketch.toShape())` 的一层包装，所以"代码里用 OCCT 造的形状"（12.8）和草图走的是**完全同一套**连面、离散、闭合校验代码。

现状与边界：

| 项 | 现状 |
| --- | --- |
| 曲线类型 | 全部支持：串边、交点切分、T 形连接都交给 OCCT，不在我们这边实现 |
| 构造几何 | `toShape()` 内部就跳过了 |
| 多个封闭 wire | 不相交 → 多个面 → 多个 shape；嵌套 → 一个面带洞 |
| 洞 | 支持：命中与填充都用 even-odd（`loops[0]` 之外都是洞） |
| 离散 | 按弦高容差（`flattenDeflection`，草图单位），大圆弧不会欠采样、小圆弧不会过采样 |
| 坐标系 | 采样**边自身的曲线**并忽略 placement（`BRep_Tool::Curve` + `GeomAdaptor_Curve`），曲线所在空间就是草图的 2D 空间。注意别用 `BRepAdaptor_Curve`：它会带上 `toShape()` 挂在 shape 上的草图平面变换，取 (x, y) 会把画在非 XY 平面上的圆压成一条线 |
| 缓存 | 按"草图指针 + 曲线数"判断是否需要重烘焙；运行时不重复离散，也不每帧调 OCCT |
| 时效性 | 不读 `doneWireShape` / `doneFaceShape` 缓存（那是 `makeDone()` 时的快照），而是按需重算，这样草图还在编辑也能更新 |

### 12.7 采样 wire 时的两个坑

这两条都踩过，而且症状一样（形状看着对、就是**不填充**），根因都在"把 wire 变成点序列"这一步。

**坑 1：边的方向。** OCCT 的 wire 由带方向的 edge 串成，相连的两条边**公用一个顶点**；而 `BRep_Tool::Curve(edge, loc, first, last)` 给的是**曲线自己的参数区间**，不跟随边的方向。如果按 first→last 采样，wire 里 `TopAbs_REVERSED` 的那条边就会被**倒着采**：

```
上一个边的末尾（共享顶点 V）── 跳到这条边的远端 U ── 再沿原路回到 V
```

对一条直线边，这正好是"同一条线段走了两遍" → 轮廓变成自接触环 → shoelace 面积（外+内）与 even-odd 区域（外−内）对不上 → 填充被拒。

所以采样要**显式看方向**：

```cpp
const bool isReversed = p_edge.Orientation() == TopAbs_REVERSED;
const int index = isReversed ? (pointCount - step) : (step + 1);
```

**坑 2：闭合是"验证"出来的，不能假定。** `ScreenPath` 把所有 loop 都当闭合多边形处理（绘制、命中、三角化都是），所以**首尾没接上时它会自己补一条弦**——外面看不出异常，只是不填充，极难查。所以采样完必须：

```cpp
const bool flaggedClosed = BRep_Tool::IsClosed(wire);      // OCCT 的判断
const float gap = Distance(points.front(), points.back());  // 几何是否真的接上
// 容差随 wire 尺寸缩放：max(1e-4, 1e-5 × 包围盒长边)，不要用固定绝对值
```

不满足就**记一条带具体数值的日志**（gap 多少、多少点），而不是静默补弦。

> 顺带一提，`SketcherObj::toShape()` 里的 `ShapeFix_Wire::FixClosed()` 也会做同样的事：它通过 `FixLacking` **在 2D 空间补一条直线边**来闭合首尾接不上的 wire（OCCT 头文件原话："add a new edge (straight in 2d space)"）。所以如果以后又见到"轮廓里多出一条莫名的直线"，先怀疑两处：这里的采样方向，和 `toShape()` 的串接有没有留下缺口。

验证控件 `PathShapeWidget`（`Settings → Passes → ImRenderer → PathShape`）就是这条链路的样板：有激活草图时每个面一个 shape，没有时用内置的两条 wire（五角星 + 圆，正好验证"多形状各自独立"）。只有形状本身占用光标（`SetRectBlocksCursor(false)`），悬停只高亮命中的那个面，点击日志报告是第几个面：

```
[info]: [PathShape] clicked face 2 of 3 (source=sketch)
```

---

### 12.8 用 OCC 曲线在代码里造形状（ShapeBuilder）

固定形状（按钮、图标、装饰轮廓）不该走"画草图 → 烘焙 → 导出文件"那一圈：形状本来就不会变，用草图反而引入一串额外状态（草图在哪、单位是多少、画多大、要不要翻转）。`ShapeBuilder` 把曲线直接写在 C++ 里，并复用同一条连面/离散链路：

```cpp
ShapeBuilder builder;                 // 在控件自己的空间里画
builder.MoveTo(42.5f, 6.5f);          // 起点
builder.ArcByCenter(cx, cy, r, startDeg, sweepDeg);
builder.LineTo(...);                  // 接住上一条曲线的终点
builder.ArcByCenter(...);
builder.Close();                      // 回到 MoveTo 点

std::vector<ScreenPath> paths = builder.BuildPaths({ 0.05 });   // 容差 = 0.05 px
```

| 方法 | 说明 |
| --- | --- |
| `MoveTo(x, y)` | 开始一条子路径（一个子路径 = 一条 wire） |
| `LineTo(x, y)` | 从当前点连直线 |
| `ArcByCenter(cx, cy, r, startDeg, sweepDeg)` | 圆上的一段圆弧，**起点由 startDeg 决定**（不是"接上一点"），逆时针为正 |
| `ArcThrough(tx, ty, ex, ey)` | 三点圆弧（当前点 → 经过点 → 终点），手写时不方便算圆心就用它 |
| `Circle(cx, cy, r)` | 整圆，自带闭合，单起一条子路径 |
| `ArcSlot(cx, cy, r, halfWidth, startDeg, sweepDeg)` | 弧形槽：等宽圆环带 + 两端圆头，一条子路径一个 shape |
| `Close()` | 从当前点补一条线回到 `MoveTo` 点 |
| `BuildWires()` / `BuildFaces()` / `BuildPaths()` | 串 wire / 连面 / 离散成 `ScreenPath` |

连面仍然是 `makeElementFace(..., Bullseye)`：**一个子路径套在另一个里面就是洞**，互不相交就是多个 shape。

#### 坐标空间：这里没有"转换"

`ShapeBuilder` 不做任何缩放、拟合、翻转——**你写什么数就是屏幕上的什么像素**。对控件来说最自然的是直接按控件局部坐标画：x 向右、y 向下（屏幕空间），原点就是控件矩形的左上角。

这一点和草图路线刚好相反，别搞混：

| 路线 | 作者空间 | 到屏幕空间要做什么 |
| --- | --- | --- |
| `ShapeBuilder` | 直接是控件局部像素 | 什么都不用做 |
| `BakeSketchFaces()` | 草图坐标（y 向上，单位是草图单位） | `FitWiresInto(rect, flipY = true)`：统一缩放/平移 + y 翻转 |

#### 样板：ViewCube 的四个箭头

`ViewCubeWidget` 的四个旋转按钮就是这条路的例子（`BuildArrowShapes()`），形状是**弧形槽**（Arc slot）：等宽的一条圆环带，两端用半圆封口。

```
        ╭────────╮        外沿：半径 R + halfWidth 的圆弧
       ╱    ↑     ╲       中心线：半径 R（按钮轴线）
      ╰─────┴─────╯       内沿：半径 R − halfWidth 的圆弧
        ↑            ↑
      圆头（半圆）    圆头（半圆）
```

一个槽的轮廓就是绕一圈走：

1. **外沿**：半径 `R + halfWidth` 的圆弧，从 `startDeg` 扫 `sweepDeg`；
2. **末端圆头**：以中心线末端为圆心、`halfWidth` 为半径的**半圆**，转向与扫描方向一致（正扫角就用 +180°）；
3. **内沿**：半径 `R − halfWidth` 的圆弧，从末端反着走回起点；
4. **起点圆头**：同样一个半圆，正好落在第一条弧的起点上，于是 `Close()` 无缝。

六个按钮 = 六个槽，各自以所在方位为中心（角度按屏幕空间、y 向下）：

| 方位 | 角度 | 动作 | 绕的轴 |
| --- | --- | --- | --- |
| 上 | `-90°` | `OrbitUp` | 相机 right（俯仰） |
| 右 | `0°` | `OrbitRight` | 相机 up（偏航） |
| 东南 | `45°` | `RollClockwise` | **相机 forward（滚转）** |
| 下 | `90°` | `OrbitDown` | 相机 right（俯仰） |
| 西南 | `135°` | `RollCounterClockwise` | **相机 forward（滚转）** |
| 左 | `180°` | `OrbitLeft` | 相机 up（偏航） |

槽的张角**按按钮配置**（`ButtonDefinition::halfSweepDeg`）：四个侧面按钮各扫 ±16°（32° 弧长），两个滚转按钮夹在它们中间、只扫 ±12°（24°），这样相邻 45° 的槽之间都留 **17°** 缺口，两个 90° 区间（左→上、上→右）留 58°。

当前参数与校验值（`kSlot*` 常量，都在 [ViewCubeWidget.cpp](../Moon/Interactive/Widgets/ViewCubeWidget.cpp)）：

| 项 | 值 |
| --- | --- |
| 环半径 / 半宽 | 70 / 7 px |
| 侧面按钮的槽 | 弧长 32°，外形约 52.6 × 16.7 px，面积 701.3 px² |
| 滚转按钮的槽 | 弧长 24°，外形约 43.1 × 15.5 px，面积约 564 px²（都是 `2·h·L + π·h²`） |
| 内沿 / 外沿 | 距立方体中心 63 / 77 px（立方体轮廓最坏 51 px，所以内沿留了 12px 空隙） |

> **环会超出控件矩形**：最外沿 77px，而立方体视口半宽只有 62.5px。控件矩形只用于锚点与坐标换算（命中是形状级的，绘制也不裁剪），所以超出去没问题，但**要给视口角落留出空间**——`kViewCubeMargin` 因此是 24 而不是 5（上面那个槽最外点离视口边缘还有 9.5px）。半径继续加大时，这个 margin 要跟着加。

想调按钮外观就改这几个常量：`kSlotRadius`（环半径）、`kSlotHalfWidth`（槽宽的一半）、`kSlotHalfSweepDeg`（每个槽张开的半角）。想做带箭头的槽，就在末端再加一段三角/燕尾曲线。

#### 按钮的行为：相机在**自己的坐标系**里转 45°，再回到包围球中心

两种控件对"方向"的处理不一样，这也是它们必须分开的原因：

```
点击立方体的面：dir = 被点中格子的内法线 ─→ Fit(dir)
                 └ 用 dir + 世界 up 重新推出姿态（所以点完总是水平的）

点击环形按钮：  rotation = delta ⊗ 当前旋转   （delta 绕的是相机自己的轴）
                 └→ SceneView::FitToFocusWithRotation(rotation)
                    └ 姿态原样保留，只把相机放回"看向包围球中心"的位置
```

关键是**"叠加一次旋转"而不是"用方向反推姿态"**：按钮的效果等价于"相机不动、把物体在相机空间里绕对应轴转 45°"，所以相机的姿态必须是**在它自己当前姿态上再转一次**（把旧的 roll/俯仰带进去），而不是从旋转后的方向 + 世界 up 重新算（那样会把倾斜抹平）。两条路径最后都会落到 `SceneView::ApplyFitPose()`：先按包围球修投影/远裁剪面，再把相机放到 `球心 − forward × 距离`，于是**相机永远看向包围球中心**，距离也按包围球重新算。

细节：

- 轴与符号：偏航/俯仰沿用 `CameraController::HandleCameraOrbit()`（左右绕相机 up、上下绕相机 right），所以按钮和同方向的拖拽手感一致；东南/西南两个按钮绕**相机视线轴**转，所以视角原地滚转（相机位置不变，因为 forward 没变），用来补上第三个自由度；
- 有选中对象时用它的包围球，没有就用整个场景的（`GetFocusSphere()`）；
- 连续点击用的是**相机正在飞向的目标姿态**（`TryGetPendingPose()`），否则第二次点击会从一个飞到一半的姿态上再转，角度会偏小；
- 相机动画的"到达"判定必须**同时看位置和旋转**（`CameraController::HandleInputs()`）：滚转按钮的位置和终点重合，只比位置的话会在第一帧就判定到位，旋转变成瞬间跳变而不是动画；
- 想改步进角度用 `ViewCubeWidget::SetStepDegrees()`，默认 45°。

#### 失败时的行为

曲线描述不出面时（没闭合、方向接不上），`ShapeBuilder` 会**记日志并跳过**，`BuildArrowShapes()` 拿不到 4 个形状就回退到内置三角形箭头（`BuildFallbackShapes()`），不会让控件整体消失。日志前缀是 `[ShapeBuilder]` / `[ShapeBake]` / `[ViewCube]`。

#### 三个容易踩的点

| 坑 | 症状 | 原因 / 做法 |
| --- | --- | --- |
| **"看起来接上了"不等于接上了** | 日志 `a sub path with N curves does not connect (error 2)`，子路径被丢弃 | OCCT 连 wire 用的是顶点坐标 + `Precision::Confusion()`（1e-7）。我们自己算的点是 float，坐标 60~130 时误差 ~1e-5，而 OCCT 按圆心+角度算出来的弧端点是 double —— 差 1e-5 就"差得很远"。所以：子路径的端点用 double 保存，每段曲线加进去后**回读 OCCT 的 `LastVertex`** 作为真实终点，并且圆弧在"起点 ≈ 当前点（<1e-3）"时直接用当前点当起点 |
| 忘了 `MoveTo` | 日志提示自动起了一条子路径 | 第一条曲线前必须先 `MoveTo` |
| 容差按"草图单位"给 | 圆弧要么棱角明显要么点多 | `flattenDeflection` 用的是**作者空间**的单位；按像素画就给 0.05px 量级 |

---

## 13. 约定与坑

**必须遵守**

1. 控件内部只用左上原点逻辑像素；GL 换算只出现在"最后一米"（`glViewport` / 着色器 uniform）；
2. 布局、翻转、命中都用同一个 `Interactor->GetSize()`；
3. 可点击的东西必须走 `HitShape`（绘制与命中同一份几何）；装饰性图形才写在 `DrawContent()` 里；
4. 形状位置变了要 `MarkGeometryDirty()`；
5. 不要重写 `onUpdate / onMouseMove / onLeftMousePressed / onLeftMouseReleased`（已被 `final` 封口），改用 11.1 节列的那组钩子；
6. 交互代码里不要读 ImGui 状态（`WantCaptureMouse` 之类）。

**容易踩的**

| 坑 | 现象 | 原因 |
| --- | --- | --- |
| 在 `Pressed` 期间刷新 hot | 高亮闪到别的形状上 | 锁存期间不按光标改写（基类已处理） |
| 忘了 `MarkGeometryDirty()` | 拖拽时命中区停在旧位置，而绘制在新位置 → 看起来"点不准" | 形状缓存只在脏标记置位时重建 |
| 光标移出视口不清 hover | 按钮一直亮着 | 需要 `Enter/Leave` 维护 `CursorInsideViewport`（基类已处理） |
| 拖拽中控件被禁用不释放 | 相机被永久挡住 | 需要在不可交互时 `EndDrag()`（基类已处理） |
| 混用两套坐标 | 控件随相机远近漂移、圆弧半径对不上 | 把世界空间算出的屏幕坐标直接当局部坐标用 |
| 用遍历顺序决定重叠层级 | 谁在上层随注册顺序变化 | 用 `SetZOrder()` |
| 采样 wire 不看边的 orientation | 形状看着对，但**永远不填充** | 反向边被倒着采 → 同一条线段走两遍 → 自接触环（见 12.7） |
| 假定 loop 首尾一定接上 | 同上，且日志里没有任何线索 | `ScreenPath` 会隐式补一条弦把环"闭上"，要显式验证并报警（见 12.7） |

---

## 14. 关键文件索引

| 文件 | 说明 |
| --- | --- |
| `Moon/Interactive/Screen/ScreenLayout.h/.cpp` | `ScreenRect` / `EScreenAnchor` / `ScreenLayout::Resolve()` |
| `Moon/Interactive/Screen/HitShape.h/.cpp` | 形状 + 命中 + 绘制 |
| `Moon/Interactive/Screen/ScreenPath.h/.cpp` | 任意轮廓：包围盒 / 拟合 / even-odd 填充判定 / 描边距离 |
| `Moon/Interactive/Screen/ShapeBuilder.h/.cpp` | 曲线 → wire → 面 → 轮廓：`ShapeBuilder`（代码造曲线）、`ConnectEdgesToWires()`、`BakeShapeFaces()` |
| `Moon/Interactive/Screen/SketchPathBake.h/.cpp` | 草图 → 轮廓烘焙（转调 `BakeShapeFaces()`）+ `FitWiresInto()` 拟合 |
| `Moon/Interactive/Screen/ScreenWidget.h/.cpp` | 基类：状态机、捕获、光标、绘制入口 |
| `Moon/Interactive/Screen/ScreenOverlayRegistry.h/.cpp` | 光标归属仲裁 |
| `Moon/Interactive/Widgets/ViewCubeWidget.h/.cpp` | 按下即触发的例子；形状用 `ShapeBuilder` 画的四个箭头 |
| `Moon/Interactive/Widgets/SplitScreen.h/.cpp` | 捕获拖拽的例子（`PathTraceRenderPass` 读它的线方程） |
| `Moon/Interactive/Widgets/PathShapeWidget.h/.cpp` | 路径形状（自定义形状）的验证控件 |
| `Moon/Interactive/Interactive/RenderWindowInteractor.h/.cpp` | 光标 / 尺寸 / Enter-Leave（2D 层的输入来源） |
| `Moon/Interactive/EventWidget.h/.cpp` | 事件回调映射、生命周期、`isVisible()` |
| `Moon/Interactive/Im3DType.h/.cpp` | `ComputeViewCubeLayout()`：立方体视口与 `ScreenLayout` 共用 |
| `Moon/renderer/GizmoRenderPass.cpp` | 控件注册与开关（Passes 面板也读这里） |
| `Moon/renderer/SceneView.cpp` | `HandleActorPicking()` 里的 `BlocksSceneCursor` 让位 |
| `Moon/renderer/CameraController.cpp` | `HandleInputs()` 里的 `IsCapturing` 让位 |
| `Moon/Interactive/Im3DRenderer.cpp` | `drawSort()` 里的 `HitsShape` 让位；控件绘制时机（`drawWidgets()`） |
