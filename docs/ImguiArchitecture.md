# Qtimgui 模块架构设计文档

> 本文基于 `Moon/Qtimgui` 目录下的实际代码整理，覆盖 Dear ImGui 核心、Qt 后端（imguiwidgets）、ImPlot 与自定义扩展，以及它们在 Moon 编辑器中的集成方式。

---

## 1. 概述

### 1.1 模块定位

`Moon/Qtimgui` 是 Moon 编辑器内置的 **Immediate Mode GUI（立即模式 GUI）** 子系统，提供：

- 完整的 Dear ImGui 运行时（布局、控件、字体、绘制列表）；
- 面向 Qt 的窗口/输入/渲染后端（`imguiwidgets`）；
- 基于 ImPlot 的绘图扩展（`implot`）；
- 项目自定义的绘制扩展（`implotCustom`）与轻量 Gizmo（`imguizmo`）。

该模块不独立成 CMake 目标，而是被 `Moon` 可执行目标通过 `file(GLOB_RECURSE ...)` 整体编入（见 [Moon/CMakeLists.txt](../Moon/CMakeLists.txt)）。

### 1.2 上游版本

| 组件 | 版本 | 来源 |
| --- | --- | --- |
| Dear ImGui | `1.93.0 WIP`（`IMGUI_VERSION_NUM 19294`） | 上游 [ocornut/imgui](https://github.com/ocornut/imgui)，`imgui/` 目录 |
| ImPlot | `1.1 WIP`（`IMPLOT_VERSION_NUM 10100`） | 上游 [epezent/implot](https://github.com/epezent/implot)，`implot/` 目录 |
| Qt 后端 | 项目自研 | `imguiwidgets/QtImGui.*`、`ImGuiRenderer.*` |

编译期定制（[imconfig.h](../Moon/Qtimgui/imgui/imconfig.h)）：

- `IMGUI_ENABLE_FREETYPE`：字体图集使用 FreeType 光栅化；
- `#define ImDrawIdx unsigned int`：顶点索引为 32 位；
- 未启用 Docking / Multi-Viewport（相关代码被注释）。

### 1.3 与项目中另一套 imgui 的关系

仓库里还有一套 `Moon/Interactive/imgui/`（仅 `imgui.h` + `imgui_draw.cpp`），它是项目早期自研的迷你立即模式 2D 绘制库，全部符号位于 `namespace MOON`，被 `Im2DRenderer`（2D 覆盖层渲染）使用。

两套 imgui **并存且符号隔离**：

| | `Moon/Qtimgui/imgui` | `Moon/Interactive/imgui` |
| --- | --- | --- |
| 命名空间 | 全局 `ImGui::`（Dear ImGui） | `MOON::` 内自定义类型 |
| 用途 | 主视口 3D 覆盖层、调试窗口、设置面板 | 2D 覆盖层绘制（`Render2D::Im2DRender`） |
| 渲染 | 自带 OpenGL 后端（`ImGuiRenderer`） | 走 MoonRender HAL（VertexBuffer 等） |

本文只讨论 `Moon/Qtimgui`。

---

## 2. 总体架构

### 2.1 分层视图

```
┌──────────────────────────────────────────────────────────────┐
│ 应用层（Moon 编辑器）                                          │
│  Im3DRenderer / ViewerWidget / debugOpenGlWidget / DebugSetting│
└──────────────────────────────┬───────────────────────────────┘
                               │ QtImGui::initialize/newFrame/render
┌──────────────────────────────▼───────────────────────────────┐
│ Qt 后端（imguiwidgets）                                        │
│  QtImGui（门面）→ WindowWrapper（QWidget/QWindow 抽象）         │
│  ImGuiRenderer（输入装配 + OpenGL 渲染后端）                    │
└──────────────┬───────────────────────────────┬───────────────┘
               │ ImGui API                      │ ImDrawData
┌──────────────▼──────────────┐  ┌──────────────▼───────────────┐
│ Dear ImGui 核心（imgui/）    │  │ 扩展（implot/）               │
│  布局/控件/绘制列表/字体      │  │  ImPlot 绘图、ImPlotCustom、  │
└─────────────────────────────┘  │  ImGizmo 视图轴               │
                                  └──────────────────────────────┘
                               │ OpenGL 3.3 / GLES 3.0
┌──────────────────────────────▼───────────────────────────────┐
│ QOpenGLWidget 的 GL 上下文（默认帧缓冲）                        │
└──────────────────────────────────────────────────────────────┘
```

### 2.2 目录结构

| 目录/文件 | 职责 |
| --- | --- |
| `imgui/` | Dear ImGui 上游源码：`imgui.cpp`（核心）、`imgui_draw.cpp`（绘制列表）、`imgui_widgets.cpp`（控件）、`imgui_tables.cpp`（表格）、`imgui_demo.cpp`（演示窗口）、`imgui_freetype.cpp`（FreeType 字体光栅化）、`imstb_*`（STB 依赖） |
| `imguiwidgets/QtImGui.h/.cpp` | Qt 集成门面 API：`initialize / newFrame / render`，`QWidget`/`QWindow` 两种包装 |
| `imguiwidgets/ImGuiRenderer.h/.cpp` | 核心后端：输入事件转换、ImGui 上下文管理、OpenGL 渲染 |
| `implot/` | ImPlot 绘图库（`implot.cpp/items/demo`） |
| `implot/implotCustom.h/.cpp` | 项目自定义绘制扩展：旋转文本 `AddTextTransform`、色标条 `ColormapScale` |
| `implot/imguizmo.h/.cpp` | 项目自定义轻量视图轴 Gizmo（`ImGizmo` 命名空间） |

### 2.3 数据流总览

每帧（Qt 的 `paintGL` 内）：

1. `QtImGui::newFrame(ref)`：装配 `ImGuiIO`（时间、显示尺寸、鼠标/滚轮/按键状态），调用 `ImGui::NewFrame()`；
2. 应用代码调用 ImGui / ImPlot 控件构建 UI；
3. 应用代码调用 `ImGui::Render()`，Dear ImGui 生成 `ImDrawData`（顶点/索引/绘制命令）；
4. `QtImGui::render(ref)`：`ImGuiRenderer` 把 `ImDrawData` 提交到当前 GL 上下文。

输入则分为两条路径：

- **事件驱动**：Qt 事件 → `eventFilter` → 写内部状态（按键、滚轮、剪贴板）；
- **帧时轮询**：`newFrame` 内读 `QCursor::pos()`、窗口活动状态 → 写 `io.MousePos` 等。

---

## 3. 公共 API（QtImGui）

定义见 [QtImGui.h](../Moon/Qtimgui/imguiwidgets/QtImGui.h)。

| 函数 | 说明 |
| --- | --- |
| `RenderRef initialize(QWidget*, bool defaultRender = true)` | 将 ImGui 绑定到 `QWidget`（`QT_WIDGETS_LIB` 下可用） |
| `RenderRef initialize(QWindow*, bool defaultRender = true)` | 绑定到原生 `QWindow` |
| `void newFrame(RenderRef ref = nullptr)` | 开始新一帧；`nullptr` 时使用全局单例渲染器 |
| `void render(RenderRef ref = nullptr)` | 渲染上一帧的 `ImDrawData` |

`RenderRef` 实际是 `QWindowWrapper*`（`WindowWrapper` 子类）的 `void*`，由 `QtImGui::newFrame/render` 反解。**不持有原始窗口的强引用**：`QWindowWrapper` 只存 `ImGuiRenderer*`，`QWidgetWindowWrapper`/`QWindowWindowWrapper` 额外存原始 `QWidget*`/`QWindow*` 指针，用于事件过滤、尺寸、光标等查询。

### 3.1 两种初始化模式

- `defaultRender = true`：使用 `ImGuiRenderer::instance()` 全局单例，适合单窗口/共享渲染器场景；
- `defaultRender = false`：每次 `initialize` 新建独立 `ImGuiRenderer`，由 wrapper 析构时删除（除非它就是单例），适合多窗口各自独立上下文。

当前项目两处调用均传 `false`（见第 10 节）。

---

## 4. 核心类设计

### 4.1 WindowWrapper（窗口抽象）

纯虚接口，抹平 `QWidget` 与 `QWindow` 的差异（[ImGuiRenderer.h](../Moon/Qtimgui/imguiwidgets/ImGuiRenderer.h)）：

| 虚函数 | 用途 |
| --- | --- |
| `installEventFilter` | 安装 ImGui 事件过滤器 |
| `size` | 逻辑像素下的窗口尺寸 |
| `devicePixelRatio` | 高分屏缩放因子（QWidget 用 `devicePixelRatioF`） |
| `isActive` | 窗口是否活动（决定鼠标位置是否有效） |
| `mapFromGlobal` | 全局坐标 → 窗口局部坐标 |
| `object` | 返回被包装的 `QObject*`（事件过滤比对用） |
| `setCursorShape` / `setCursorPos` | 光标形状与位置（`QT_NO_CURSOR` 下为空实现） |

两个实现分别包装 `QWidget`（`QWidgetWindowWrapper`）与 `QWindow`（`QWindowWindowWrapper`）。

### 4.2 ImGuiRenderer

继承 `QObject` 与 `QOpenGLExtraFunctions`，是 Qt 后端的心脏：

- **上下文**：`ImGuiContext* g_ctx`（每渲染器一个）；
- **输入状态**：`g_MousePressed[3]`、`g_MouseWheel/H`、`g_Time`；
- **GL 资源**：字体纹理、shader program、VAO/VBO/IBO、uniform/attribute 位置；
- **职责**：`initialize`（建上下文）、`newFrame`（装配 IO）、`render`（提交绘制）、`eventFilter`（消费 Qt 事件）、`createDeviceObjects`/`createFontsTexture`（懒创建 GL 资源）。

### 4.3 生命周期

```
initialize()
  ├─ new ImGuiRenderer（defaultRender=false）或取单例
  ├─ ImGui::CreateContext()          → g_ctx
  ├─ ImPlot::CreateContext()         → 全局 GImPlot
  ├─ 设置 io.BackendFlags/BackendPlatformName/剪贴板回调
  └─ window->installEventFilter(this)

newFrame() → 首次调用 createDeviceObjects()（编译 shader、建 VAO/VBO、生成字体纹理）

~ImGuiRenderer()
  ├─ ImPlot::DestroyContext()
  └─ ImGui::DestroyContext(g_ctx)
```

注意：`ImGuiRenderer::instance()` 是 **永不释放的单例**；`defaultRender=false` 创建的实例随 `QWindowWrapper` 析构删除。当前应用代码中 `imref` 是文件级静态变量，退出时不会显式销毁，因此两个渲染器的析构路径实际都不会在正常退出前执行。

---

## 5. 帧循环设计

### 5.1 主视口（ViewerWidget）

编排位于 [viewerwidget.cpp](../Moon/editor/View/sceneview/viewerwidget.cpp) 的 `paintGL`：

```
paintGL()
  ├─ ImRenderer::newImgui()          // QtImGui::newFrame(imref) + ImPlot::SetCurrentContext
  ├─ Im2DRender::newFrame()          // MOON 迷你 imgui 2D 覆盖层
  ├─ ImRenderer::newFrame(mSceneView)// 3D 即时绘制（网格、坐标轴、Gizmo 等）
  ├─ mSceneView->Update / Render / Present
  ├─ debugImgui()                    // ImGui::Image 显示 G-buffer 纹理（调试开关）
  ├─ Im2DRender::endFrame()
  └─ ImRenderer::endImgui()          // ImGui::Render() + QtImGui::render(imref)
```

要点：

- Dear ImGui 作为**最后一层覆盖**渲染在默认帧缓冲上（`mSceneView->Present()` 之后）；
- `ImGui::Render()` 由应用调用，`QtImGui::render()` 只负责把 `ImDrawData` 画出来；
- 3D 覆盖层的拾取/交互（`Im3DRenderer` 的 `makeHot/makeActive` 体系）与 ImGui 控件互不干扰，但共用 `GetForegroundDrawList()` 绘制圆点/线段等（如 `PointHandleGizmo::lineSplit`）。

### 5.2 调试窗口（debugOpenGlWidget）

[debugOpenGlWidget.cpp](../Moon/editor/Debug/debugOpenGlWidget.cpp) 是独立的 `QOpenGLWidget`，用 `startTimer(0)` 驱动持续重绘：

```
initializeGL → QtImGui::initialize(this, false) + ImPlot::CreateContext()
paintGL
  ├─ QtImGui::newFrame(imref)
  ├─ ImPlot::SetCurrentContext(ctx)
  ├─ glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject())
  ├─ showImGui()                     // 目前为空实现
  ├─ ImGui::Render()
  └─ QtImGui::render(imref)
```

### 5.3 newFrame 的输入装配

`ImGuiRenderer::newFrame()`（[ImGuiRenderer.cpp](../Moon/Qtimgui/imguiwidgets/ImGuiRenderer.cpp)）：

1. 首次调用时 `createDeviceObjects()`（shader/VAO/VBO + 字体纹理）；
2. `io.DisplaySize` = 窗口逻辑尺寸，`io.DisplayFramebufferScale` = DPR（每帧刷新，天然支持窗口缩放）；
3. `io.DeltaTime` 用 `QDateTime::currentMSecsSinceEpoch()` 计算，首帧固定 `1/60`；
4. 若 `io.WantSetMousePos`，先设置光标位置；
5. 窗口活动时 `io.MousePos` = `mapFromGlobal(QCursor::pos())`，否则置 `(-1,-1)`；
6. `io.MouseDown[3]` 取事件缓冲的 `g_MousePressed`，滚轮增量取 `g_MouseWheel/H` 后清零；
7. `updateCursorShape(io)` 同步 OS 光标；
8. `ImGui::NewFrame()` 开始新帧；
9. 末尾有一段**测试代码**：`ImPlotCustom::AddTextTransform(... "HelloWorld")` 与注释掉的 Demo 窗口。

### 5.4 render 的绘制提交

`render()` → `ImGui::GetDrawData()` → `renderDrawList()`，流程见第 7 节。

---

## 6. 输入系统

### 6.1 事件过滤

`ImGuiRenderer` 通过 `installEventFilter` 挂在窗口对象上，`eventFilter` 只处理**被包装对象自身**的事件（`watched == m_window->object()`）：

| Qt 事件 | 处理函数 | 效果 |
| --- | --- | --- |
| `MouseButtonPress/Release/DblClick` | `onMousePressedChange` | 更新 `g_MousePressed[0..2]`（左/右/中） |
| `Wheel` | `onWheel` | 累加 `g_MouseWheel` / `g_MouseWheelH` |
| `KeyPress/KeyRelease` | `onKeyPressRelease` | 转成 `ImGuiKey` 事件 + 字符输入 + 修饰键 |

事件过滤不吞事件，最终交还 `QObject::eventFilter`。

### 6.2 按键映射

`keyMap`（`QHash<int, ImGuiKey>`）把 `Qt::Key_*` 映射到 Dear ImGui 1.93 的 `ImGuiKey_*`，覆盖方向键、编辑键、`A/C/V/X/Y/Z` 等。映射在 `onKeyPressRelease` 中通过 `io.AddKeyEvent()` 上报；字符输入经 `io.AddInputCharacter()`（只处理单字符 `event->text()`）。

修饰键按平台区分：

- macOS：`Meta→Ctrl`、`Ctrl→Super(Command)`；
- 其他：`Control→Ctrl`、`Shift→Shift`、`Alt→Alt`、`Meta→Super`。

### 6.3 鼠标滚轮、剪贴板与光标

- **滚轮**：优先用 `pixelDelta`（像素模式），否则 `angleDelta()/120`；垂直方向每 5 行文本为 1 单位，水平方向按 1 行文本归一化；
- **剪贴板**：`io.SetClipboardTextFn/GetClipboardTextFn` 桥接 `QGuiApplication::clipboard()`，文本暂存于文件级静态 `g_currentClipboardText`（UTF-8）；
- **光标**：`cursorMap` 将 `ImGuiMouseCursor_*` 映射到 `Qt::CursorShape`，在 `updateCursorShape` 中应用；`io.MouseDrawCursor` 或 `ImGuiMouseCursor_None` 时隐藏 OS 光标。对应后端能力标志 `ImGuiBackendFlags_HasMouseCursors` 与 `HasSetMousePos`。

---

## 7. OpenGL 渲染后端

### 7.1 设备对象（懒创建）

`createDeviceObjects()` 在首个 `newFrame` 时执行：

- 编译链接内置 shader（顶点：`Position/UV/Color` → `Frag_UV/Frag_Color`，`ProjMtx` 变换；片段：`Out_Color = Frag_Color * texture(Texture, Frag_UV)`）；
- 查询 `Texture/ProjMtx` uniform 与 `Position/UV/Color` attribute 位置；
- 创建 1 个 VAO + 1 个 VBO + 1 个 IBO，按 `ImDrawVert`（20 字节：`pos` 2×float、`uv` 2×float、`col` 4×u8）配置顶点属性；
- `createFontsTexture()` 生成字体纹理（见 7.5）。

GLSL 版本由宏控制：桌面 `#version 330`，Android 下 `USE_GLSL_ES` 走 `#version 300 es` 并补 `precision mediump float;`。

### 7.2 每帧绘制

`renderDrawList()` 处理 `ImDrawData`：

1. 空帧（帧缓冲宽高为 0）直接返回；`draw_data->ScaleClipRects(DisplayFramebufferScale)` 处理高分屏；
2. 设置正交投影矩阵（像素坐标 → NDC）；
3. 绑定 VAO、uniform、纹理单元 0；
4. 逐 `CmdList`：把 `VtxBuffer`/`IdxBuffer` 以 `GL_STREAM_DRAW` 上传到 VBO/IBO；
5. 逐 `ImDrawCmd`：`UserCallback` 直接回调；否则按裁剪矩形设置 `glScissor`（Y 轴翻转）、绑定 `pcmd->GetTexID()`、`glDrawElements`（索引宽度按 `sizeof(ImDrawIdx)` 选 `GL_UNSIGNED_SHORT/INT`）。

### 7.3 GL 状态保存与恢复

渲染前备份：活动纹理、program、纹理/数组缓冲/顶点数组绑定、混合方程与因子、视口、裁剪框、`BLEND/CULL_FACE/DEPTH_TEST/SCISSOR_TEST` 开关；渲染后逐一恢复。这让 ImGui 可以安全地嵌入在既有的渲染器管线中间而不污染其状态。

渲染状态本身：`GL_BLEND`（`SRC_ALPHA, ONE_MINUS_SRC_ALPHA`）、无面剔除、无深度测试、开裁剪测试。

### 7.4 限制说明

- 未实现多视口（`ImGuiViewport`）支持，`clip_off=(0,0)`、`clip_scale=DisplayFramebufferScale` 的代码路径是预留的；
- 未使用 `ImDrawCmd::VtxOffset`（配合 32 位索引的顶点偏移特性未开启）；`ImGuiBackendFlags_RendererHasVtxOffset` 未设置，因此单个绘制列表顶点数受 `ImDrawIdx`（32 位）上限约束即可；
- 渲染时要求调用方保证 GL 上下文有效，且绘制到调用方当前绑定的帧缓冲（应用侧自行绑定 `defaultFramebufferObject()`）。

### 7.5 字体

`createFontsTexture()`：

- 路径：`<exe>/Moon/Data/Engine/Fonts/Ruda-Bold.ttf`（经 `Tools::Utils::PathParser::GetExeDirectory()` 拼接）；
- 配置：25px，`ImGuiFreeTypeLoaderFlags_LightHinting`（`IMGUI_ENABLE_FREETYPE` 生效，见 [imconfig.h](../Moon/Qtimgui/imgui/imconfig.h)）；
- 图集：`io.Fonts->GetTexDataAsRGBA32()` 导出，上传为 `GL_RGBA8` 纹理（`GL_LINEAR`、`GL_CLAMP_TO_EDGE`），纹理 ID 存入 `io.Fonts->TexID`；
- 若文件缺失，`AddFontFromFileTTF` 会失败（`io.FontDefault` 为空），后续帧依赖默认字体回退。工程已通过 CMake POST_BUILD 拷贝资源目录，字体正常情况下存在。

---

## 8. 扩展组件

### 8.1 ImPlot

完整的上游 ImPlot 绘图库（曲线、柱状、热图、子图、图例、色图等），与 Dear ImGui 共享同一帧循环：`BeginPlot → Setup* → Plot* → EndPlot`。项目内主要保留其能力与 Demo（`ImPlot::ShowDemoWindow()` 被注释），实际业务使用目前较少。

ImPlot 的上下文是**全局单例指针 `GImPlot`**（见 [implot.cpp](../Moon/Qtimgui/implot/implot.cpp) 的 `ImPlotContext* GImPlot`），不随 ImGui 上下文隔离。

### 8.2 ImPlotCustom（项目自定义）

定义于 [implotCustom.h](../Moon/Qtimgui/implot/implotCustom.h)：

- `AddTextTransform(pos, angle, col, text)`：在 `GetForegroundDrawList()` 上逐字形旋转绘制文本（支持 UTF-8、像素对齐、顶点预分配/回退）；
- `ColormapScale(label, val, min, max, ...)`：绘制带刻度的纵向色标条，并叠加指向当前值 `val` 的三角指示箭头（复用 ImPlot 内部 ticker/colormap 实现，需包含 `implot_internal.h`）。

### 8.3 ImGizmo（项目自定义）

[imguizmo.h](../Moon/Qtimgui/implot/imguizmo.h) 暴露极简 API：

| 函数 | 行为 |
| --- | --- |
| `SetRect(x, y, size)` | 设置 Gizmo 屏幕矩形 |
| `SetDrawList(drawlist)` | 选择绘制列表（默认前景层） |
| `BeginFrame(background)` | 以无装饰/无输入窗口包住 Gizmo 区域 |
| `DrawGizmo(view, proj, pivotDistance)` | 把世界坐标轴经 `view×proj` 投影到矩形内，绘制 XYZ 轴、正负端与中心圆 |

实现为一个**只读视图轴指示器**（返回 `false`，不含拖拽交互），目前仅有头文件被包含，应用尚未调用。

---

## 9. 构建与依赖

- 源码接入：`Moon/CMakeLists.txt` 的 `file(GLOB_RECURSE Moon_SOURCE ...)` 自动收录 `Qtimgui` 下全部 `.cpp`，无需单独列文件；
- 依赖：Qt（Core/Gui/Widgets/OpenGL/Svg）、FreeType（`${LibDIR}/freetype` 头文件与库，`Moon` 目标链接 `freetype`）、Boost（PathParser 所在工具链）；
- 编译要求：C++20，MSVC `/bigobj`（imgui/implot 单文件体量大）；
- 渲染 API：`QOpenGLExtraFunctions`（VAO、`glGenVertexArrays` 等），要求运行时上下文支持 OpenGL 3.3 Core 或 GLES 3.0。

---

## 10. 应用集成点

| 集成点 | 位置 | 用途 |
| --- | --- | --- |
| 3D 主视口 | [Im3DRenderer.cpp](../Moon/Interactive/Im3DRenderer.cpp) | `QtImGui::initialize(&GetService(ViewerWidget), false)`；`newImgui/endImgui` 包裹每帧；前景绘制列表用于 2D 交互元素 |
| 调试窗口 | [debugOpenGlWidget.cpp](../Moon/editor/Debug/debugOpenGlWidget.cpp) | 独立 `QOpenGLWidget`，定时器驱动重绘，验证 imgui 后端 |
| 设置面板 | [DebugSetting.cpp](../Moon/Settings/DebugSetting.cpp) | `ImGui::SliderScalarN` 等控件渲染调试参数 |
| G-buffer 调试 | [viewerwidget.cpp](../Moon/editor/View/sceneview/viewerwidget.cpp) | `ImGui::Image` 显示 position/normal/occlusion 纹理 |

两个主集成点都使用 `defaultRender=false`（每窗口独立渲染器），并在 `newFrame` 后各自 `ImPlot::SetCurrentContext(ctx)`。

---

## 11. 已知问题与改进建议

基于代码阅读整理，按影响排序：

1. **ImPlot 上下文重复创建且全局共享**
   `ImGuiRenderer::initialize()` 创建了一个 ImPlot 上下文，而每个调用方（`Im3DRenderer`、`debugOpenGlWidget`）又各自 `CreateContext()` 一次。由于 `GImPlot` 是全局指针、第二个 `CreateContext` 不会覆盖当前上下文，最终每窗口用自己那份 `ctx`，初始化时创建的那份变成“孤儿”且析构时机不确定（`ImPlot::DestroyContext()` 只销毁当前 `GImPlot`）。多窗口同时存在时 `GImPlot` 会在帧间被切换。
   *建议*：统一为“每 ImGui 上下文一个 ImPlot 上下文”，删除 renderer 内部或调用方的重复创建，并在渲染器析构时按所有权释放。

2. **newFrame 内嵌测试代码**
   `ImGuiRenderer::newFrame()` 末尾无条件绘制旋转的 “HelloWorld” 文本，属于调试残留。*建议*：移出后端，交给应用层。

3. **键盘状态未完整初始化**
   `initialize()` 里按键映射循环（`io.KeysData / KeyMap`）整体被注释，只有 `AddKeyEvent` 路径生效；`keyMap` 中 `Up/Down` 重复，且仅覆盖少量按键。*建议*：补全映射或用 Qt 扫描码直接映射。

4. **单例渲染器资源不释放**
   `ImGuiRenderer::instance()` 由 `new` 创建且永不删除；`RenderRef` 为裸指针，退出前无显式清理路径。*建议*：接入 RAII/退出钩子，或至少在进程退出前 `DestroyContext`。

5. **32 位索引与 VtxOffset**
   `ImDrawIdx` 已改为 32 位，但渲染端未声明 `ImGuiBackendFlags_RendererHasVtxOffset`，也未处理 `ImDrawCmd::VtxOffset`。目前数据量下无碍，属潜在上限问题。

6. **多视口 / Docking 未启用**
   `ImGuiConfigFlags_DockingEnable` 与 DockSpace 代码均为注释状态，渲染端也未实现多视口。若后续要 Editor Dock 布局，需同步启用后端多视口支持。

7. **两套 imgui 并存**
   `Moon/Interactive/imgui`（MOON 迷你版）与 Dear ImGui 并存，符号分属不同命名空间故不冲突，但概念上容易混淆、维护成本高。*建议*：长期可评估用 Dear ImGui 统一替代 2D 覆盖层。

---

## 12. 关键文件索引

| 文件 | 内容 |
| --- | --- |
| `Moon/Qtimgui/imguiwidgets/QtImGui.h` / `.cpp` | 门面 API、WindowWrapper 实现 |
| `Moon/Qtimgui/imguiwidgets/ImGuiRenderer.h` / `.cpp` | 输入装配、OpenGL 渲染后端、上下文管理 |
| `Moon/Qtimgui/imgui/imconfig.h` | 编译期配置（FreeType、32 位索引） |
| `Moon/Qtimgui/imgui/imgui.h` | Dear ImGui 公共 API |
| `Moon/Qtimgui/implot/implot.h` | ImPlot 公共 API |
| `Moon/Qtimgui/implot/implotCustom.h` / `.cpp` | 旋转文本、色标条扩展 |
| `Moon/Qtimgui/implot/imguizmo.h` / `.cpp` | 视图轴 Gizmo |
| `Moon/Interactive/Im3DRenderer.cpp` | 主视口集成（newImgui/endImgui） |
| `Moon/editor/Debug/debugOpenGlWidget.cpp` | 调试窗口集成 |
