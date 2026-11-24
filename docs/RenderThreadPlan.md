# 渲染线程分离：分步计划

目标：MOON CAD 的场景渲染跑在**独立的渲染线程**上，GUI 线程只提交"这一帧要什么"、粘贴上一帧的
结果。命令队列（`Rendering/Backend/`）已经就位，这里记录从"同线程"走到"真分离"的分步路径、
已经踩到的硬约束，以及每一步的验证方法。

## 1. 已经就位的部分

| 组件 | 说明 |
|---|---|
| `CircularBuffer` / `CommandStream` | 环形 arena；命令 = 函数指针 + 就地参数 + next 偏移（Filament 形状） |
| `CommandBufferQueue` | 批次发布/回收、`WaitForCommands`（消费侧）、`RequestExit`；录音侧 flush-on-full |
| `DriverAPI.inc` | 12 条命令（viewport/scissor/capability/blending/pipeline state/VAO/element draw/array draw/clear color/clear/material） |
| `OpenGLDriver` | 执行体：`Execute(begin,end)` 走命令链；`PipelineState` 的状态机 diff 和执行体的 applied-state 缓存都在这里 |
| 一条 draw | `DrawMaterial` + `PipelineState` + `DrawMesh`（携带网格指针，回放时才解析 VAO），**一个批次、批次内零 immediate GL** |
| `DeferredDeleter` | 资源删除排队到下一个 drain（`Mesh` 的 VAO/IBO 已经走这条路）；帧首与 `Build()` 末尾各一次 `Flush()` 是确定执行点 |
| `Driver::Enqueue(std::function)` | 逃生口（Filament 的 `queueCommand`）：把还没命令化的 GL 块按原样录进批次、回放侧执行。2D overlay 的绘制与状态恢复已经走它 |
| `Core::Context` / `Core::RenderModuleHost` | **引擎侧**（`MoonRender/include/Core/`，Qt-free）：引擎不认识 Qt，宿主通过 `Rendering::Context::IGLContextHost` 交进上下文，需要建在该上下文里的宿主对象（ImGui/gizmo）走 build 回调。编辑器侧 `Editor::Core::Context` / `Editor::Core::RenderModuleHost` 只是别名，`QtGLContextHost` 是该接口的 Qt 实现 |
| 批次执行期（direct mode） | 执行体跑批次时，驱动被调用就**当场执行**（`Driver::m_executing`），不再往环形缓冲里录。有了它，任意编辑器 GL 块都能整体包进一个 `Enqueue`：ImGui 的绘制、Im3D 的五个 overlay pass（gizmo/网格/光照/无排序/文字）已经这样搬进队列 |
| 帧尾单次 drain | `AView::Render` 在 overlay 之后、解绑 present target 之前调 `ExecutePendingCommands()`：这一帧所有 overlay 命令在这里、以正确的渲染目标执行。这是"一帧一次 flush"的落点 |

## 2. 硬约束（实测）

**`QOpenGLContext` 有线程归属**：`QtGLContextHost::MakeCurrent()` 在别的线程调用会直接失败——

```
Cannot make QOpenGLContext current in a different thread
```

（Qt 的 `QOpenGLContext::makeCurrent` 检查 `thread() == QThread::currentThread()`；
`QOffscreenSurface` 是窗口对象，也不能搬线程。）

所以**模块上下文必须在渲染线程上创建**，不能"GUI 线程建好、渲染线程拿来用"。这一点决定了下面
的顺序：上下文 -> 依赖它的所有 GL 对象 -> 帧。

**第二层约束（实测，2026-11-17 已两次撞到、均已回退）**：

1. 上下文在渲染线程上创建是可行的（日志确认 `the render module now has its own context`），
   但**帧用到的每一个 GL 对象都必须创建在同一个上下文里**：ImGui 的字体纹理/VAO、Im3D 的 gizmo
   VAO、pass 的 FBO、视图的渲染目标。曾试过"渲染线程上下文 + GUI 线程上下文共享"（Filament demo
   的形状），VAO 与 FBO 不在共享范围内，帧在 GUI 上下文里跑会崩。所以 `Build()` 与 `SceneView`
   必须整体在渲染线程上。
2. 把整帧（含 `FlushPendingRead()`）搬到渲染线程后，**编辑器侧不是线程安全的**：
   `QObject::startTimer: Timers can only be used with threads started with QThread`，随后
   `0xC0000005` 退出。文档读/feature 链会碰 Qt（定时器、信号、服务），而它又必须在建网格时拥有
   模块上下文（VAO 在模块上下文里）。

结论：**顺序要反过来 —— 先把"场景侧"与"GL 侧"拆开（M4），再把帧搬走（M3）。** 具体是：
GUI 线程只登记资源描述与场景变更，GL 对象（VAO/VBO/纹理/FBO）的创建与删除变成命令，在渲染
线程执行；网格上传天然属于后者。做完这一层，`FlushPendingRead()` 就成了纯 CPU 工作，帧搬线程时
才不需要它同时拥有上下文。

## 2.5 照 Filament 抄：线程与前端/后端模型（逐条对照）

路径相对 `D:\Project\C++\filament\filament\`。**以后实现按这张表来，不再自己发明"包一层 lambda"的做法。**

| Filament 的做法 | 位置 | 我们要做成 |
|---|---|---|
| 引擎创建时先起线程 `std::thread(&FEngine::loop, instance)`，再 `mDriverBarrier.await()` 等驱动就绪 | `src/details/Engine.cpp:165-175` | `Core::RenderModuleHost::Create` 先起线程，线程内建上下文+驱动，主线程用 condvar/latch 等"就绪" |
| 驱动（含 GL 上下文）在**驱动线程**里创建：`mDriver = mPlatform->createDriver(mSharedGLContext, cfg)` | `src/details/Engine.cpp:773` | 平台接口加 `CreateDriver(void* sharedContext)`；`QtGLContextHost` 在**该线程**里 new `QOpenGLContext` + `QOffscreenSurface`（实测可行） |
| `class Platform { virtual Driver* createDriver(void*, const DriverConfig&) = 0; }` | `backend/include/backend/Platform.h:238` | `Rendering::Context::IPlatform`，Qt 实现 = 现在的 `QtGLContextHost` |
| 驱动线程主循环 `while (true) { if (!execute()) break; }`；`execute()` = `waitForCommands()` → `driver.execute(buffer)` → `releaseBuffer(item)` | `src/details/Engine.cpp:825-829`、`1418-1435` | `Core::RenderModuleHost` 的线程体；用已有的 `CommandBufferQueue`（接口已对齐） |
| 前端刷新点：`FEngine::flush()` = `flushCommandBuffer(...)`；`Renderer` 在 begin/end/render 处调 `engine.flush()` | `src/details/Engine.cpp:724-727`、`src/details/Renderer.cpp:295/416/469` | `Driver` 拆成"前端录音（GUI 线程）+ `Flush()`（发布批次）"；`AView::Render` 帧首/帧尾各一次 |
| 同步：`flushAndWait()` = `driver.finish()` + `FFence::wait(timeout)` | `src/details/Engine.cpp:733-760` | 后端加 `Finish()`（`glFinish`）+ `Fence`（`glFenceSync`/`glClientWaitSync`），贴帧前等待 |
| 资源前后端分离：`HwVertexBuffer` 等由驱动线程创建/销毁，前端只持句柄 | `backend/src/` | `Mesh`/`Texture`/`ShaderProgram` 持句柄，`Upload` 变命令（M4） |
| ImGui 走引擎自己的绘制：`ImDrawData` → `VertexBuffer`/`IndexBuffer` + 每命令一个 `MaterialInstance`（裁剪用 `setScissor`）+ Renderable prim；字体图集是引擎 `Texture` | `libs/filagui/src/ImGuiHelper.cpp:190-274`、`104-126` | overlay 不写裸 GL：顶点进 HAL buffer、每命令一个 scissor + material、走 `DrawMesh`。这正是"present 进块后导航立方体箭头消失"的根因——裸 GL 与驱动状态机混用 |
| 每帧重建 Renderable + `syncThreads()`（fence）保证数据有效 | `libs/filagui/src/ImGuiHelper.cpp:216-273` | 顶点数据由命令携带（`CommandStream::Allocate` 的 span），执行体上传后立即绘制 |

**据此修正的顺序**：①线程骨架（平台接口 + 线程/就绪等待 + `while(execute())` + `Flush()`/`Finish()`+fence）；②资源前后端（M4）；③overlay 按 filagui 改成引擎绘制；④删脚手架 drain，帧首/帧尾各一次 `Flush()`。

**已落地（2026-11-17）**：Rendering::Context::IPlatform（对标 Filament 的 `Platform::createDriver`）与 `QtGLContextHost::CreateDriver/DestroyDriver`；`Core::RenderModuleHost::Create(platform, sharedContext, cb)` 与 `Core::Context(std::unique_ptr<Driver>)` 都在，编译通过、行为中性（现行路径未切换）。把活路径切到平台入口后**首帧 0xC0000005**（上下文创建成功、日志正常），原因待查——下一轮先用 cdb 拿栈，重点看 `AView::ModuleContextScope`（平台模式下每帧绑定语义）与 `Context` 在平台所建驱动上的初始化顺序。
**已落地并验证（2026-11-17）**：平台入口成为**活路径**——`ViewerWidget::CreateRenderModule` 现在把 `QtGLContextHost` 当 `IPlatform` 交进去，`Core::RenderModuleHost` 调 `CreateDriver(sharedContext)` 在**调用线程**上建上下文与驱动，然后 `Core::Context` 接管。逐像素验证：`head-1` vs 现在 **0 差异**，obj **0**，干净退出。**这里踩到的坑（已修）**：CreateDriver 最初在返回前 DoneCurrent()，而引擎随后在同一线程用 build 回调建 ImGui/字体/几何，QtImGui::initialize → initializeOpenGLFunctions → QOpenGLContext::shareGroup 就在**当前上下文为空**时 AV。cdb 栈：Moon!QtImGui::initialize → Moon!MOON::ImRenderer::init → Moon!Core::RenderModuleHost::RenderModuleHost+0x112。修法与 Filament 一致：**createDriver 返回时上下文保持 current**，由引擎在构建完成后自己关（BuildOnDriver 末尾 DoneCurrent）。下一步就是把这次 CreateDriver 调用搬进 std::thread（Engine.cpp:165-175 的 mDriverThread + mDriverBarrier），线程体是 while (execute())（Engine.cpp:1418-1435），执行侧用我们已有的 CommandBufferQueue::WaitForCommands/ReleaseBuffers。

## 3. 分步路径

### M4（先做）. 场景侧与 GL 侧分离

1. 网格/纹理/着色器的创建改成"句柄 + 描述 + 上传命令"：GUI 线程拿到句柄，渲染线程创建 GL 对象；
   删除走已经落地的 `Backend::DeferredDeleter`。
2. `FlushPendingRead()`（feature 链）留在 GUI 线程，只产生句柄与上传命令，不再直接 `new Mesh` 上传。
3. 判定标准：GUI 线程在任何时刻都不再让模块上下文 current。

### M3a. 上下文与整帧落到渲染线程（同步等待）

1. `RenderModuleHost` 先起线程，线程内创建 `QtGLContextHost`（`QOpenGLContext` + `QOffscreenSurface`）、
   `Context`、`ImRenderer::initGizmoGl()/init()`；创建结果通过一次等待回传给 GUI 线程。
2. `MakeCurrent/DoneCurrent` 限定为"只能在渲染线程调用"（加断言），并提供
   `RunOnRenderThread(std::function<void()>)`：投递一个 job、等它结束。
3. `ViewportFrame` 的 `SceneView` 创建/销毁改成 job（渲染目标、pass 的 GL 对象属于模块上下文）。
4. 帧拆成两段：
   - GUI 线程：`TreeViewSync::Flush()`、`ReadPassSettingsOnce()`、输入清理、粘贴；
   - 渲染线程 job：`FlushPendingRead()`（建 feature 链 = 上传网格）、`view->Update()`、
     `view->Render(...)`、overlay、`DoneCurrent()`。
5. 其余 10 处让模块上下文 current 的地方，改成 job（或让它们用各自的上下文）：

   | 文件 | 处 |
   |---|---|
   | `Moon/editor/View/sceneview/viewportframe.cpp` | 3 |
   | `Moon/editor/UI/MaterialPanel/MaterialPreviewWidget.cpp` | 3 |
   | `Moon/editor/UI/MaterialPanel/MaterialDesigner.cpp` | 2 |
   | `Moon/editor/UI/MaterialPanel/MaterialEditorPanel.cpp` | 1 |
   | `Moon/editor/UI/ContentBrowser/ContentBrowserPanel.cpp` | 1 |
   | `Moon/editor/UI/ContentBrowser/MaterialThumbnailRenderer.cpp` | 1 |

   （`viewerwidget.cpp` 的 4 处是 Qt 自己的 `QOpenGLWidget::makeCurrent()`，与模块无关。）

M3a 的收益：**场景帧不再在 GUI 线程发任何 GL**；主线程仍然等这一帧，所以没有并发、行为可逐像素验证。

### M3b. 去掉等待（帧管线）

1. 主线程提交 job 后立即返回，粘贴**上一帧**的结果（报文里带 fence：`GLsync` 或 `CommandBufferQueue`
   的批次完成信号）。
2. 双重/三重缓冲：`ViewportFrame` 持有 2 份 present 纹理 + 帧序号，渲染线程写第 N 帧时窗口贴第 N-1 帧。
3. 输入/相机状态需要双缓冲：渲染线程读的快照由 GUI 线程在提交时给出（今天的 `getInutState()` 是
   共享可变状态，要改成"提交时冻结"）。

### M4. 场景命令化（在 M3b 之后按需推进）

> **渲染线程属于引擎，不属于编辑器宿主。** MoonRender 是 Qt-free 的，所以驱动帧的线程、以及在
> 该线程上创建的上下文，都应当放在引擎里（Filament 的做法：Engine 持有 ackend::Driver 线程，
> 通过 Platform 接口让宿主提供上下文创建/交换链）。编辑器侧只做两件事：实现那个平台接口（用 Qt
> 创建共享上下文）、把渲染结果贴到窗口上。Editor::Core::RenderModuleHost 不该出现 QThread、
> std::thread 或 job 队列——它只是一个 Qt 上下文宿主。

STEP 导入在 JobSystem 工作线程里就直接 `new Mesh` → 上传顶点缓冲（`Feature::makeDone` 路径），
这在"渲染线程持有上下文"之后是跨线程发 GL。做法与 Filament 一致：主线程只登记资源描述与句柄，
上传/delete 变成命令，在渲染线程执行；配合已经落地的 `DeferredDeleter`。

**已经落地的两块**：

1. 绘制命令携带**网格**而不是解析好的 VAO 名（`DrawMeshRequest`），执行体在回放时才问
   `IMesh::GetVertexArrayId()`。这样 GL 名字不再经过录音侧，也让"上传延后到渲染侧"成为可能。
2. `Mesh::Upload()` 拆成公开的 `Upload()`（只写 CPU 侧的 stride）与私有的 `UploadNow()`（真正的
   上传：VBO/IBO/顶点数组布局）。GPU 那一半随时可以挪到渲染侧。

**试过并回退**（2026-11-17）：把 `Upload()` 直接投进 `DeferredDeleter` 队列，帧首再统一 flush。
结果坐标轴 gizmo 的箭头与原点球消失——gizmo/overlay 的几何**不走命令路径**，它们不等一次 drain，
所以"更早"的上传必须变成显式的资源屏障（上传完成才能绘制），或者先把 overlay/gizmo 也纳入命令流。
现在帧首与 `RenderModuleHost::Build()` 末尾各有一次 `DeferredDeleter::Flush()`，是"排队资源工作"
的确定执行点（目前只包含删除）。

## 4. 每一步的验证方法

- **像素 A/B**：把工作区退回提交点重编一版当参照（窗口尺寸会随会话变化，必须在同一会话内对比），
  跑 `SketchExample/3.moon` 与 `Resource/Moon/Data/Engine/Models/material_sphere.obj`，
  用 `Build/refactor-shots/compare.py` 逐像素比较；0 差异为通过。
  已知噪声：鼠标悬停会让模型出现高亮描边、拓扑手柄变红。**截图前必须把光标停到视口外**
  （例如屏幕左上角 5,5），否则同一份二进制两次跑都能差 6000 px；停住光标后同构建两次运行是 0 差异。
  另外**要等相机 fit 结束**（MOON_SHOT_DELAY_MS=15000）：6 秒时 fit 可能还在跑，画面会有
  上万像素的"差异"，那不是回归。
- **退出**：连续 5 次运行都干净退出（`exit 0`），并出图。
- **零 GL 断言**（M3a 起）：在 GUI 线程包一层"当前线程是否有模块上下文"的检查，帧路径里出现 GL
  调用即断言/记日志。

## 5. 已经踩过并回退的坑

### 5.1 渲染线程要用 QThread，不能用 std::thread（已解决）

FlushPendingRead() 这条路上有 Qt 的定时器/信号，std::thread 上会报
QObject::startTimer: Timers can only be used with threads started with QThread，随后崩溃。
换成一个 QThread 子类跑 job 循环后这条警告消失——Qt 至少"认识"这条线程。

### 5.2 线程模式的其余清单（QThread 方案已废弃）

曾经用 `QThread` 起过一个渲染线程并让帧整体在它上面跑。**该做法已删除**：引擎是 Qt-free 的，线程必须属于引擎（见 §2.5 的对照表），不能用 Qt 的线程类，也不该由编辑器宿主管理。
那一轮实测到的两条仍然有效：

- `std::thread` 上跑编辑器侧的文档读会撞 Qt（`QObject::startTimer: Timers can only be used with threads started with QThread`）——这正说明**场景侧要先和 GL 侧分开**（M4），渲染线程里不能有编辑器代码；
- 帧在渲染线程上跑时，控制器与 overlay 依赖的状态必须由命令流显式携带，不能依赖"当时 GL 恰好是什么"。

## 6. 记录：平台入口成为活路径（2026-11-17）
`ViewerWidget::CreateRenderModule` 现在把 `QtGLContextHost` 当 `IPlatform` 交进去；`Core::RenderModuleHost` 调 `CreateDriver(sharedContext)` 在**调用线程**上建上下文与驱动，`Core::Context` 接管该驱动（`Core::Context(std::unique_ptr<Driver>)`）。逐像素：head-1 vs 现在 **0 差异**，obj **0**，干净退出。

**踩到的坑（已修）**：`CreateDriver` 最初在返回前 `DoneCurrent()`，而引擎随后在同一线程用 build 回调建 ImGui 字体/几何，`QtImGui::initialize → initializeOpenGLFunctions → QOpenGLContext::shareGroup` 在**当前上下文为空**时 AV。cdb 栈（Release + /DEBUG 临时链接）：

```
Qt5Gui!QOpenGLContext::shareGroup
Qt5Gui!QOpenGLFunctions::initializeOpenGLFunctions
Moon!QtImGui::ImGuiRenderer::initialize
Moon!QtImGui::initialize
Moon!MOON::ImRenderer::init
Moon!Core::RenderModuleHost::RenderModuleHost+0x112
Moon!Core::RenderModuleHost::Create
Moon!MOON::ViewerWidget::initializeGL
```

修法与 Filament 一致：**createDriver 返回时上下文保持 current**，由引擎在构建完成后自己关（`BuildOnDriver` 末尾 `DoneCurrent`）。

下一步：把这次 `CreateDriver` 调用搬进 `std::thread`（Engine.cpp:165-175 的 `mDriverThread + mDriverBarrier`），线程体 `while (execute())`（Engine.cpp:1418-1435），执行侧复用 `CommandBufferQueue::WaitForCommands/ReleaseBuffers`。

### 6.1 驱动拆成前/后端（2026-11-17）

`Driver::Flush()`（前端：发布批次，对应 `FEngine::flush` 到 `flushCommandBuffer`）与 `Driver::ExecuteAll()`（后端：取批次、执行、归还，对应 `FEngine::execute`）；`ExecutePendingCommands()` 现在就是 `Flush(); ExecuteAll();`，单线程路径行为不变。验证：`head-1` vs 现在 0 差异，obj 0。

**踩到的坑（已修）**：`ExecuteAll` 最初用"环形缓冲为空"做早退，而 `Flush()` 之后环形缓冲本来就是空的（批次已发布到队列），于是批次永远不执行、空间不归还，运行到工作区起来后以 `0xCFFFFFFF` 退出。修法：早退只看 `TakeCommands()` 的结果，不看环形缓冲。

**下一步**：`Driver::WaitAndExecute()`（=`WaitForCommands` → 执行 → `ReleaseBuffers`，Filament 的 `execute()` 返回 false 表示退出），线程体 `while (execute())`。前提是 GUI 线程上不再有 immediate GL（`Present()`、resize、资源创建），即先把 overlay/present 改成 filagui 那样的引擎绘制——否则 GUI 线程在没有 current 上下文的情况下发 GL 会崩。

### 6.2 帧首 resize 进队列 + 阻塞式 execute（2026-11-17）

- `Driver::WaitAndExecute()`：Filament 的 `FEngine::execute()`（阻塞等批次 → 执行 → 归还；空表示队列要求退出）。线程体就是 `while (driver.WaitAndExecute()) {}`。
- `AView::Render` 的三处 FBO `Resize`（framebuffer / msaa / presentTarget）搬进了场景队列块：resize 会创建/释放 GL 对象，属于同侧工作。验证：`head-1` vs 现在 0 差异，obj 0。

**关于 `Present()`（仍未进队列）**：`AView::Present()` 调 `ABaseRenderer::Present(mat)`，后者构造一个 quad drawable 并走 `DrawEntity`（虚函数 → `CompositeRenderer::DrawEntity`，它读 `m_currentPass.value()`）。把它移进批次后导航立方体箭头消失，说明它不是"单纯一段 GL"，而是依赖 pass/renderer 的上下文。按 Filament 的做法，正确形态是把它变成一个**正常的 pass**（有 pso、material、mesh，走与场景一致的绘制路径），而不是继续调时序。


### 6.3 `Present()` 的进一步结论（2026-11-17）

查代码时发现一处**隐式前置条件**：`CompositeRenderer::DrawEntity` 直接 `m_currentPass.value()`，而 `Present()`（`ABaseRenderer::Present(mat)` → 一个 quad drawable → `DrawEntity`）是在 pass 之外调的。已把它改成显式分支：没有 current pass 时跳过 feature hooks、直接走 `ABaseRenderer::DrawEntity`（这是正确语义：outline/picking 这些 feature 是 per-pass 的，不是每个 draw 都过）。

但**把这处前置条件显式化之后，把 `Present()` 记进批次仍然丢导航立方体的箭头 overlay**（同一 1413 px、可复现）。所以原因不在那个 `value()`，而在别处——最可能是 overlay 块在**录制时**做的事（Im2D 的 `UploadGeometry`、Im3D 的图集上传）相对于一个"还没执行的 present"所处的状态不同。下一步的检查点：在两种顺序下分别打印 Im2D draw list 的命令数/顶点数，以及 overlay 执行前的 `GL_CURRENT_PROGRAM / GL_VERTEX_ARRAY_BINDING / GL_SCISSOR_BOX / GL_VIEWPORT / GL_ACTIVE_TEXTURE`，用数据定位。

目前 `Present()` 保持 immediate（唯一留在 GUI 线程的每帧 GL），其余都在队列里；`WaitAndExecute()` 与 resize 进块已落地并验证 0 差异。


### 6.4 网格上传延后的第二次尝试：同一结论，原因更清楚了（2026-11-17）

把 `Mesh::Upload` 投进 `Backend::DeferredDeleter`（这次 overlay 已经在队列里了）再跑：仍然后果完全一样——obj 场景 / 3.moon 的导航立方体区域 264 px（obj）与 2711 px（3.moon）差异，即**gizmo 的箭头/旋转图标仍消失**。

结论：那块几何**不走帧**——它在 `ImRenderer::initGizmoGl()` 里就被建好并画掉了（模块构建期间，任何 drain 之前）。所以"先记录、稍后上传"的设计必须给这类**初始化期绘制**提供显式上传屏障；这不是队列时序能绕过的。已回退为立即上传（代码里写明了原因）。

对应的做法（Filament）：它的 `VertexBuffer` 上传在驱动线程按批次执行，而"初始化期绘制"在 Filament 里同样是**帧内**发生的——没有谁在帧外直接画一笔。我们要么把 gizmo 图集的生成也做成帧内的一步（首选，与 filagui 一致），要么在 `initGizmoGl` 里显式 `DeferredDeleter::Flush()`。


### 6.5 追到 gizmo 几何的来源（2026-11-17）

读了 `ImRenderer::initGizmoGl()` → `prepareGl()`：它只**创建**资源（5 个 `Material`、overlay 的 VAO/VBO、以及 `preStoreMesh()`）；`preStoreMesh()` 里为 `CoordAxis`、`NavigateCube` 等建了 `Mesh`（每个 Mesh 的 `Upload` 都是在这里发生的）。

对照截图：延后上传后**消失的正是 3D gizmo 的箭头头部与原点球**（绿色/蓝色箭杆还在），而立方体本体与周围箭头是 ImGui/Im2D 画的、不受影响。所以问题落在 `preStoreMesh()` 建的这些 `Mesh` 上：它们的上传被排队后，第一帧的 `DrawMesh` 在回放时解析到的 VAO 还是 0（上传要到下一次 drain 才执行），而后续帧本应恢复——实测没有恢复，说明还有别的问题（可能是这些 Mesh 在首次绘制时才创建，或 VAO/IBO 的创建被排到了更后面）。

下一步（用数据，不再猜）：给 `Mesh::Upload` / `DeferredDeleter::Flush` / `DrawMeshR` 加临时日志（帧号 + 对象地址 + VAO id），跑 3 帧看上传与首次绘制谁先谁后；或者直接对照 Filament：**资源创建（含 VAO/IBO）是同步的、只有数据上传是按帧的**——若把 `Mesh` 的构造与 `AddSubRangeBuffer` 保持同步、只延后 `UploadNow`，并在首次绘制前提供一个显式 `Flush`，这条路才成立。


### 6.6 A/B 探针的结论：不是 GL 状态，是队列记账（2026-11-17）

给 `AView::Render` 加了 `MOON_PRESENT_IN_BATCH` 开关（默认关），并在 Im2D 队列 lambda 开头打印 GL 状态（`MOON_DUMP_GL`）。实测：

- **默认（Present 立即执行）**：每帧一行 `[GLdump] im2d progg=7 vao=0 ... viewport=0,0,W,H`，即 Im2D 的 lambda **确实被执行**；
- **`MOON_PRESENT_IN_BATCH=1`**：同样的运行，**一行 GLdump 都没有**——Im2D 的 lambda **从未执行**，但进程正常退出、帧照常保存。

所以丢的不只是"箭头"：**整块 overlay 命令没有被执行**。问题在队列记账而不是 GL 状态：某个批次发布后，走查在批次里提前停住（例如批次里出现第二个终止 `NoopCommand`，或发布范围与写入范围重叠），后续命令被 `TakeCommands` 取走却从未执行。

下一轮把它坐实（都很快）：在 `OpenGLDriver::Execute` 里按批次打印 `begin/end/命令条数/停止原因`，并对照 `CommandBufferQueue::Flush/TakeCommands/ReleaseBuffers` 的 `m_head/m_tail/m_freeSpace` 变化；同时按 Filament 的形状核对：`Engine::execute` 每批 `driver.execute(item.begin)`+`releaseBuffer(item)`，批次边界由 `CircularBuffer::GetBuffer()` 决定、**每个批次只含一个终止 Noop 且发布后不再写入**。

探针已全部移除；工作区回到绿（obj 0 差异，3.moon 仅为已知悬停抖动）。


### 6.7 队列记账的实测数据（2026-11-17）

在 `Driver::Flush/ExecuteAll` 与 `OpenGLDriver::Execute` 里加临时打印，比较两种模式的批次构成。结论：**两种模式执行的批次集合一致**（最后几批：40 B/2、80 B/3、136 B/8、160 B/3、32 B/2、48 B/2），`TakeCommands`→`Execute`→`ReleaseBuffers` 的记账没有丢批次。

因此 §6.6 里"Im2D 的 lambda 从未执行"的观察需要修正：那次探针比较的前提不成立（探针位置与两次运行的条件不一致）。现在能确定的是：**批次本身都在跑**，`Present()` 进批次后真正变化的只有"谁在什么时候执行"，而丢的是 3D gizmo 的箭头（`preStoreMesh` 那几个 `Mesh`），不是 overlay 整体。

下一步：回到 §6.5 的结论，用日志确认 `preStoreMesh()` 建的 Mesh 在首次绘制时 VAO 是否为 0；这条线索与 `Present()` 无关，是独立的资源/VAO 问题。

探针已全部移除，工作区绿（obj **0 差异**，3.moon **0 差异**——这次光标停住、fit 已结束）。


### 6.8 上传延后两次失败之后：结论与选择（2026-11-17）

两次尝试（overlay 在队列外 / 在队列内）都是同一现象：**gizmo 的箭头头部与原点球消失**，其余一切正常。已排除：队列丢批次（§6.7 实测批次集合一致）、`Present()` 时序（在队列外时同样失败）、overlay 是否在队列里（两种都失败）。

剩下两种可能，都需要资源层才能区分：

1. `preStoreMesh()` 建的 `Mesh` 在延迟上传期间被搬迁/重建，lambda 捕获的 `this` 失效（→ 上传落到野指针或静默失败）；
2. 那次绘制的 VAO/layout 建立晚于首次使用（首帧画空，后续本应恢复但实测没恢复）。

这两条都指向同一个结论，也就是 Filament 的做法：**上传不能对着裸对象指针延后，必须对着句柄（前端对象 + 后端资源）**。Filament 里是 `HwVertexBuffer` 这一类后端对象：前端只持句柄，上传按句柄排进驱动线程，对象的生命周期由引擎管理，不存在"捕获一个可能已经不在原地的 this"。

**决定**：在拿到句柄层之前，网格上传保持同步（当前状态，已验证 0 差异）。M4（资源前后端）是线程化的真正前置，而不是可以绕过的细节。下一步就做它：Mesh/Texture 前端只存描述与句柄，后端资源在驱动线程创建，上传按句柄排队。


### 6.9 三次尝试后的定论：先有句柄层，再谈资源搬家（2026-11-17）

这轮把"gizmo 初始化搬进帧内 + 上传屏障"也试了（initGizmoGl 从模块构建移到 `ViewportFrame::Render` 首次执行，`prepareGl` 末尾加 `DeferredDeleter::Flush()`，`Mesh::Upload` 延后）：**启动即 0xC0000005**。已整体回退，工作区回到已验证状态。

三次实验（延后上传 ×2、搬到帧内 ×1）都失败，方向一致地说明：**问题不在"什么时候上传"，而在"对着谁上传"。**裸对象指针 + 延后执行 = 生命周期无法保证；Filament 用 `Handle`（前端）+ `HwVertexBuffer`（后端）把这件事变成"按句柄排队"，才成立。

**定论**：下一步就是句柄层（M4），不做别的绕行。顺序：

1. `Backend::Handle<T>`（索引 + 世代，按 `filament/libs/utils/include/utils/Handle.h` 的形状）；
2. 后端的资源表（对每个 `T` 一个数组 + 空闲链表），创建/销毁走命令；
3. `Mesh` 前端只留句柄 + 描述，上传按句柄排队；`Texture` 同理；
4. 初始化期绘制（gizmo）改成帧内一步，并在需要时显式 flush。

另记一条验证方法：窗口尺寸**每次会话可能不同**（1254x480 / 1160x487）。参照图必须同尺寸比较；本次会话的绿基准已存为 `Build/refactor-shots/ref-1160-1.png` 与 `ref-1160-obj.png`，同尺寸两次运行差异为 **0**。


### 6.10 句柄层落地（2026-11-17，实际代码）

新增 `Rendering/Backend/ResourceHandle.h`：按 Filament 的 `utils/Handle.h` 形状，`ResourceHandle<T>` = 16 位槽号 + 16 位世代（`Index()/Age()/operator bool`），配 `ResourceHandleAllocator<T>`：回收槽位时世代 +1，**过期句柄不会解析到新主人**。命名避开了 OCC 的 `Handle(Class)` 宏（那个宏会把它改成 `opencascade::handle`）。

新增 `Resources/MeshRegistry.{h,cpp}`：句柄 → `Mesh*` 的表（互斥量保护，两侧都会碰）。`Mesh` 在构造函数里 `Register(this)`、析构函数里先 `Unregister` 再拆 GL 对象。

`Mesh::Upload` 现在是**按句柄寻址**的：队列里的工作捕获 `ResourceHandle<Mesh>` + 数据拷贝，执行时 `MeshRegistry::Find` 解析，解析不到就丢弃——这正是之前两次失败的根因（裸 `this` 可能在执行前失效）。

延后与否由能力开关决定：`DeferredDeleter::SetUploadsDeferrable/AreUploadsDeferrable`，**只有在帧跑在独立渲染线程上时才打开**（今天为 false → 立即上传）。代码路径是真实执行的，不是死代码：开关一开就换行为。

验证：obj **0 差异**；3.moon 仅几何区的已知悬停抖动（gizmo 区域一致）。

翻开关的前提（仍未做）：把 gizmo 的初始化期绘制搬进帧内，使它的上传与绘制走同一条队列路径。


### 6.11 上传延后真正跑通了（2026-11-17）

把 gizmo 的几何构建从模块构建搬进**第一帧**（`ViewportFrame::Render` 的 renderJob 开头、`MakeCurrent` 之后、`m_overlay->BeginFrame` 之前），并在它跑完之后才 `SetUploadsDeferrable(true)`。

效果：**上传延后已经真实生效**（文档读建的网格走 `DeferredDeleter` 队列、按 `ResourceHandle<Mesh>` 寻址、在下一次 drain 上传），而 gizmo 箭头**不再丢失**。

验证（改动前 `hg-*` vs 改动后 `gz-*`，同一会话同尺寸）：**两者都 0 差异**；obj 与绿基准 `ref-1160-obj` 也是 **0 差异**（含 gizmo 角落）。

之前三次失败的原因现在很清楚：gizmo 的几何在**构建期**创建并被**同一帧**绘制，那时上传还没跑（违障），而现在"创建"与"绘制"都在帧内、且创建早于开关打开，所以延后不会让绘制看到空 VAO。


### 6.12 Present 进队列：定位到具体缺失物（2026-11-17）

把 `Present()` 记成独立块后，差异仍存在；裁剪放大后可以**精确说出丢了什么**：导航立方体周围的**六个白色箭头**与**两个旋转图标**（都是 Im2D/ImGui 里带 **裁剪矩形** 的图像），而方块本体、左/前二字、3D 轴与原点球全部正常。

已排除：队列记账（批次构成实测一致）、上传时序（§6.11 已修并同时复测）、`DrawEntity` 的 pass 前置条件（已显式化）。

指向的结论：**带裁剪矩形的 overlay 图像**在"录制/回放分离"后失效——最可能是 `ImGuiRenderer::Render` 里的 `drawData->ScaleClipRects(io.DisplayFramebufferScale)` 在回放侧被调用、而 `ImDrawData` 同时被多于一处的执行路径触碰（或同一帧被执行两次），于是裁剪矩形逐次收缩到不可见。下一步就在那里打印 `CmdListsCount/ClipRect` 前后值确认。

目前 `Present()` 保持立即执行（唯一每帧 immediate GL），代码与 §6.11 一致，最终验证：obj 与绿基准 **0 差异**、3.moon 与 §6.11 状态 **0 差异**。


### 6.13 Present 进队列的缺失物：进一步缩小范围（2026-11-17）

用两个探针比较"Present 立即"与"Present 进队列"两种顺序：

1. `ImGuiGLRenderer::Render` 调用计数：每帧 **1 次**、`CmdListsCount=0`、`DisplayFramebufferScale=1`——**ImGui 与视口里的内容无关**（它只画面板），所以"ScaleClipRects 被重复调用"的猜测排除。
2. `Im2DRender` 的绘制命令：每帧 **6 条**、`elems=6`、纹理 83..88、裁剪矩形为整屏——这 6 条是导航立方体的**六个面**（每面一张纹理），也就是说缺的箭头/旋转图标**不是 Im2D 画的**。

结合 §6.12 的裁剪图：缺的是方块**周围**的六个白箭头与两个旋转图标，它们由 **Im3D 的图集路径**绘制（`ImRenderer::drawLit` 里 `ViewCubeLabels::IsDirty()` → `UpdateViewCubeLabels()` 的那一段，图集上传与这些四边形在**同一个队列块**里）。

下一步（需要断点或块内日志，不再猜）：在那一段打印"图集是否 dirty / 上传后纹理 id / 这些四边形的纹理 id 与 UV"，确认进队列时它们拿到的纹理是不是**还没上传完**的那一张；若是，把图集上传提前到块首（绘制之前）。

工作区：探针已全部移除；obj 与绿基准 **0 差异**；3.moon 的 2469 px 是**同一构建两次运行**之间的固有抖动（几何区 450,29-744,458，拓扑手柄悬停），不是回归。


### 6.14 决定性的新事实：缺失物与时间有关（2026-11-17）

找到那些图标的绘制点：ViewCubeWidget::DrawIcons 用 Im2D::AddImageQuad(IconCache::Get(svg, 20), ...)。两种顺序下 Im2D 的命令**完全相同**（每帧 6 条、纹理 83..88 一一对应）。

**但 2.5 秒时两种顺序逐像素完全相同（0 差异）**；15 秒时（相机 fit 结束）进队列那一版才丢图标。也就是说差异**与时间/状态有关**，不是帧内的执行顺序问题——之前几轮的"队列记账/上传时序"猜测因此都不成立。

由此可以推断（下一步验证）：fit 期间每帧都在变，fit 结束后帧内容稳定，某些**惰性资源**（`IconCache` 的纹理、或 `ViewCubeLabels` 图集）在这之后才被（重新）上传；若上传发生在"已经引用它的命令之后"，那一版就会看到空纹理。验证方法：在 `IconCache::Get` 返回 0 与返回非 0 时各打印一次（带帧号），跑 15 秒看是否在上传前返回过 0。

工作区：探针已全部移除；obj 与绿基准 **0 差异**。Present() 仍保持立即执行。


## 7. 渲染资源的前后端分离：现状与缺口（2026-11-17）

Filament 的形状（对照本机源码）：前端是 `Texture`/`VertexBuffer` 这类**只有描述与句柄**的对象，后端是 `HwTexture`/`HwVertexBuffer`，由驱动线程在命令里创建（`createTexture`、`update2DImage`）；前端句柄在引擎里解析成后端对象，GL 名字只在后端出现。

**我们已经有的**：

- 网格数据上传：ResourceHandle<Mesh> + MeshRegistry + DeferredDeleter 队列，执行侧按句柄解析后上传；  已实测生效（§6.11）。
- 贴图句柄表：ResourceHandle<HAL::Texture> + Backend::TextureRegistry（CreateHandle / Publish / Find / Release），  Resources::Texture 在构造时领取句柄、析构时释放，SetTexture 时发布后端对象。

**仍然不是分离的（就是这个缺口）**：

1. **贴图的创建**还在调用线程：HAL::Texture 构造函数里 glCreateTextures（GLTexture.cpp:32），   PrepareTexture 里 glTexImage2D + glGenerateMipmap（TextureLoader.cpp）。→ 必须变成命令。
2. **材质按裸指针绑纹理**：MaterialInstance 的纹理参数是 HAL::Texture*，    绑定时直接 GetID()；调用点很多（Im3DRenderer 里 10 处以上的 SetProperty(...Texture)、ABaseRenderer::Blit、缩略图/材质编辑器）。    → 必须改成按句柄绑定，在**回放侧**解析成 GL 名字。
3. Resources::Texture::GetTexture() 仍直接返回后端对象，调用方（材质、ImGui 图集、视口覆盖）假定它随时有效。

**落地顺序（下一步就按这个做）**：

1. `MaterialPropertyType` 增加 `ResourceHandle<HAL::Texture>` 变体；`MaterialInstance::Bind` 对句柄走 `TextureRegistry::Find`（解析不到就跳过并记一次告警）。
2. 所有 `SetProperty(name, HAL::Texture*)` 调用点改成传句柄；对**直接 new 出来的** `HAL::Texture`（`Im3DRenderer` 的 `uAtlas`/`edgeTexture`/`blockTexture`）在创建处登记句柄，创建本身同样走命令。
3. `TextureLoader::Create/CreateFromMemory` 改成：前端只建 `Resources::Texture`（描述 + 句柄），像素数据拷贝进命令；执行侧 `new HAL::Texture` → `Publish` → `Allocate/Upload`。
4. 之后才谈线程：序列里已经有"创建"、"上传"、"绘制"三类命令，渲染线程只是把执行搬走。

现状：编译通过、obj 与绿基准 **0 差异**（本轮只加了句柄表，不改行为）。


### 7.1 实测：只把绑定改成按句柄解析会改变 gizmo 画面（2026-11-17）

尝试：MaterialInstance 的两处纹理解析（`UploadProperties` 绑定处、`SetProperty` 登记处）改成 `TextureRegistry::Find(asset->GetHandle())`，解析不到再退回 `GetTexture()`。

结果：obj 场景出现 **702 px** 差异，全部集中在 gizmo 立方体面（1049,45-1123,103）——说明有材质拿到的后端对象与之前**不是同一个**。可能原因：同一 `Resources::Texture` 的后端对象在运行中被替换（`SetTexture` 重新发布）而绑定侧仍按旧句柄解析，或句柄槽位在析构与重建之间被复用。已回退，回到 obj **0 差异**。

结论：按句柄绑定与创建走命令**必须一起改**，单独改绑定会暴露后端对象被替换的时序，画面就变了。顺序：先把创建搬进命令（后端对象只由渲染侧产生、发布一次、不再替换），再把绑定改成按句柄解析。


### 7.2 句柄与分配器改成 Filament 的形状（2026-11-17）

按 ilament/backend/include/backend/Handle.h 与 HandleAllocator.h 重写：

- `ResourceHandleBase`：HandleId + 
ullid（UINT32_MAX）+ operator bool / clear() / getId()；`ResourceHandle<T>` 增加类型安全的派生转换与比较（不加世代号——Filament 也不加，过期检测在 debug 里靠 tag）；
- `ResourceHandleAllocator<T>`：槽位数组 + 空闲表，接口是 Filament 的 Allocate / Release / `handle_cast` / `SetObject`（`handle_cast` 就是 Filament 的 handle_cast<D*>）；
- 分配器**只拥有槽位、不拥有对象**：槽里存的是引擎持有的对象指针（Mesh、HAL 贴图），这正是前后端的分界——句柄是前端引用，对象是后端资源。

贴图数据上传也已经走队列（TextureLoader::QueuePrepare）：像素拷进 queued callable，执行侧用 `TextureRegistry::Find(handle)` 找目标再 Allocate/Upload；在 `AreUploadsDeferrable()` 为假时仍同步执行。验证：obj 与绿基准 **0 差异**。

仍未搬走：HAL::Texture 的构造（glCreateTextures）与材质按裸指针绑定，见 §7 的 1/2 两条。


### 7.3 让注册表持有后端贴图对象：画面回归，已回退（2026-11-17）

尝试把贴图后端对象的所有权从 `Resources::Texture` 移到 `TextureRegistry`（Filament 的 `HandleAllocator` 就是持有对象的），并让加载器先建前端对象、再由注册表建后端对象。

结果：obj 场景出现 **702 px** 差异，位置固定在 gizmo 立方体的正面（1049,45-1123,103）——说明有调用方缓存了 `HAL::Texture*`（材质属性、gizmo 的面缓存），对象所有权一换，它们拿到的就不是同一个了。即使把 `Create` 改成"每个句柄只建一次"也没能消掉。已完整回退，obj 回到 **0 差异**。

**保留的成果**：

- `ResourceHandleBase`/`ResourceHandle<T>`/`ResourceHandleAllocator<T>` 是 Filament 的形状（
ullid、handle_cast、槽位+空闲表）；
- `TextureRegistry`（非拥有，Publish/Find/Release）与 `MeshRegistry`（拥有槽位、指向引擎对象）；
- 网格与贴图的数据上传都可以走队列（AreUploadsDeferrable），网格那条已生效。

**下次要做所有权迁移，先做这一步**：把所有缓存 `HAL::Texture*` 的地方（材质属性、PolygonMesh::texture/edgeTexture/blockTexture、ImRenderer 的 `uAtlas` 等）改成存**句柄**，在回放的绑定点再解析。所有权和绑定必须一起换。


### 7.4 采样器上传时按前端对象解析（保留）与一次失败的所有权尝试（2026-11-17）

**落地**：`MaterialInstance::UploadProperties` 的采样器循环现在对**有 asset 的**采样器在**上传时**解析后端对象：`m_descriptorSet.SetTexture(binding, &asset->GetTexture(), asset)`，不再沿用设置属性时缓存的那个指针。这就是"绑定晚于创建"的前置条件。验证：obj 与改动前 **0 差异**。

**失败**：把 `Resources::Texture::GetTexture()` 也改成优先查 `TextureRegistry::Find(m_handle)`、解析不到才用成员指针，obj 场景直接崩（0xC0000409）。说明别的持有者会解析到**不同对象**，正是 §7.3 说的耦合。已回退，obj 回到 **0 差异**。

下一步（顺序不能颠倒）：

1. 让所有持有者只存句柄（`PolygonMesh::texture/edgeTexture/blockTexture` 已是 `Resources::Texture*`，但 `ImRenderer` 的 `uAtlas`、`BvhService` 的几处、`IconCache` 仍取 HAL 指针）；
2. 再把后端对象所有权移进 `TextureRegistry`（创建与销毁都成为命令），`GetTexture()` 才能只查表；
3. 最后把创建搬进命令流，渲染线程只是执行者。

当前状态：编译 exit 0；obj 与绿基准 **0 差异**（连续三次运行逐像素一致）。


### 7.5 描述符集的前后端分离（2026-11-17，已落地并验证）

按 Filament 的结构改：`Descriptor::TextureInfo` 现在分三部分——

- `handle`：**前端**持有的 `ResourceHandle<HAL::Texture>`（提交时才解析）；
- `direct`：**后端**用的 `HAL::TextureHandle*`，只由后端资源填充（渲染目标附件、引擎 1x1 回退）；
- `asset`：可序列化的资源对象（.mat 存路径）。

**提交步骤**：`Descriptor::ResolveTexture()` 把句柄解析成后端对象（`TextureRegistry::Find`），由 `DescriptorSet::Bind` / `BindFixed` 在**绑定前**调用——正好是"上下文所在的一侧"。`Descriptor::Bind` 随后绑的是 `direct`，也就是后端资源。

验证：obj 与改动前 **0 差异**，与绿基准 **0 差异**；3.moon 正常。

这就是 §7.3/§7.4 里"绑定必须晚于创建"的具体形态。剩下的一步：把 `TextureRegistry` 变成**拥有**后端对象，创建与销毁都成为命令；那时前端只剩句柄与描述符，渲染资源只在持有上下文的一侧产生。


### 7.6 贴图上传变成 driver 命令（2026-11-17，已落地并验证）

按"这些操作要做成 driver 的命令"改：

- `TextureLoader::RecordPrepare` 不再在调用线程做 `Allocate`/`Upload`，而是`driver->Enqueue(...)` 记录一条命令，命令里**自带一份像素数据拷贝**（std::shared_ptr<std::vector<uint8_t>>），执行侧用 `TextureRegistry::Find(handle)` 找目标后上传；
- 执行顺序由环形缓冲保证：先入队的创建/上传先执行，后入队的绘制解析到的句柄必然已经有效——这正是 Filament 的语义，不需要"晚绑定"这种额外机制；
- 没有渲染模块时退回同步执行（与改动前完全一致）。

验证：obj 与改动前 **0 差异**，与绿基准 **0 差异**；3.moon 正常。

还差的（按 Filament 的完整形态）：

1. 把 `CreateTexture`/`SetImage` 做成 `DriverAPI.inc` 里的**类型化命令**（现在是自定义命令携带 lambda），参数里带 `TextureDesc` 与像素 span（用 `CommandStream::Allocate` 放进环形缓冲，而不是堆上 shared_ptr）；
2. `TextureRegistry` 拥有后端对象，创建/销毁都走命令；
3. 删掉过渡形态：`Descriptor::TextureInfo::direct`、`Descriptor::ResolveTexture()`、以及 `Resources::Texture` 的 `m_texture`（前端只剩句柄与描述）。


### 7.7 贴图上传做成 driver 的具名命令（2026-11-17，已落地并验证）

按 Filament 的 `update2DImage` 形状，上传不再是调用点的 lambda：`r

- `DriverAPI.inc` 增加一条类型化命令：`DECL_DRIVER_COMMAND(UploadTexture, (TextureUploadRequest p_request), (std::move(p_request)))`；
- 记录端是具名方法 `Driver::UploadTexture(request)`，内部只做 `EnqueueUploadTexture`（没有 lambda）；
- 执行端是 `OpenGLDriver::UploadTextureR(request)`：按句柄取后端贴图 → `Allocate` → `Upload` → 可选 `GenerateMipmaps`；
- 请求（`TextureUploadRequest`）自带描述符与**像素拷贝**，随命令进环形缓冲，执行完由命令析构释放（CommandType<...>::Command::Execute 会在调用后显式调析构）；
- 句柄以整数 id 传递，避免请求头把 HAL 拖进 Driver.h（会形成包含环）。

验证：obj 与改动前 **0 差异**，与绿基准 **0 差异**；3.moon 干净退出。

**方法备注**：本轮之后的工作方式固定为——先在本机 Filament 源码里找到对应设计（backend 的 `DriverAPI.inc`、`CommandStream`、`HandleAllocator`、filagui 等），照它的结构改，再验证；不再自己发明结构。


### 7.8 贴图创建搬进命令的**前置依赖**（2026-11-17，下一步的入口）

要让 `CreateTexture` 也变成命令（把 `glCreateTextures` 挪到执行侧），必须先解决**一处仍然在录制期索要 GL 名字**的地方：

`Moon/editor/UI/IconCache.cpp:67` —— `return texture->GetTexture().GetID();`，返回值被塞进 `Im2D::TextureId`（即 Im2D 的绘制命令里的 `cmd.textureId`）。也就是说 **Im2D 的绘制列表携带的是裸 GL 名字**，任何被它引用的贴图必须在**录制期**就存在。

Filament 没有这个问题：filagui 把 **`filament::Texture*`（前端对象）** 放进 `ImGui::Image` 的 `ImTextureID`，真正的 GL 名字是渲染侧执行绘制时才解析的（`libs/filagui/src/ImGuiHelper.cpp`）。

所以顺序是：

1. Im2D::TextureId（以及 `cmd.textureId`）改携带**句柄 id**，绘制循环里用 `TextureRegistry::Find` 解析成后端贴图；ImGui 自带字体纹理这类"后端自有资源"直接传 GL 名字的路径要区分开（加一个标志位或高位标记）；
2. 之后 `IconCache` 只传句柄，贴图创建就能安全地变成命令；
3. 再让 `TextureRegistry` 拥有后端对象、销毁走 `DeferredDeleter`；
4. 最后删掉 `Resources::Texture::m_texture`、`Descriptor::TextureInfo::direct`、`Descriptor::ResolveTexture()`。

现状：编译 exit 0；obj 与绿基准 **0 差异**；UploadTexture 已是类型化命令（§7.7）。


### 7.9 Im2D 的纹理标识改成句柄（2026-11-17，已落地并验证）

照 filagui 的做法（ImTextureID 放前端对象、执行时解析），

- `Im2D::TextureId` 增加"句柄标记"：最高位为 1 时低 32 位是 `ResourceHandle<HAL::Texture>` 的 id；没有标记的值仍是裸 GL 名字（ImGui 字体图集、白色像素这类后端自有资源）；0 仍表示无贴图。辅助函数：`TextureIdFromHandle` / `IsHandleTexture` / `HandleOfTexture`；
- `Im2DRender` 的绘制循环里解析：句柄 → `TextureRegistry::Find` → `GetID()`，这一步发生在**回放侧**，所以被它引用的贴图可以由更早入队的命令创建；
- `IconCache::Get` 改成返回 `TextureIdFromHandle(texture->GetHandle().GetId())`，不再在录制期索要 GL 名字。

验证：obj 与改动前 **0 差异**，与绿基准 **0 差异**；3.moon 干净退出。

下一步（§7.8 的第 2 步）：贴图创建本身变成命令——`DECL_DRIVER_COMMAND(CreateTexture, …)`，执行端在注册表槽位里 `new HAL::Texture`（`glCreateTextures`）+ `Allocate(desc)`，`TextureRegistry` 拥有对象、销毁走 `DeferredDeleter`；`TextureLoader::Create/CreateFromMemory` 只记录 `CreateTexture` + `UploadTexture`。注意 `CreateMutable`（`IconCache` 用它）还要支持后续按名字 `Upload`，那条也要变成命令（`UpdateTexture`）。


### 7.10 CreateTexture 命令与注册表所有权：本轮未完成，已回退（2026-11-17）

尝试：DECL_DRIVER_COMMAND(CreateTexture, …) + OpenGLDriver::CreateTextureR（在注册表槽位里 new HAL::Texture 并 Allocate）+ TextureRegistry 拥有 unique_ptr 槽位（销毁走 DeferredDeleter）+ Resources::Texture 只留句柄。

卡住的地方（都要一起改，否则编译或运行就会半途而废）：

1. TextureLoader.cpp 的四个入口（Create / CreateFromMemory / CreateMutable / Reload）都要从"同步创建"改成"记录命令"，其中 CreateMutable 与 Reload 还会在**调用点立刻** GetTexture()（IconCache、BvhService、Im3DType），这些调用点必须改成"后续都走命令"；
2. Im3DType 的两处 SetTexture(unique_ptr)（纹理缓冲）要改成 TextureRegistry::Create + Allocate；
3. 一旦某处仍在录制期 GetTexture()，创建命令还没执行就会空解引用（Release 下直接崩），所以这四件事必须**同一个改动里**完成并一起验证。

本轮为了不让工作区停在半成品，把这部分回退到上一绿状态：CreateTextureRequest.h 保留（未接线），DriverAPI.inc 的 CreateTexture 条目与执行体已移除，TextureRegistry 回到非拥有，Resources::Texture 回到"持有对象 + 句柄"。

验证：obj 与上一绿状态 **0 差异**，与绿基准 **0 差异**；3.moon 干净退出。

下次做这件事时按 §7.9/§7.10 一次改完这四个文件（DriverAPI.inc、OpenGLDriver.cpp、TextureRegistry.{h,cpp}、Resources/Texture.{h,cpp}、TextureLoader.cpp、Im3DType.cpp、TextureUploadRequest.h），再一次性验证。


### 7.11 CreateTexture 成为命令、注册表拥有后端对象（2026-11-18，已落地并验证）

按 Filament 的 `createTexture` 形状补齐了最后一块：`r

- `DriverAPI.inc`：`DECL_DRIVER_COMMAND(CreateTexture, (CreateTextureRequest p_request), (std::move(p_request)))`；
- 记录端：`Driver::CreateTexture(request)`（内部只做 `EnqueueCreateTexture`）；
- 执行端：`OpenGLDriver::CreateTextureR(request)` → `TextureRegistry::Create(handle, type, name)`（真正的 `glCreateTextures` 在这里）→ 可选 `Allocate(desc)`；
- `TextureRegistry` **拥有**后端对象（`unique_ptr` 槽位），销毁排队到上下文侧（`DeferredDeleter`）；
- `Resources::Texture` **只剩句柄 + 路径**，`GetTexture()` 通过注册表解析；
- `TextureLoader`：Create/CreateFromMemory 记录 `CreateTexture` + `UploadTexture`，CreateMutable/Reload 走注册表直接创建（调用方马上要用），Im3DType 的纹理缓冲也走注册表。

**资源加载期是同步语义**：记录完创建/上传后立刻 `driver->ExecutePendingCommands()`，因为紧接着的调用（上传、材质属性、gizmo 绘制）会在这侧索要对象——Filament 在资源设置处同样 flush 并等待。

**踩到的坑（这次终于定位）**：Resources::Texture 的默认构造没有领句柄，而运行时纹理（Im3D 的 `edgeTexture`/`blockTexture`）正是默认构造出来的 → 句柄为空 → 材质里解析不到 → 立方体的棱边高光消失（**702 px**，出现在之前两次所有权尝试里）。默认构造现在也领句柄，问题消失。

验证：obj 与上一绿状态 **0 差异**，与绿基准 **0 差异**；3.moon 干净退出。

后续可清理的过渡：资源加载期的 `ExecutePendingCommands()`（等所有调用点都不再在录制期索要对象后可以去掉）；`Descriptor::TextureInfo::direct` 与 `ResolveTexture()` 也可以简化成"绑定时按句柄取"。


### 7.12 Present 进队列的复测与回退；对最早基准的逐像素校验（2026-11-18）

贴图那条线做完后复测 `Present()` 进队列：**更糟**——同一构建两次运行差 **53%**（318866 px）。原因与之前的判断一致：`Present()` 走 `ABaseRenderer::DrawEntity`，依赖 renderer 里"当前 pass"与绑定目标的隐式状态，录制后再回放时那份状态已经不属于它。已回退为立即执行，并在代码里写明**正确做法是把它做成真正的 pass**（自己的 pso / material / mesh / target，由 pass 循环绘制），而不是把这行调用录进队列。

**逐像素校验（本轮最有价值的数据）**：窗口这次回到 1254x480，与最早的绿基准同尺寸，于是直接比：`r

- `head-obj`（重构前基准） vs 现在（obj 场景）：**0 差异**（max delta 0）；
- `head-1`（重构前基准） vs 现在（3.moon）：**0 差异**（max delta 0）。

也就是说这一阶段的全部重构——环形缓冲、driver 命令、句柄层（`ResourceHandle` / `HandleAllocator` 形状）、注册表拥有的后端贴图、CreateTexture/UploadTexture 命令、Im2D 句柄化——**对画面零影响**。

同构建两次运行的 obj 也是 **0 差异**（确定性）。


### 7.13 Present 进队列终于成立；根因是 Im2D 的状态设在块外（2026-11-18）

两处改动，合起来解决了那个折腾很久的 **1413 px**：`r

1. **Present 成为 pass**：`PresentPass`（`Rendering/Core/PresentPass.h`）+ `CompositeRenderer::PresentTo(source, target)`——显式设置 `m_currentPass`、绑定目标、设置视口、再画 quad，不再依赖 renderer 里遗留的隐式状态（Filament 的 blit pass 是同一思路）；
2. **Im2D 的 GL 状态搬进绘制块内**：`EndFrame` 里原本先记录 6 条状态命令并**立即 drain**，绘制块（回放时才跑）依赖那份状态；一旦 present 被录进队列，它会在两者之间执行、改掉混合状态，于是 Im2D 的图标（导航立方体周围六个箭头 + 两个旋转图标）在关闭混合后**完全透明**。`r
   现在这些命令在 lambda **内部**（回放时）执行：driver->EnqueueCapability(...) 等在块首，状态与依赖它的绘制在同一处。

验证（PresentTo 已录进场景块）：

- obj：head-obj（重构前基准） vs 现在 → **0 差异**（max delta 0）；同构建两次运行 → **0 差异**；
- 3.moon：head-1/head-2 各自 vs 现在 → **0 差异**（两次运行分别落在基准的两个稳定状态上，两次之间仍是那 2447 px 的固有悬停抖动）。

**这意味着 GUI 线程每帧已无 immediate GL**：场景 pass 树、Present、Im2D/ImGui/Im3D 全部是环形缓冲里的命令。下一步就是线程本身：`Core::RenderModuleHost` 起 `std::thread`，线程内 `Platform::CreateDriver` + `while (driver.WaitAndExecute())`，GUI 线程只录制与贴图（见 §2.5 的对照表）。


### 7.14 线程化前的最后一份清单（2026-11-18）

现在每帧的 GL 全在环形缓冲里（场景 pass 树、Present pass、Im2D/ImGui/Im3D），剩下的是**资源创建路径**上的 GUI 线程 GL：`r

| 位置 | 内容 | 需要的命令 |
|---|---|---|
| `TextureLoader::CreateMutable` / `Reload` | `HAL::Texture` 构造（`glCreateTextures`）+ `Allocate` | `CreateTexture(allocate=true)` + `UpdateTexture`（已有） |
| `BvhService.cpp`（8 处 gl） | 纹理/缓冲 `Allocate` | 同上（`CreateTexture` 需支持再次分配） |
| `ShaderLoader.cpp` + `GLShaderStage.cpp`（各 7 处 gl） | **着色器编译/链接** | `CreateProgram` / `CreateShaderStage` 命令 |
| `HAL::VertexArray` / `Buffer` 等在加载期构造 | `glGen*` | 网格上传已延后；**VAO/IBO 的构造**仍需命令 |

已经在回放侧执行、无需再动的（看着是裸 GL，但都在队列块里）：`PathTraceRenderPass`（45 处）、`Im3DGLRenderer`（34 处）、`Im3DType`（19 处）、`PointRenderPass`、`PickingRenderPass`、`Im3DRenderer` 的图集上传（在 Im3D 块内）。

本轮新增：`UpdateTexture` 命令（`UpdateTextureRequest` + `OpenGLDriver::UpdateTextureR` + `HAL::Texture::UploadSubImage`，即 Filament 在可变贴图上的 `update2DImage`）。验证：obj 与基准 **0 差异**；3.moon 与上一状态 **0 差异**。

线程化的顺序（每步都要绿）：

1. `CreateTexture` 支持再次分配（`allocate` 时不跳过），把 `CreateMutable`/`Reload`/`BvhService` 改成命令；
2. 着色器编译做成命令（`CreateProgram`，源码随命令走）；
3. VAO/IBO 的构造做成命令；
4. 上述清零后：`Core::RenderModuleHost` 起 `std::thread`（线程内 `Platform::CreateDriver`），`Driver::ExecutePendingCommands()` 在线程模式下变成 `Flush()` + 等批次完成（`flushAndWait`），GUI 线程只剩录制与贴图。


### 7.15 资源创建继续命令化：UpdateTexture、可重分配 CreateTexture、CreateMutable（2026-11-18）

- `UpdateTexture` 命令：子区域上传（`UpdateTextureRequest` → `OpenGLDriver::UpdateTextureR` → `HAL::Texture::UploadSubImage` → `glTextureSubImage2D`），对应 Filament 在可变贴图上的 `update2DImage`；
- `CreateTextureRequest` 增加 `pixels` / `generateMipmaps`，执行端允许**再次分配**（可变贴图随数据增长原地扩容，BVH 的逐三角形缓冲是主要用户）；
- `TextureLoader::CreateMutable` 改成记录 `CreateTexture(allocate=true)`（像素随请求走）+ 立刻 drain，调用方随后仍能拿到对象，而 GL 发生在执行侧。

验证：obj 与基准 **0 差异**；3.moon 落在基准的两个稳定态之一（2447 px 为已知悬停抖动）。

剩余 GUI 线程 GL（线程化的最后清单，§7.14 减去本轮）：

1. `BvhService.cpp`：3 处 `Allocate(desc)`（其中 2 处还直接 `new HAL::GLTexture`，需要把裸后端贴图也纳入句柄模型）；
2. `ShaderLoader.cpp` / `GLShaderStage.cpp`：着色器编译与链接（各 7 处 gl），需要 `CreateProgram` 命令；
3. `HAL::VertexArray` / `Buffer` 的构造（`glGen*`）。


### 7.16 BvhService 的贴图分配改为命令（2026-11-18）

新增局部辅助 `BvhRecordTextureAllocate`（记录 `CreateTexture(allocate=true)`，像素随请求走，然后 drain 让对象对随后的材质可用），并把 `v->GetTexture().Allocate(desc)` 那处（BVH 的逐三角形信息缓冲）改成调用它。

验证：obj 与基准 **0 差异**；3.moon 与上一状态 **0 差异**。

**踩到的坑（记下来避免重复）**：`BvhService.cpp` 是 **UTF-8 with BOM**，用不带 BOM 的 UTF-8 覆写后，MSVC 会按 ANSI 读整个文件，文件里原有的中文字面量全部损坏（报`常量中有换行符`）。改这类文件必须保留 BOM（`New-Object System.Text.UTF8Encoding($true)`）。

剩余 GUI 线程 GL（继续）：`BvhService` 里还有 2 处直接 `new HAL::GLTexture`（需要把裸后端贴图纳入句柄模型）、着色器编译（`ShaderLoader`/`GLShaderStage`）、HAL 的 VAO/Buffer 构造。


### 7.17 渲染线程骨架落地（默认关闭），以及阻塞点（2026-11-18）

按 Filament 的 `Engine::loop` 落地了线程骨架，用环境变量 `MOON_RENDER_THREAD` 开关（**默认关闭**，默认路径逐像素 0 差异）：

- `CommandBufferQueue::WaitForIdle()`（已发布批次全部释放的信号）+ `m_outstanding` 计数；
- `Driver::SetRenderThreadActive()/IsRenderThreadActive()`：`ExecutePendingCommands()` 在线程模式下变成 `Flush()` + `WaitForIdle()`（Filament 的 `flushAndWait`，无 fence 版）；`RequestExit()` 通知队列退出；
- `Driver::WaitAndExecute()`：线程体（阻塞等批次 → 执行 → 归还）；
- `Core::RenderModuleHost`：线程模式下先起 `std::thread`，线程内 `m_platform->CreateDriver(...)`（即 Filament 的 `Platform::createDriver` 在驱动线程里调用）→ 构建 → 置就绪（主线程等待）→ `while (WaitAndExecute())` → 在**同一线程**销毁 Context；析构时 `RequestExit() + join()`；
- 线程模式下 `MakeCurrent/DoneCurrent` 不再绑定（Qt 规定上下文只能在其创建线程 current，否则进程直接 abort），`AView` 的 `ModuleContextScope` 也跳过绑定。

**线程模式的当前阻塞点**（开 `MOON_RENDER_THREAD=1` 实测）：跑到第一帧后，Qt 在 `QOpenGLWidgetPrivate::initialize/paint` 里对某个上下文 `makeCurrent` 报 `Cannot make QOpenGLContext current in a different thread`。栈显示它发生在 widget 自己的绘制路径里，说明模块上下文与 widget 共享组 + 在两个线程创建的组合触到了 Qt 的线程断言。下一步要么让 widget 侧不再依赖共享组（用独立的 FBO/纹理传递帧），要么让模块上下文在 GUI 线程创建、仅把命令执行搬到线程（但那与 Qt 的线程规则冲突）。

验证（默认路径）：obj 与基准 **0 差异**；3.moon 与上一状态 **0 差异**。


### 7.18 线程模式：定位到构建回调碰了 Qt 控件（2026-11-18）

开 `MOON_RENDER_THREAD=1` 后仍在第一帧 abort，报 `Cannot make QOpenGLContext current in a different thread`。按 Qt-Filament demo 的差异排查后，结论落在模块构建回调上：

构建回调里跑的是 `MOON::ImRenderer::init()`，它调用 `QtImGui::initialize(&GetService(ViewerWidget), ...)`，而 `QtImGui` 会拿那个 `QWidget` 去做 `makeCurrent()`。线程模式下这个回调跑在渲染线程上，于是 Qt 对 widget 自己的上下文做了跨线程 `makeCurrent` → 进程 abort。

所以线程化还差一步结构性拆分：构建回调分两半——

1. Qt/ImGui 的窗口侧初始化（`QtImGui::initialize`、输入绑定）留在 GUI 线程，在起线程之前、widget 上下文 current 时做；
2. 引擎侧与 GL 资源（gizmo 几何、字体图集上传）放进渲染线程的构建回调。

顺带把共享源也按 demo 改了：`MOON_RENDER_THREAD` 下与 widget 自己的上下文共享（而不是 Qt 全局共享上下文，后者属于 GUI 线程的 context group，跨线程共享会让 widget 的 `makeCurrent` 触发 Qt 线程检查）。默认路径不受影响。

验证（默认路径）：obj 与基准 0 差异；3.moon 落在基准的两个稳定态之一。


### 7.19 QtImGui 从构建回调里拆出去；线程模式从 abort 变成挂起（2026-11-18）

按最小改动：ImRenderer::init() 只留引擎/GL 侧，新增 `initHost(QWidget&)` 承载窗口侧（`QtImGui::initialize` + `ImPlot::CreateContext`），由 `ViewerWidget::initializeGL` 在 widget 上下文仍 current 时、`doneCurrent()` 之前调用。

效果：线程模式下 Qt 的跨线程 makeCurrent 报错消失了（另外补回了 `AView::ModuleContextScope` 在线程模式下不绑定的守卫——它之前被一次文件重写冲掉了）。现在线程模式表现为**挂起**而不是 abort：GUI 线程在等批次完成，渲染线程要么没进循环、要么卡在 GL 调用里。

下一步：用 cdb 在挂起时抓全线程栈（`~*kb`）。上一次尝试里 cdb 的继续执行不会返回，需要外部超时后再中断并抓栈，或者用 procdump 抓 dump 再分析。

验证（默认路径）：obj 与基准 **0 差异**；3.moon 落在基准的两个稳定态之一。


## 8. ImRenderer 迁移进 MoonRender（渲染核心的一部分）

目标：ImRenderer（场景交互/叠加绘制）整体属于渲染核心，应住在 `MoonRender`，引擎对 Qt 零依赖。

依赖测量（迁移前）：

| 文件 | 行数 | Qt 命中 | 结论 |
|---|---|---|---|
| Im3DType.h | 348 | 0 | 已搬（`Rendering/Overlay/Im3DType.h`，旧路径留转发头） |
| Im3DEffect.{h,cpp} | 132 / 18 | 0 | 已搬（`Rendering/Overlay/Im3DEffect.*`，旧路径留转发头） |
| Im3DType.cpp | 607 | 仅图集（QImage） | 待搬：图集改由引擎侧钩子提供 |
| Im3DRenderer.{h,cpp} | 437 / 2262 | 10（QtImGui、ViewerWidget） | 待搬：输入/光标走宿主接口 |

本轮已落地：`Rendering/Overlay/ViewCubeLabels.{h,cpp}`（引擎侧）——`FaceUvRect` 的数字表与 `AtlasBuilder` 钩子（编辑器注入绘制实现，引擎只取像素）。编辑器侧的 `ViewCubeLabels::FaceUvRect` 改为转发。

验证：obj 与基准 **0 差异**；3.moon 与上一状态 **0 差异**。

下一步：把 `Im3DType.cpp` 的 `QImage` 段改成 `ViewCubeLabels::BuildAtlas`（引擎钩子）+ 编辑器注册实现，然后把该文件搬进 `MoonRender/src/Rendering/Overlay/`；接着给 `ImRenderer` 加 `IOverlayHost`（光标/输入/窗口句柄）后整体搬迁。


### 8.1 本轮迁移进度（2026-11-18）

已搬进 `MoonRender`（旧路径都留了转发头，调用点未动）：

- `Rendering/Overlay/Im3DType.h`（此前）、`Rendering/Overlay/Im3DEffect.{h,cpp}`（此前）；
- `Maths/MathUtil.{h}` + `src/Rendering/Overlay/MathUtil.cpp`；
- `Rendering/Overlay/ScreenLayout.{h,cpp}`；
- `Rendering/Overlay/ViewCubeLabels.{h,cpp}`：UV 数字表 + `AtlasBuilder` 钩子 + dirty 标志；编辑器侧改为转发并自注册绘制实现（QImage → RGBA8）；
- `src/Rendering/Overlay/Im3DType.cpp`：图集改为向引擎钩子取像素，文件本身已无 Qt 依赖。

验证：obj 与基准 **0 差异**；3.moon 落在基准的两个稳定态之一。

剩下最后一块：`Moon/Interactive/Im3DRenderer.{h,cpp}`（2262 行，10 处 Qt：QtImGui 与 GetService<ViewerWidget>）。需要：IOverlayHost（光标位置/输入状态/窗口句柄）由编辑器实现，QtImGui 的三处调用（initialize/newFrame/render）换成引擎侧的 `ImGuiGLRenderer` + 宿主接口。做完这块，ImRenderer 就整体住在引擎里了。


### 8.2 QtImGui 解耦完成；Im3DRenderer 的搬迁被依赖链挡住（2026-11-18）

**落地**：`Rendering/Overlay/IImGuiHost.h`（引擎接口：NewFrame() / Render()）——`ImRenderer` 不再直接碰 `QtImGui`，改为 `SetImGuiHost(IImGuiHost*)` + `CreatePlotContext()`；编辑器在 `ViewerWidget::initializeGL` 里用 `QtImGuiHostAdapter`（包住 `QtImGui::initialize/newFrame/render`）注册。这样 ImGui 的窗口侧由宿主实现，引擎只调两个方法——与 filagui 的分工一致。

**受阻**：把 `Im3DRenderer.{h,cpp}` 文件搬进引擎时，依赖链暴露出编辑器子系统：`r

- `LineDashEffect.h` → `Interactive/Effect/ALineEffect.h`（及其子类链）；
- `ScreenOverlayRegistry.cpp` → `Interactive/Screen/ScreenWidget.h`（屏幕控件体系）；
- `SceneView` 本身只需要引擎的 `AView` 接口（GetCamera/getInutState/GetSafeSize），这一项**已经可以切**。

已完整回退（git checkout 恢复编辑器文件、删除引擎副本），当前绿。

下一步顺序：`r

1. 把 effect 体系（ALineEffect 及其子类、Im3DEffect 基类）搬进 `Rendering/Overlay/`；
2. 把 screen 体系（ScreenWidget、ScreenOverlayRegistry）搬进引擎（它们只依赖 Im2D/ImGui）；
3. 把 `ImRenderer` 的 `SceneView*` 参数改成 `Editor::Panels::AView*`，然后整文件搬迁。

验证：obj 与基准 **0 差异**；3.moon 落在基准的两个稳定态之一。


### 8.3 ImRenderer 与 SceneView 解耦：FrameInfo（2026-11-18）

按设计把 ImRenderer 对视图的依赖收成一份数据：新增 `Rendering/Overlay/FrameInfo.h`，只有它需要的**事实**：

| 字段 | 来源（编辑器侧组装） |
|---|---|
| iewMatrix / projectionMatrix / projectionMode | camera->GetViewMatrix() 等 |
| cameraForward / cameraUp / cameraPosition | camera->GetTransform()（导航立方体的朝向） |
| width / height | p_view.GetSafeSize() |
| input（InputState*，引擎类型） | p_view.getInutState() |
| itToFocus(dir) 回调 | 编辑器绑到 SceneView::FitToFocus |

ImRenderer::newFrame(const FrameInfo&) 存一份副本，代码里 8 处 enderView->... 全部改为读它；取景请求走回调（引擎不认识 view）。组装点：`Moon/editor/View/sceneview/viewportoverlay.cpp`。

顺带删掉两个已无用的 include（`viewerwidget.h`、`SketcherObj.h`）。

验证：obj 与基准 **0 差异**；3.moon 落在基准的稳定态之间（2447 px 为已知悬停抖动）。

**剩余阻塞（搬迁 Im3DRenderer 的最后三处）**：

1. `EventWidget.h`（Qt-free，可直接搬）；
2. `Interactive/Screen/ScreenOverlayRegistry` → `ScreenWidget` → `RenderWindowInteractor` 链（interactor 头 Qt-free、实现有 132 处 Qt）；
3. `Settings/DebugSetting.h` 的 `createWidget(QWidget*)` 虚函数（调试面板工厂，需拆到编辑器侧）。


### 8.4 ImRenderer / Im3DRenderer 已整体搬进引擎（2026-11-18）

本轮把剩余依赖链一次性搬完，Im3DRenderer 现在住在 `MoonRender/src/Rendering/Overlay/Im3DRenderer.cpp`，**对 Qt 零引用**：

| 搬入引擎 | 内容 |
|---|---|
| `Rendering/Overlay/Interactive/*` | 交互控件体系 11 个头 + 9 个实现（EventObject/EventObserver/WidgetEventTranslator/AbstractWidget 等）；Qt 输入驱动 `RenderWindowInteractor.cpp` 留在编辑器 |
| `Rendering/Overlay/Screen/*` | 屏幕覆盖体系（ScreenWidget/ScreenPath/HitShape/ScreenOverlayRegistry） |
| `Rendering/Overlay/EventWidget.h`、`Im3DPalette.h` | 控件基类与调色板 |
| `Rendering/Overlay/Im3DRenderer.{h,cpp}` | 3D 叠加渲染器本体（2262 行） |

配套改动：

- `MoonRender/CMakeLists.txt` 链接 `spdlog`（引擎侧日志；`ScreenPath.cpp` 的 `CORE_WARN` 改用 spdlog）；
- `Im3DRenderer` 里 `Editor::Core::Context` → `::Core::Context`（引擎上下文）、`MOON::ViewCubeLabels` → 引擎命名空间；
- 删掉空实现 `registerDebugSettings()` 带来的 `DebugSetting.h` 依赖；
- 所有旧路径保留转发头，编辑器调用点未动。

验证：窗口尺寸在本会话变成了 1160x487（与旧基准 1254x480 不同），已重建同尺寸基准 `base-obj.png` / `base-1.png`。obj 与基准 **0 差异**；3.moon 2469 px 属已知两稳定态抖动；同二进制两次运行 obj **0 差异**。


### 8.5 渲染线程：挂起点定位到第 5 个批次内的跨上下文对象（2026-11-18）

用 stderr 临时追踪（`Core::Debug::ThreadTrace`，已撤除）拿到证据：

- 渲染线程：entry → driver made, building → built + active → 执行了 4 个批次，之后停在批次内部；
- 主线程：flush x6、wait idle x6，但只有 4 次 idle → 第 5、6 次等待未返回；
- 同一日志里的 GL 错误：`VAO names must be generated with glGenVertexArrays`、`cannot use the zero renderbuffer object`。

结论：挂起发生在渲染线程执行批次时的跨上下文对象误用上——VAO 属于容器对象，不随共享组共享，凡是仍在主线程（widget 上下文）创建的 VAO/FBO，渲染线程都拿不到；第 5 批次里第一次用到它们就卡住。这正好对应 §7.14 的剩余清单（HAL 的 VertexArray/Buffer 构造、FramePaster/IconCache 的 GL 对象、着色器编译）。

顺带落地一处真实修复：Driver::ExecutePendingCommands() 增加驱动线程判据（Filament 的 isThisThreadTheDriverThread）——在渲染线程内部调用时走内联执行，否则会发布批次再等自己。

验证（默认路径）：obj 与基准 0 差异；3.moon 2469 px（已知两稳定态抖动）。

下一步（按顺序）：

1. HAL VertexArray/Buffer 的构造改成命令（CreateVertexArray/CreateBuffer），加载期不再在主线程 glGen；
2. FramePaster/IconCache/MaterialThumbnailRenderer 里仍直接建 GL 对象的地方走命令；
3. 着色器编译 CreateProgram 命令。


### 8.6 两处 GUI 线程 GL 创建改为记录执行（2026-11-19）

按 §8.5 的结论，把仍在主线程（widget 上下文）创建 GL 对象的两处改成向驱动记录：

1. `Im2DRender::NewFrame` 的首次 `Init()`（program + VAO + 两个 VBO）：改为 `driver->Enqueue(...) + ExecutePendingCommands()`，对象因此创建在持有上下文的一侧；没有驱动（无头测试）时退回内联执行。
2. `ImGuiRenderer::initialize`（编辑器侧）里的 `m_glRenderer.Initialize()`：同样改为记录到模块驱动，因为线程模式下这里跑在 GUI 线程，而模块上下文不在这条线程上——之前正是它让 `glGenVertexArrays` 拿到 0。

验证：默认路径 obj 与基准 **0 差异**；线程模式仍然挂起，说明还有其它主线程 GL 创建点（`FramePaster`、`IconCache`、`MaterialPreviewWidget` 的贴图/帧缓冲，以及 `Material` 加载期的着色器编译）。

下一步：把这些点逐个改成记录，或把 `SetRenderThreadActive` 之后的主线程 GL 调用加断言，让违规点直接报出来而不是挂住。


### 8.7 命令级追踪：挂起落在第 4 条命令（present 路径）（2026-11-19）

在 `OpenGLDriver::Execute` 的循环里加了每条命令的 begin/end 追踪（已撤除），线程模式下的日志显示：

- 前 3 条命令正常结束；第 4 条命令 **begin 之后没有 end**；
- 该命令同时触发两个 GL 错误：`VAO names must be generated with glGenVertexArrays`、`cannot use the zero renderbuffer object`。

即：卡住的这一条既绑了一个无效 VAO，又绑了一个没有 renderbuffer 的 framebuffer——对应 `Present` 收尾（blit 到 present target / widget 侧 FBO）这条路径。结合 §8.5 的结论（VAO 不随共享组共享），说明 present 路径上仍有对象属于 widget 上下文。

下一步（针对这条命令）：

1. 确认 `PresentTo` 的源/目标都是模块自己的 FBO（`m_framebuffer` → `m_presentTarget`），widget 只负责把 `m_presentTarget` 的颜色贴图 blit 到自己的默认 FBO（`FramePaster`，GUI 线程、widget 上下文）；
2. 检查该命令用到的 VAO 是否由 `ImGuiGLRenderer/Im2DRender/Im3DGLRenderer` 在模块侧创建（§8.6 已改），以及 `FramePaster` 是否被误记进命令流。

验证（默认路径）：obj 与基准 **0 差异**；3.moon 与上一状态 **0 差异**。


### 8.8 命令带名字 + MOON_CMD_TRACE（2026-11-19）

为了能说清卡在哪条命令，给命令流加了名字（Filament 的命令流不带名字，这是调试补充）：

- `CommandBase` 增加 `const char* m_name` 与 `GetName()`（只存静态字符串指针，不拥有）；
- `CommandType<...>::Command` 的构造函数多一个 `const char* p_name`，由 `DriverAPI.inc` 的录制宏用 `#name` 传进来；`NoopCommand` 与 `CustomCommand` 分别是 Noop 与 Custom；
- `OpenGLDriver::Execute` 里当 `MOON_CMD_TRACE` 置位时逐条打印 `[cmd] <name>`。常驻诊断开关，默认关闭。

验证：默认路径 obj 与基准 **0 差异**；3.moon 2469 px（已知两稳定态抖动）。线程模式本轮没有再抓到 `[cmd]` 输出——应用在更早的位置停住（Home 之后只剩 `invalidate buffer` 刷屏），说明卡点已经前移到记录侧：还没进入命令执行就停了。

下一步：重点怀疑 `InitializeImGuiHost`——它在 `CreateRenderModule` 之前调用，此时模块还不存在，只能走内联分支，于是 GL 对象落在 widget 上下文里。线程模式下应改成模块建好之后再注册宿主。


### 8.9 线程模式：整帧作为作业交给渲染线程（2026-11-19）

找到一处结构性错误：`ViewportFrame::Render` 在线程模式下仍然在主线程执行 `renderJob`，而 `module->MakeCurrent()` 在线程模式下是**不绑定**的（Qt 上下文不能跨线程 current），于是那些直接 GL 调用（`DeferredDeleter::Flush`、`initGizmoGl`、`BeginFrame/EndFrame`）全部在没有上下文的情况下执行——正是 §8.5/§8.7 里看到的 VAO 与 renderbuffer 错误。

改法（Filament 的形状：帧就是一条命令，而不是窗口线程里的一串调用）：`RenderModuleHost::IsThreaded()` 暴露线程状态，`ViewportFrame::Render` 在线程模式下把整个 `renderJob` 用 `driver->Enqueue(renderJob)` 交给驱动线程执行并等待；帧内记录的其它命令因为在批次内（`m_executing`）会就地执行，不会重复入环。非线程路径完全不变。

验证：默认路径 obj 两次运行 **0 差异**（1314x504），画面正确（材质球 + 导航立方体 + 旋转手柄）。线程模式仍然挂起，说明还有别的前置阻塞（记录侧在进入命令执行之前就停住）。

**环境注意**：窗口尺寸在多次会话间变化过 1160x487 / 1254x480 / 1314x504，像素比对只在同尺寸下有效；仓库里保留了 `base-obj.png`（1160x487）、`base1254-obj.png`、`base1314-obj.png` 三套基准，按产生尺寸选用。


### 8.10 纠偏：撤回 lambda 搬迁、摘掉 ImGui（2026-11-19）

按纠正后的原则重做：

1. **撤回** `ViewportFrame::Render` 里把整帧用 `driver->Enqueue(renderJob)` 挪到渲染线程的做法（那是把主线程逻辑整体搬家，不是命令化）；恢复 `MakeCurrent/renderJob/DoneCurrent` 的原路径。
2. **摘掉 ImGui**：不再注册 `IImGuiHost`（`InitializeImGuiHost` 注释停用），`ImRenderer::newImgui/endImgui` 在无宿主时直接返回，依赖它的调试视图（gbuffer 预览、帧统计、拾取调试图）用 `ImRenderer::HasImGuiHost()` 守卫跳过。验证：去掉 ImGui 前后 obj **0 差异**（1314x504）。

线程模式现状：日志到 `[Viewer] drawing a frame` 后停住，仍无任何 `[cmd]` 输出，并伴随 `cannot use the zero renderbuffer object`。即卡点在**帧内的直接 GL**（命令流之前）：

| 位置 | 现在 | 应该 |
|---|---|---|
| `DeferredDeleter::Flush` | 主线程直接执行上传/删除回调 | 上传与删除各自成为命令（`UploadTexture` 已有，删除要 `DestroyTexture`） |
| `initGizmoGl` | 主线程建材质/几何 | `CreateVertexArray` / `CreateBuffer` / `CreateProgram`（§8.11 的清单） |
| `Im2DRender::NewFrame` | 用 `Enqueue(lambda)` 记录 Init | 改成 `CreateProgram`+`CreateVertexArray` 命令 |
| `FramePaster` | 主线程（widget 上下文）blit | 保持，它属于窗口侧 |

命令的同步/异步语义（Filament 的 `CommandStream`）：同步方法立即在调用线程执行（只在上下文就在本线程时用），异步方法分配环形缓冲空间、把 `OpenGLDriver::xxxR` 的静态入口与参数写进去。主线程只做后者；`Enqueue(lambda)` 现在起视为**禁用**（除过渡期的两处已在 §8.6 记录的以外不再新增）。


### 8.11 下一步的正确形状：HAL 的 VAO/Buffer 也走句柄加命令（设计）

帧内里程碑追踪（`MOON_FRAME_TRACE`，常驻、默认关闭）在线程模式下给出确切位置：

`frame: enter` → `initGizmoGl` → `overlay->BeginFrame` → `FlushDocumentRead` → `view->Update` → `view->Render` **停住**，且没有任何 `[cmd]`。

卡在 `AView::Render` 内部；那里面主线程仍直接做 GL 的只剩 `PresentTo`（绑 FBO + 一次全屏 present），而它画 quad 用的 VAO 是 HAL 在主线程构造的：`GLVertexArray::TVertexArray` 直接 `glCreateVertexArrays`、`GLBuffer::TBuffer` 直接 `glCreateBuffers`。线程模式下这些名字拿不到（无上下文），于是绑定时报 `VAO names must be generated` 与 `zero renderbuffer`。

按 Filament 的做法，不再把调用包 lambda 搬到渲染线程，而是把这两种对象也变成句柄加命令（与贴图同构）：

| 层 | 现在 | 目标 |
|---|---|---|
| HAL 构造 | 直接 glCreate*，拿 GL 名字 | 只分配句柄（`ResourceHandleAllocator`），对象名由渲染侧填 |
| 命令 | 无 | `CreateVertexArray(handleId)`、`CreateBuffer(handleId, type)`、`DestroyBuffer(handleId)` |
| 注册表 | 无 | `VertexArrayRegistry` / `BufferRegistry`（拥有 HAL 对象、句柄到对象），形状与 `TextureRegistry` 一致 |
| 同步语义 | 无 | 录制侧只记录；需要立刻用的场景（加载期取 `GetID`）用同步方法 `Flush() + WaitForIdle()` |

顺序：先 Buffer（`Allocate/Upload` 一并命令化），再 VertexArray（`SetLayout` 里的 `glVertexAttribPointer` 必须一起进命令），再 Framebuffer/Renderbuffer（`SetupFramebuffer` / `Resize`），最后 ShaderProgram/ShaderStage（编译链接）。每完成一类，`MOON_RENDER_THREAD=1` 应能往前推进一段帧内里程碑。

验证（本轮）：默认路径 obj 与 `base1314-obj.png` **0 差异**；ImGui 摘除前后同样 0 差异。


### 8.12 缓冲区的句柄与命令落地（2026-11-19）

按 §8.11 的顺序，先落地缓冲区这一层（与贴图同构，不动调用方）：

| 新增 | 内容 |
|---|---|
| `Rendering/Backend/BufferRegistry.{h,cpp}` | 句柄到 `HAL::Buffer` 的表：CreateHandle/Create/Find/Release/SlotCount，实现照 `TextureRegistry`；销毁经 `DeferredDeleter` 排到持有上下文的一侧 |
| `Rendering/Backend/BufferRequests.h` | `CreateBufferRequest{handleId, type}`、`AllocateBufferRequest{handleId, size, usage}` |
| `DriverAPI.inc` 新增两条 | `CreateBuffer`、`AllocateBuffer`（录制端 Enqueue*、执行端 OpenGLDriver::*R） |

执行端：CreateBufferR 在上下文侧建对象并登记；AllocateBufferR 找到对象后调用 Allocate(size, usage)。录制端仍由 X 宏生成，主线程只写命令、不进 GL。

验证：默认路径 obj 与同尺寸基准 `base1254-obj.png` **0 差异**（本构建窗口尺寸回到 1254x480）。

下一步（同一顺序）：把 `GLBuffer::TBuffer` 与 `GLVertexArray::TVertexArray` 的构造改成先分配句柄、由命令建对象，并让 `Mesh` / `Model` 这类持有者改持句柄；随后是 `SetLayout`（glVertexAttribPointer 必须一起进命令）以及 Framebuffer/Renderbuffer。


### 8.13 缓冲区与顶点数组的创建改为命令（2026-11-19）

按 §8.12 的计划，把 HAL 的两类容器对象的**创建与销毁**搬到了执行侧：

| 件 | 做法 |
|---|---|
| `BufferRegistry` | 改为**名字表**（句柄到 GL 名字，不再持有 HAL 对象，避免二次所有权）；`Create/Find/Release`，`Release` 经 `DeferredDeleter` 排到上下文侧删除 |
| `VertexArrayRegistry` | 同上，新增 |
| 命令 | `CreateBuffer{handleId,type}`、`CreateVertexArray{handleId}`（录制端 `Enqueue*`、执行端 `OpenGLDriver::*R`） |
| `GLBuffer::TBuffer` / `~TBuffer` | 构造：分配句柄 → 记录 `CreateBuffer` → **同步 drain**（`ExecutePendingCommands`）→ 从注册表取名字；析构：只调 `Release` |
| `GLVertexArray::TVertexArray` / `~TVertexArray` | 同上（`CreateVertexArray`） |
| `Rendering/HAL/Common/HALDriverAccess.{h,cpp}` | HAL 侧取当前驱动（无模块时返回空，退回内联创建） |

这就是命令流的**同步语义**（Filament 的立即执行那一半）：录制侧写命令，随即 drain，拿到对象后继续；线程模式下 drain 变成 flush + wait，创建工作因此发生在持有上下文的一侧。异步语义留给每帧的绘制命令。

验证：obj 与 3.moon 均与同尺寸基准 `base1254-*.png` **0 差异**。

剩余同类工作：SetLayout（`glVertexAttribPointer` 也要进命令）、Framebuffer/Renderbuffer 的创建与 `Resize`、ShaderProgram/ShaderStage 的编译链接。


### 8.14 SetLayout 命令化失败并回退（2026-11-19）

尝试把 `GLVertexArray::SetLayout` 也改成命令（`VertexArrayLayoutRequest` 携带 `stride`、`offset` 与最多 16 个属性，执行端统一 `glBindVertexArray + glBindBuffer + glVertexAttribPointer`）。结果：**首帧崩溃**（0xC0000005，栈上返回地址为野值）。

已做的两步排查：

1. 属性类型改为录制期就用 `EnumToValue<GLenum>` 映射；
2. 确认请求按值进命令（宏的 `Arguments` 是值元组）。

两步之后仍崩，因此回退 `SetLayout` 到原来的直接 GL 实现；缓冲区与顶点数组的创建/销毁命令化（§8.13）保留。回退后 obj 与同尺寸基准 `base1314-obj.png` 0 差异，绿。

下次再动这块先做小实验：只把 `glVertexAttribPointer` 换成命令、把 `Bind/Unbind` 留在调用侧，帧稳定后再整体迁移。崩溃点更可能是执行端把 VAO 与缓冲重新绑成 0 干扰了既有绑定，而不是命令机制本身。


### 8.15 SetLayout 小实验也崩：只保留创建/销毁的命令化（2026-11-19）

按 §8.14 的小实验路线试了两档：

1. 执行端只做 `glEnableVertexAttribArray + glVertexAttribPointer`，绑定留在 `SetLayout` 里 —— 仍崩（首帧 0xC0000005）；
2. 完全回退 `SetLayout` 到直接 GL —— 绿（obj 0 差异）。

结论：崩溃与执行端重新绑定 VAO/缓冲无关，而与把属性描述搬到命令并同步 drain 有关；未定位清楚前不再动它。处置：

- 删除未使用的 `SetVertexArrayLayout` 命令、执行端与 `VertexArrayLayoutRequest.h`（不留半成品）；
- `GLBuffer` 与 `GLVertexArray` 的构造加分支：没有渲染线程时内联创建（与历史行为完全一致），有渲染线程时才记录 `CreateBuffer` 与 `CreateVertexArray` 并同步 drain；析构只走 `Release`（排到上下文侧删除）。

验证：obj 与 `base1314-obj.png` 0 差异；3.moon 的 2412 px 差异经同构建两次运行确认为已知的两稳定态抖动。

下一步建议：先做线程模式下只画背景的最小闭环（跳过场景与叠加绘制），确认 `WaitAndExecute` 与 present 这条链能跑通，再逐块接回场景；不要在完整帧上继续试命令化——完整帧里任何一个直接 GL 都会掩盖问题。


### 8.16 最小闭环：线程骨架跑通（2026-11-19）

新增 `MOON_RENDER_MINIMAL`（配合 `MOON_RENDER_THREAD`）：帧在 `SyncViewSize()` 之后直接返回，GUI 线程每帧既不记录也不画，只留下引擎自己的线程在跑。

结果：应用正常起来、不崩、不挂，日志到 `[GL] the render module now has its own context` 为止，进程空闲（CPU 0）。说明线程这一层是通的：上下文在渲染线程创建、模块在它上面构建、循环在等命令、退出路径可用。尚未被验证的是帧内容——它现在被整体跳过了。

下一段按从空到满推进，每步只加一层，崩了就是刚加的那层：

1. 只跑 CPU 侧（`FlushDocumentRead` 与 `view->Update`），不做任何 GL；
2. 加 `BindFramebuffer + Clear`（清 present target），仍不画场景；
3. 加场景录制块（`AView::Render` 里那段 `Enqueue`）；
4. 加 present（`PresentTo`，现在是直接 GL，需要变成命令）；
5. 最后加叠加层。

验证（默认路径）：obj 与 `base1314-obj.png` 0 差异。


### 8.17 Framebuffer/Renderbuffer 也走句柄+命令；线程模式推进到场景 pass（2026-11-19）

新增 `FramebufferRegistry` 与 `RenderbufferRegistry`（名字表，与 Buffer/VertexArray 同构）、命令 `CreateFramebuffer` / `CreateRenderbuffer`，并把 `GLFramebuffer::TFramebuffer` / `GLRenderbuffer::TRenderbuffer` 的构造与析构改成同一模式：无渲染线程时内联创建（与历史一致），有渲染线程时记录命令并同步 drain；析构只调 `Release`。

效果（`MOON_RENDER_THREAD=1 MOON_RENDER_MINIMAL=1`）：

- 之前的 `cannot use the zero renderbuffer object` **消失**（说明 FBO/RBO 现在是有效对象）；
- 帧内里程碑推进到：enter → BeginFrame → DrawFrame……然后**挂住**（无 GL 错误）。

即：线程骨架、资源创建（纹理/缓冲/VAO/FBO/RBO）、命令环与等待都通了，卡点已经进到**场景 pass 树内部**。

验证（默认路径）：obj 与 `base1314-obj.png` 0 差异。

下一步：在 `CompositeRenderer::DrawFrame` 的 pass 循环里按同法加 `MOON_FRAME_TRACE` 里程碑，定位是哪一个 pass 挂住；常见来源是同步 fence（`glClientWaitSync`）、异步读回或某个 pass 在等一个只有主线程才会给的状态。


### 8.18 关键发现：模块上下文在渲染线程上名义 current、实则无效（2026-11-19）

这一轮把线程模式一路推进，并逐个修掉了真实缺陷：

1. Framebuffer/Renderbuffer 命令化（`FramebufferRegistry` / `RenderbufferRegistry` + `Create*` 命令）——`zero renderbuffer` 错误消失；
2. `Driver::m_renderThreadId` 从未被赋值（早前编辑静默失败）：批次内的 `ExecutePendingCommands()` 误判为记录侧，调用 `WaitForIdle()` 等自己，造成自死锁；修为在 `SetRenderThreadActive(true)` 里记录当前线程 id；
3. `Flush()` 与 `ExecuteAll()` 增加批次内直接返回的守卫（Filament 语义：驱动线程上 flush 无效）；
4. `QOffscreenSurface` 改为在 GUI 线程预备（Qt 规定），渲染线程只建上下文并绑定；
5. 帧内里程碑追踪（`MOON_FRAME_TRACE`）覆盖 `AView::Render`、pass、BeginFrame 三层。

最后的结论性证据：在 `Loop()` 里 MakeCurrent 之后直接查询 GL 状态，得到 `vendor=(null) viewport=0,0 0x0`。即 `QOpenGLContext::makeCurrent()` 返回成功，但上下文在该线程上并未真正可用（Qt 5.15 对非 GUI 线程配合 `QOffscreenSurface` 的限制）。这就是之前所有卡住的真正原因：第一条 GL 调用即阻塞在驱动里。

这是一个方案性决策点，有两条路：

| 方案 | 做法 | 代价 |
|---|---|---|
| A | 上下文与 surface 在 GUI 线程创建，渲染线程用裸 `wglMakeCurrent`（取 Qt 的 HGLRC）绑到该线程，绕过 Qt 的线程检查 | 小；须保证 Qt 侧不再碰该上下文 |
| B（推荐） | 引擎的 `IPlatform` 由编辑器用 WGL + 隐藏窗口自己实现（不用 QOpenGLContext），与 Qt 上下文通过 `wglShareLists` 共享 | 中等；换来引擎彻底不依赖 Qt，正是 Filament 的形状 |

验证（默认路径）：obj 与 `base1314-obj.png` 0 差异。


### 8.19 突破：帧在渲染线程上跑起来了（2026-11-19）

**1. 真正的死锁元凶找到了。** `Driver::ExecutePendingCommands()` 一直是旧版本：只有 `if (m_renderThreadActive) { Flush(); WaitForIdle(); }`，既没有驱动线程判据、也没有 `m_executing` 判断。于是批次内（渲染线程正在执行）任何一次 drain（`Clear`、`SetViewport`、资源创建）都会 `WaitForIdle()` 等自己。改为：批次内直接返回；渲染线程自己同理；只有记录侧才 flush+wait。

**2. §8.18 的结论是错的（测量位置放错了）。** sanity check 被插到了 `MakeCurrent()` 之前，所以看到 `vendor=(null)`。移到之后：current=1 vendor=NVIDIA Corporation viewport=0,0 1424x712 —— 模块上下文在渲染线程上完全可用，Qt 这条路没有问题（与 Qt-Filament 的 `PlatformGlfwGL` 一致：context 与 offscreen surface 都在驱动线程创建、同一份 fmt、samples=0）。

**3. 线程模式下的帧现在真的跑在渲染线程上**：base clear done → matrices done → BeginFrame: features → DrawFrame → SkyboxRenderPass → draw。

**4. 关机崩溃修掉**：延迟删除队列原来是命名空间静态，HAL 对象在静态析构期 push 会用到已销毁的锁与表。改成函数内 `new State()`（永不销毁），退出时泄漏这点内存是廉价的一边。

**5. 当前线程模式的失败点**：天空盒 pass 绘制后崩溃，GL 报 `GL_INVALID_FRAMEBUFFER_OPERATION`（FBO 不完整）与大量 `GL_INVALID_VALUE: Invalid offset and/or size`。即资源的 Allocate/Upload 仍是调用线程直接 GL：GUI 线程上调它们时没有上下文，尺寸与数据都没真正上传，渲染线程用到时自然无效。

下一步（仍是同一模式，无需新决策）：把 `Allocate` / `Upload` / `Bind` 等数据操作命令化，与创建共用那个分支——无渲染线程则内联，有则记录并同步 drain；然后重跑 `MOON_RENDER_MINIMAL=1` 看 pass 是否走完。

验证（默认路径）：obj 与 `base1314-obj.png` 0 差异。


### 8.20 按 Filament 的分工：录制逻辑搬进 CommandStream（2026-11-19）

之前的错误形状：录制（分配 + 构造命令 + 传函数指针）写在 `Context::Driver` 的 `Enqueue*` 桩里，而 `OpenGLDriver` 也 include 了 `DriverAPI.inc` 去声明执行端——两边都知道命令这个概念。

现在按 Filament 的形状拆开：

| 角色 | 职责 |
|---|---|
| `Backend::CommandStream` | 唯一的录制入口。由同一个 `DriverAPI.inc` 生成一组真实方法（DrawMaterial、Viewport 等）：分配环形缓冲空间、在地址上构造命令、把 `&OpenGLDriver::<name>R` 与参数写进去；栈满时通过 `SetOverflowHandler` 处理（单线程执行、线程模式发布）。stream 本身不认识 driver |
| `Context::Driver` | 对外 API 的薄委托：批次内（`m_executing`）就地调执行端，否则 `m_commandStream-><name>(...)`；不再碰环形缓冲与命令构造 |
| `OpenGLDriver` | 只留执行端方法（`*R`），命令最终落到这里 |

验证：默认路径 obj 与 `base1314-obj.png` 0 差异；线程模式行为与重构前一致（帧进入 DrawFrame 并跑到 SkyboxRenderPass）。

还剩两步才算完全对齐：

1. 把 `DriverAPI.inc` 从 `OpenGLDriver` 里去掉：执行端方法改成手写声明、去掉 `R` 后缀，让它完全不 include 命令相关的头；
2. 拆掉三处 `Enqueue(lambda)` 录制块（AView::Render、Im2DRender、ImGuiRenderer）：块体改为在录制侧写命令，此后 `m_executing` 的内联分支可退化成断言。


### 8.21 OpenGLDriver 不再认识命令（2026-11-19）

第二步落地：`DriverAPI.inc` 从 `OpenGLDriver` 里彻底移除。

- 执行端方法改成手写声明（由 `DriverAPI.inc` 的条目一次性生成，之后就是普通声明），并去掉 `R` 后缀：`Viewport`、`DrawMaterial`、`CreateTexture` 与它执行的动作同名；
- `OpenGLDriver` 不再 include 命令与命令流相关头（只保留签名里用到的请求结构体）；
- 录制入口只剩 `CommandStream` 一处（`CommandStream::DrawMaterial(...)` 里构造 `Command<&OpenGLDriver::DrawMaterial>`）；
- `Context::Driver` 的 `Enqueue*` 只有两种走向：批次内 `m_openGLDriver->name(...)`（内联），否则 `m_commandStream->name(...)`（入环）。

验证：默认路径 obj 与 `base1314-obj.png` 0 差异。

至此职责与 Filament 一致：命令只存在于 CommandStream 侧，OpenGLDriver 只是方法。剩下最后一件事：拆掉三处 `Enqueue(lambda)` 录制块（AView::Render、Im2DRender、ImGuiRenderer），之后 `m_executing` 的内联分支可以删掉或退化成断言，命令里再录制命令、以及主线程看到执行中标志这两个问题会同时消失。


### 8.22 AView 的录制块拆掉：帧变成纯录制（2026-11-19）

新增两条命令（`FramebufferRequests.h`）以覆盖原来块内的 GL：

- `ResizeFramebuffer(target, w, h)`：尺寸变化时重建附件（纹理/渲染缓冲）；
- `CopyFramebufferColor(source, si, target, ti)`：MSAA resolve 与帧交接用的 blit。

`AView::Render` 里原来的 `Enqueue(lambda)`（正常路径与 minimal 路径各一处）全部删除，改成录制调用：

  ResizeFramebuffer x3 → BeginFrame → DrawFrame → EndFrame → CopyFramebufferColor → PresentTo → ExecutePendingCommands

效果：帧内不再有代码在命令执行时运行，命令也就无法再录制命令；场景路径上不再用到 `m_executing` 的内联分支。

验证：

- 默认路径 obj 与 `base1314-obj.png` 0 差异；
- 线程模式（`MOON_RENDER_MINIMAL=1`）帧推进到 SkyboxRenderPass、Opaques、PointDraw、Grid，随后在 Grid 处退出：这几个 pass 的 Draw() 里仍有直接 GL（GridRenderPass 3 处），线程模式下它们跑在记录侧、没有上下文。

下一步：把这些 pass 的直接 GL 也变成命令，然后是 `Im2DRender`（2 处）与 `ImGuiRenderer`（2 处）的录制块（后者已运行时禁用，可最后处理）。


### 8.23 pass 里的 framebuffer 绑定改为命令（2026-11-19）

新增 `ABaseRenderer::GetDriver()/BindFramebuffer()/BindDefaultFramebuffer()`，并把三个 pass 里对 HAL 的直接绑定换成驱动调用（绑定本身是命令，pass 跑在录制侧必须走它）：

| 文件 | 处数 |
|---|---|
| GridRenderPass.cpp | 5（镜像 FBO、阴影 FBO、MSAA FBO） |
| PickingRenderPass.cpp | 3 |

验证：

- 默认路径 obj 与 `base1254-obj.png` **0 差异**（本次窗口 1254x480）；
- 线程模式帧推进到：SkyboxRenderPass → Opaques → PointDraw → Grid → SectionContour → Transparents → **HZB**，在 HZB 处退出。即线程模式下已经连续跑过 7 个 pass，剩下的失败点是 HZB pass（以及之后的叠加层）。

下一步：HZB / 剩余 pass 的直接 GL，然后 `Im2DRender` 的两处录制块。


### 8.24 HZB 暂缓 + 线程模式推进到叠加层（2026-11-19）

试着把 `HzbBuildPass` 末尾的 `msaaBuffer.Bind()` 也换成命令，默认路径崩溃（0xC0000005）：这个 pass 前后有直接 GL（深度 blit、fence 与 PBO 读回），Bind 的时序对它不是无关紧要的。已回退该处，并在线程模式下禁用 HZB（IsRenderThreadActive 时把名为 HZB 的 pass SetEnabled(false)），留给读回命令化那一轮。

结果（线程模式 + MOON_RENDER_MINIMAL=1）：帧走完 Post-Process、Path Tracing、Lines，最后停在 ImRenderer（叠加层）pass——它里面是 Im3DType/Im3DGLRenderer 的直接 GL（VAO/VBO 生成、上传、绘制）。

验证：默认路径 obj 与 `base1254-obj.png` 0 差异。

下一步：把叠加层的直接 GL 命令化（它现在是引擎侧代码，改动面较小），之后是 `Im2DRender` 的两处录制块。


### 8.25 线程模式先摘掉叠加层（2026-11-19）

叠加层（Im3D 的 VAO/VBO 上传、Im2D 的绘制列表）是最后一层还在自己发 GL 的东西，线程模式下它跑在记录侧、没有上下文。因此在线程模式里先关掉：

- `ViewportFrame::Render` 增加 `overlayOff = minimalFrame || module->IsThreaded()`，用它挡住 `DeferredDeleter::Flush`、`initGizmoGl`、`overlay->BeginFrame/EndFrame`；
- 线程模式下把名为 `ImRenderer` 的 pass 也 `SetEnabled(false)`。

验证：默认路径 obj 与 `base1254-obj.png` **0 差异**（这条守卫只在线程模式下生效）。

线程模式仍然在首帧退出，日志里只剩 `GL_INVALID_FRAMEBUFFER_OPERATION` 与 `Invalid offset and/or size` 两类报错；说明卡点不在叠加层，而是某个 FBO 在创建/尺寸上还没跟上（`ResizeFramebuffer` 之后附件尺寸与 pass 期望不一致，或某处仍在记录侧直接建 FBO）。

下一步：在线程模式下逐个 pass 二分（`MOON_PASS_ONLY` 之类的开关只启用单个 pass），把 FBO 不完整的来源锁定；这比继续猜要快。


### 8.26 二分工具 MOON_PASS_ONLY 与结论：问题不在某个 pass（2026-11-19）

新增 `MOON_PASS_ONLY=<name>`（`CompositeRenderer::DrawFrame` 过滤，常驻、默认关闭），用来在线程模式下只跑一个 pass。

用它只跑 `SkyboxRenderPass` 时，线程模式**照样**刷 `GL_INVALID_FRAMEBUFFER_OPERATION` 并退出。所以问题不在某个具体 pass，而在**视口的渲染目标本身**：`m_framebuffer` / `m_msaaframebuffer` / `m_presentTarget` 是 `AView` 构造时（GUI 线程）通过 `FramebufferUtil::SetupFramebuffer` 建的——那里的**纹理创建与附件绑定**是直接 GL，线程模式下没有上下文，于是 FBO 名字有了、附件却是无效的，任何 FBO 操作都报 incomplete。

验证：默认路径 obj 与 `base1254-obj.png` 0 差异（`MOON_PASS_ONLY` 默认关闭）。

结论（下一步的设计点）：`SetupFramebuffer` 必须变成可录制的东西——要么整段做成一条 `SetupFramebuffer(descriptor)` 命令，要么拆成 `CreateTexture` + `AttachTexture`（`glNamedFramebufferTexture`）+ `CreateRenderbuffer` 三条。视口的构造也要相应地推迟到第一次帧（那时上下文在渲染线程上）。


### 8.27 SetupFramebuffer 变成命令（照 Filament 的 S/R 分工，2026-11-19）

读 Filament：`createRenderTargetS()` 只 `initHandle<GLRenderTarget>()`（分配句柄，无 GL）；`createRenderTargetR(handle, targets, w, h, samples, layerCount, MRT color, depth, stencil)` 才 `glGenFramebuffers`、按**纹理句柄** `glNamedFramebufferTexture` 挂附件、再 `glDrawBuffers`。

据此新增 `SetupFramebufferRequest`（target/width/height/useDepth/useStencil/useMipMaps/useMultiSample，或 TextureDesc 形式），执行端 `OpenGLDriver::SetupFramebuffer` 调 `FramebufferUtil::SetupFramebuffer` 做真正的建纹理/渲染缓冲/挂附件；录制端 `Driver::SetupFramebuffer(...)` 只发命令。`AView` 构造里三处直接调用改成命令，并在构造末尾 `ExecutePendingCommands()`（线程模式下 = flush + 等渲染线程），保证构造结束时附件已经存在。

结果（线程模式）：`GL_INVALID_FRAMEBUFFER_OPERATION` 与 `GL_INVALID_VALUE` **全部消失**（0 次），帧推进到 `SkyboxRenderPass::Draw` 内部；那里还有记录侧的直接 GL：`output.value().Bind()`（已改为命令）与 `material.SetProperty(...)`（uniform 上传，仍是记录侧 GL，是下一个失败点）。

验证：默认路径 obj 与 `base1254-obj.png` 0 差异。

下一步：把材质属性/uniform 上传并入 `DrawMaterial` 命令（Filament 的做法是 descriptor set 随命令走），Skybox 的 `SetProperty` 与各 pass 的同类调用就都能在记录侧安全执行。


### 8.28 描述符集改命令 + 一次回退（2026-11-19 晚）

按纠正后的模型落地：属性不是命令，描述符集的提交与绑定才是。

- 新增 `DescriptorRequests.h`：`UpdateBufferRequest`（Filament 的 updateBufferObject）、`BindDescriptorRequest`（bindDescriptorSet）；
- 两条命令 + 执行端：UpdateBuffer 走 `glNamedBufferSubData`，BindDescriptor 走 `glBindBufferRange` 与 `glBindTextureUnit`；
- `Driver::UpdateBufferRange` 把字节拷进命令环（`CommandStream::Allocate`），调用方可以传栈上数据；
- 新增 `AllocateBuffer` 命令（执行端 `glNamedBufferData`，目前未被调用）。

回退：把 `GLBuffer::Upload` 接到 `UpdateBufferRange` 后，默认路径的抓帧崩溃（`QOpenGLWidget::grabFramebuffer` 触发），停用该分支后立即恢复（0 差异）。怀疑点写在代码注释里：CommandStream::Allocate 先写字节、后写命令，若两者之间发生一次 `Flush`，命令就会指向生产者已经在复用的内存。下一步应让命令与负载成为不可分割的一批（Filament 的 `allocatePod` 在同一 buffer 内连续分配，Flush 只发生在批次边界）。

线程模式进展：computeSkyTexture 在线程下跳过（它内部是自己的一组渲染），帧推进到 SkyboxRenderPass 的 SetCamera 之后；而 EngineBufferRenderFeature::SetCamera 正是走 GLBuffer::Upload，即被回退的那条路径。

验证：默认路径 obj 与 `base-obj.png`（1160x487）0 差异，进程正常退出。


### 8.29 去掉 EnsureRoom，flush 时机对齐 Filament；并修掉一个真 bug（2026-11-19 夜）

Filament 里没有腾空间这一层：`CircularBuffer::allocate` 只是指针推进并在超限时断言；生产者唯一的背压是 `CommandBufferQueue::flush()`，发布一批后若剩余空间小于一帧所需就 wait，等消费侧 releaseBuffer。因此删除 `CommandStream::EnsureRoom` 与 `SetOverflowHandler`，录制路径不再可能插入一次执行。

上传负载的归属也改了：从环内拷贝改成请求自持（Filament 的 BufferDescriptor）。`UpdateBufferRequest` 现在持有 `shared_ptr<const std::vector<uint8_t>>`，因为一次上传可能是几 MB，而环只按一批命令的大小配置。

真 bug：抓帧崩溃与上传命令无关。`ViewportFrame::Render` 结束时无条件 `module->DoneCurrent()`，把调用方原本的上下文也解绑了；`QOpenGLWidget::grabFramebuffer()` 恰恰是在 paintGL 之后立刻读自己的 FBO，于是在没有上下文的情况下读，直接崩。改为：进入前记下 `QOpenGLContext::currentContext()`，结束时把调用方的上下文重新 current（没有则 DoneCurrent）。

验证：

- 默认路径（上传已走命令）obj 与 `base-obj.png` 0 差异，进程正常退出，抓帧正常；
- 线程模式：帧到 SkyboxRenderPass 的 SetCamera 之后仍退出；日志里的 `GL_INVALID_VALUE: Invalid offset and/or size` 全部来自模块初始化阶段，帧内 GL 错误为 0。

下一步：线程模式 SetCamera 之后那一步的对象有效性，以及初始化阶段那批 Invalid offset 的来源。


### 8.30 缓冲区分配/上传也走命令；线程模式剩下一个执行侧崩溃（2026-11-19 深夜）

本轮完成的命令化（都在 `IsRenderThreadActive()` 下启用，默认路径保持逐字节不变）：

- `GLBuffer::Allocate` -> `AllocateBuffer` 命令（执行端 `glNamedBufferData`）；
- `GLBuffer::Upload` -> `UpdateBufferRange`（请求自持字节，对应 Filament 的 BufferDescriptor）；
- `Descriptor::Upload` -> 同上。

线程模式的现状：帧走到 `SkyboxRenderPass` 的 `SetCamera`，四条 engine-UBO 上传**都已由渲染线程执行**（`[cmdx] UpdateBuffer id=7 off=0/64/128/140`），随后进程退出且**不是**记录侧的 `RemoveFeature` 之前。因此失败点在执行侧：命令执行完 `~Command()` 销毁它持有的 `shared_ptr` 负载时，怀疑同一批被**执行了两次**（`Flush`/`ReleaseBuffers` 在双线程下的记账），于是 shared_ptr 被析构两次。

下一步（诊断已缩小）：打印批次号与 `tail/head` 指针，确认是否有批次重复执行；再检查 `CommandBufferQueue::Flush` 与 `WaitForIdle` 的互斥是否覆盖了 `m_commandBuffersToExecute` 的整段交换。

验证：默认路径 obj 与 `base-obj.png` **0 差异**（每次改动后都复核）。


### 8.31 崩溃点收敛到 glDrawElements（2026-11-19 深夜）

在 `OpenGLDriver::DrawMesh` 打点后，第一帧那次绘制是 `vtx=4 idx=6 vao=1 inst=1 prim=4`，且 `glGetString(GL_VERSION) != null`（上下文 current）。同样的参数在默认路径下能正常绘制，所以问题不在请求内容，而在渲染线程上那次 GL 调用本身。

本轮排除/修复：Mesh::GetIndexCount 越界（已加边界检查）；gladLoadGL 只加载一次（表是进程级的，第二次从另一上下文加载会用那边解析不到的结果覆盖）；线程模式下记录侧不再对模块上下文 MakeCurrent（一个上下文同一时刻只能在一个线程上 current）。三条都保留。

剩下的差异只在渲染线程的上下文与可绘制面。下一步：在该线程上做最小自检（glClear + 一个三角形 + glFinish），若同样崩，就把 QtGLContextHost 的 offscreen surface 换成与 Qt-Filament 完全一致的创建顺序，并核对 QOffscreenSurface 的线程亲和性。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.32 makeCurrent 按 Filament 的链路接上（2026-11-19 深夜）

Filament 的链路（已核对源码）：`FRenderer::beginFrame`（Renderer.cpp:361）→ `swapChain->makeCurrent(driver)` → `driver.makeCurrent(schDraw, schRead)`（DriverAPI.inc:486，一条驱动 API）→ `OpenGLDriver::makeCurrent`（OpenGLDriver.cpp:2697，内部 `mPlatform.makeCurrent(...)` 并解绑所有绑定）。

对应实现：`Driver::MakeCurrent()` 记录一条 MakeCurrent 命令 → 执行端 `OpenGLDriver::MakeCurrent()` 调 `m_contextHost->MakeCurrent()`；`AView::Render` 把它作为帧的第一条命令发出（对应 FRenderer::beginFrame）。无参命令的录制方法手写（我们的宏不支持空参，展开会留下逗号）。之前自己发明的每批执行前 makeCurrent 已删除。

仍崩在同一处：帧内第一次 `glDrawElements`（mesh vtx=4 idx=6 vao=1 prim=4）；崩溃前 MakeCurrent 命令已在渲染线程执行且未报失败，崩溃点 `glGetString` 非空。同样参数在默认路径下正常。

继续需要驱动内部状态：崩溃时该线程的 `wglGetCurrentContext()` 与 `wglGetCurrentDC()` 是否等于 QtGLContextHost 的 context/DC，以及 `QOpenGLContext::isValid()`。


### 8.33 对齐 Qt-Filament 的 PlatformGlfwGL（2026-11-19 夜）

读了 `Qt-Filament/test/PlatformGlfwGL.cpp`，把它的两条关键约定搬过来：

1. makeCurrent 的写法：先判断 `QOpenGLContext::currentContext() != mDriverContext` 再 make current；
2. 上下文只归驱动线程：该平台的 makeCurrent 只在驱动线程被调用，GUI 线程只用自己的 widget 上下文去 blit 环形纹理，从不碰驱动上下文。

对应实现：`QtGLContextHost::MakeCurrent()` 在模块有渲染线程且当前线程不是驱动线程时直接返回 false；`Driver::GetRenderThreadId()` 提供判据；已经 current 时直接返回 true。

仍在帧内第一次 `glDrawElements` 崩（vtx=4 idx=6 vao=1 prim=4，上下文 current）。已排除：命令参数、VAO 名、primitive、Mesh::GetIndexCount 越界、glad 重复加载、上下文被其他线程抢占、缺少 makeCurrent 命令。

下一步：按 PlatformGlfwGL 完全复刻，用驱动线程自己的 `QOpenGLFunctions_4_5_Core` 入口画一个最小三角形（不经 glad）；若成功，说明问题在 glad 入口表与该上下文的搭配，而不在上下文本身。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.34 线程模式的真凶：顶点布局从未被应用（2026-11-19 夜，突破）

`GLVertexArray::SetLayout` 一直是记录侧直接 GL（`glBindVertexArray`、`glVertexAttribPointer`、`glEnableVertexAttribArray`）。线程模式下没有上下文，这些调用什么也没做——VAO 于是**没有任何属性、也没有索引缓冲**，第一次 `glDrawElements` 在 NVIDIA 驱动里就崩（参数本身完全正常：vtx=4 idx=6 vao=1 prim=4）。

改为命令（Filament 的形状：布局描述给驱动、由执行端应用）：`VertexArrayRequests.h` 的 `SetVertexArrayLayoutRequest`（vao id、vertex buffer id、index buffer id + 属性数组，属性数组堆上自持）→ 命令 `SetVertexArrayLayout` → 执行端 `glBindVertexArray / glBindBuffer / glEnableVertexAttribArray / glVertexAttribPointer`。

结果（线程模式）：帧第一次**跑完整串 pass**：`… → Lines → ImRenderer → Picking`，现在停在 `Picking` 的 draw（那里又出现 FBO incomplete，说明 Picking 的渲染目标是记录侧建的，同一类问题）。

同时确认并保留的改动：上下文只归驱动线程（`QtGLContextHost::MakeCurrent` 在非驱动线程返回 false）；`PrepareSurface` 在 GUI 线程建 offscreen surface、`CreateDriver` 复用它；`MakeCurrent` 作为帧的第一条命令。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.35 Picking 的渲染目标也走驱动；它的读回是下一个点（2026-11-19 夜）

`PickingRenderPass` 的 `m_actorPickingFramebuffer` 原来在构造里用 `FramebufferUtil::SetupFramebuffer`（记录侧直接 GL）创建，每帧的 `Resize` 也是直接 GL。两处都改成 `m_renderer.GetDriver().SetupFramebuffer(...)` 与 `ResizeFramebuffer(...)` 命令。

它的第二个颜色附件（世界坐标）是带独立格式的额外目标，仍是直接 GL 创建；线程模式下先跳过，选点仍能拿到 id。

结果：线程模式第一次跑完整串 pass（Lines、ImRenderer、Picking），仍停在 Picking 的 draw，日志是 FBO incomplete；该 pass 结尾的读回（glReadPixels 那套）仍是记录侧直接 GL，是下一个要命令化的点。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.36 读回也照 Filament 变成命令（2026-11-19 深夜）

Filament 的读回是驱动 API：`DECL_DRIVER_API_N(readPixels, RenderTargetHandle, x, y, width, height, PixelBufferDescriptor&&)`（DriverAPI.inc:517），执行端 `OpenGLDriver::readPixels` 在驱动线程上 `glReadPixels`（桌面路径用 PBO + fence，完成时回调），数据由 PixelBufferDescriptor 自己持有。

对应实现：`ReadPixelsRequest`（framebufferId、readBufferIndex、x/y/w/h、format/type、`shared_ptr<vector<uint8_t>>` 数据、可选 `onDone` 回调）+ 命令 `ReadPixels` → 执行端 `glBindFramebuffer(GL_READ_FRAMEBUFFER) / glReadBuffer / glReadPixels` 后调 `onDone`。

拾取的四处读回（同步两处、异步 PBO 环一处）下一步改成走它；当前先保证默认路径不变。

Picking 的 draw 仍报 FBO incomplete：它的目标已经通过 `SetupFramebuffer`/`ResizeFramebuffer` 命令创建，所以下一步用 `MOON_CMD_TRACE` 看这两条命令是否真的在执行端跑过、以及跑的尺寸。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.37 后处理与 PingPong 的渲染目标也走驱动；Picking 目标已完整（2026-11-19 夜）

新增 `FramebufferUtil::SetupFramebufferWithDriver(...)`（两个重载）：有驱动时发 `Driver::SetupFramebuffer` 命令，否则回退原来的直接 GL。四处改用它：`AutoExposureEffect`（两处）、`BloomEffect`（两处）、`PostProcessRenderPass`、`SkyboxRenderPass` 的 BRDF 缓冲。

新增诊断：`OpenGLDriver::BindFramebuffer` 在 `MOON_FRAME_TRACE` 下打印目标 id、名字、尺寸与 `glCheckNamedFramebufferStatus`（0x8cd5 完整、0x8cd6 附件不完整、0x8cd7 缺附件）。

结果：`[fbo] bind 42 (ActorPicking) 1920x823 status=0x8cd5`，Picking 的渲染目标现在完整；线程模式不带 `MOON_SHOT` 时可以持续运行。

仍剩：带 `MOON_SHOT`（grabFramebuffer）时线程模式仍退出——抓帧路径与渲染线程共用窗口上下文的关系还要理。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.38 线程模式下整帧跑完，失败点移到 present/抓帧收尾（2026-11-19 夜）

带 `MOON_FRAME_TRACE` 的线程模式日志显示：整串 pass 都跑完，每次绑定都是 `status=0x8cd5`（完整），最后一条是 Picking 的 draw 之后，随后进程退出。崩溃已移到 pass 之后的收尾：EndFrame、CopyFramebufferColor、PresentTo、最后一次 drain、BindFramebuffer(nullptr)，以及 grabFramebuffer 的抓帧。

不带 `MOON_SHOT` 时同一条路径可以持续运行，所以更可能在抓帧那一侧：Qt 在 GUI 线程用自己的上下文读窗口 FBO，而帧的收尾命令在渲染线程上执行。

下一步：给 AView::Render 的收尾各段加里程碑确认崩在哪一段；再按 PlatformGlfwGL 的思路把呈现完全交给平台（commit(swapChain) 拷进环形纹理、GUI 线程只做 blit），而不是让 GUI 线程读模块的 FBO。


### 8.39 帧尾里程碑 + 现状（2026-11-19 深夜）

给 `AView::Render` 的收尾加了 `TraceView` 里程碑（BeginFrame / DrawFrame / EndFrame / CopyFramebufferColor / PresentTo / drain / BindFramebuffer(nullptr)），两条路径（正常与 minimal）都有。线程模式下日志停在 `[view] DrawFrame`，与 pass 里程碑合起来看就是 **Picking** 这一层：`[pass] Picking → draw → [fbo] bind 42 (ActorPicking) 0x8cd5`，之后退出。

所以现在的失败点是 Picking pass 的绘制/读回本身（它的目标已完整）。下一步是给 `PickingRenderPass::Draw` 内部加里程碑（bind、scissor、逐个 drawable、结尾读回），定位到具体语句。

已经稳定成立的部分：命令流（录制/执行分离）、makeCurrent 命令、上下文只归驱动线程、framebuffer/顶点布局/缓冲区上传/描述符/读回全部命令化、默认路径逐字节不变。


### 8.40 HAL 侧绑定也纳入诊断；后处理目标自愈（2026-11-19 深夜）

诊断覆盖率补全：`GLBackend::BindFramebuffer`（HAL 自己那条路径，例如 `Framebuffer::Bind()`）也在 `MOON_FRAME_TRACE` 下打印 id 与 `glCheckNamedFramebufferStatus`。之前只打印驱动命令那条路径，绕过去的目标是看不见的。

结果：不完整的目标正是后处理那几个 —— id 10、34、35（PingPong0）、36（PingPong1），状态 `0x8cd7`（缺附件），而且都是经 HAL 直接绑定的。原因：它们在本 pass 构造时创建，那一刻模块上下文可能还不存在，附件就没建上。

修法：`PostProcessRenderPass::Draw` 开头自愈 —— 目标没有颜色附件时就再发一次 `SetupFramebufferWithDriver`（命令），它在同批的绘制之前执行。

验证：默认路径 obj 与 `base-obj.png` 0 差异；线程模式仍在 Picking 之后退出（下一步要它的读回命令化）。


### 8.41 Picking 里的 gizmo 拾取绘制也挡住（2026-11-20 凌晨）

`PickingRenderPass::Draw` 里还有一处记录侧 GL：`ImRenderer::instance().drawMeshPick()`（gizmo 的拾取几何，走的是 ImRenderer 自己的直接 GL）。线程模式下先跳过——gizmo 暂时不可拾取，其余照常。

验证：默认路径 obj 与 `base-obj.png` 0 差异；线程模式仍在 Picking 之后退出，说明剩下的不是这一处，而需要一条真正的崩溃栈（c d b 在本机符号化不稳，看到的帧落在 ImPlot 上，不可信）。

下一步（需要调试器）：在崩溃点取调用栈，确认是 Picking 的绘制命令、还是随后 `EndFrame/PresentTo` 的收尾。


### 8.42 Tracy 空指针修复 + 呈现目标是空的（2026-11-20 凌晨）

崩溃真因（用户用调试器定位）：`GLBackend::OnFrameCompleted` 里的 `TracyGpuCollect` —— Tracy 的 GPU 上下文是线程本地的，在 `Init`（渲染线程）创建，却在 GUI 线程 collect，于是 Collect 里 this 为空。修法：`GLBackendContext::initThreadId` 记录创建线程，不是该线程就直接返回。

修复后：线程模式不再崩溃、正常退出、抓帧成功（1160x487）。

但画面是空的。新增探针（`MOON_FRAME_TRACE` + 线程模式）用 ReadPixels 命令读呈现目标中心像素：`[present] centre pixel = 236 236 236 255`，即模块自己的帧就是纯灰，不是粘贴的问题。下一步：在场景 pass 之后读 `m_framebuffer` 与 `m_msaaframebuffer` 的中心像素，逐个目标定位内容在哪一步丢的。

验证：默认路径 obj 与 `base-obj.png` 0 差异。


### 8.43 三步探针定位：清屏有、绘制无（2026-11-20 凌晨）

在 AView::Render 里加三步探针（都走 ReadPixels 命令，线程模式 + MOON_FRAME_TRACE）：

after scene (msaa) = 0 0 0 0（读 MSAA 本身非法，忽略）；after resolve = 236 236 236 255；present centre = 236 236 236 255。

对照默认路径同一位置是 (241, 38, 241)（模型的品红）。所以线程模式里只有清屏、没有几何体。

已修：Descriptor::Bind 在线程模式下改走 BindDescriptor 命令（UBO 与纹理绑定），默认路径不变。

推断（下一步验证）：剩余原因是着色器程序在记录侧编译。GLShaderProgram 里的 glCreateProgram / glCompileShader / glUniformBlockBinding 在文档加载时（GUI 线程）执行，线程模式下没有上下文，程序因此不可用——清屏能过、绘制全部无效。Filament 对应的是 createProgram 命令：编译发生在驱动线程。这就是程序创建命令化这一块，也是把画面找回来的最后一大块。

验证：默认路径 obj 与 base-obj.png 0 差异。


### 8.44 剩下的最后一块：程序/纹理创建命令化（计划，2026-11-20 凌晨）

现状：线程模式跑完、退出干净、抓帧正常、命令流与上下文归属都对；画面空白，探针证明"清屏有、绘制无"。

原因：文档加载阶段的 GPU 对象仍在记录侧创建 - GLShaderProgram 的 glCreateProgram、glAttachShader、
glLinkProgram、QueryUniforms，以及 shader stage 的编译、纹理创建与上传，都发生在 GUI 线程；线程模式下那里没有上下文。

Filament 的形状（照着做）：createProgram / createShader / createTexture 都是驱动 API 命令，
源码与描述在录制侧打包，编译与创建在执行侧；前端拿到的只是 handle。

落地步骤（文件级）：

1. 新增 ProgramRegistry 与 ShaderRegistry（handle 到 GL 名字），照 BufferRegistry 的写法；
2. 请求与命令：CreateShaderRequest（handleId, stage, source）、CreateProgramRequest（handleId, shader handles），
   源码堆上自持，参照 UpdateBufferRequest；
3. 执行端：编译、attach、link，并把 uniform/block 信息反射成 CPU 侧数据存进注册表；
4. 前端 GLShaderProgram：只分配 handle 并发命令；GetID 变成注册表查找（延迟到执行侧）；
   Bind 改为命令（BindProgram），与 BindDescriptor 一致；
5. InitializeDescriptorBindings 里的 glGetUniformLocation 与 glGetUniformBlockIndex 换成源码反射
   （MaterialParser 已经解析出 uniform 与 sampler 字段），槽位本来就由 layout 决定；这样录制侧完全不需要 GL 查询。

估时：这是一次跨 HAL/Rendering 的重构，单个构建循环十分钟，建议在带调试器的新会话里做，
而不是在已连续工作十余小时的会话尾部。

更正（2026-11-20 上午，用户指出）：8.44 第 5 点原本写成"用源码反射替换 glGetUniformLocation / glGetUniformBlockIndex"，这是错的。

InitializeDescriptorBindings 的设计意图是**把 layout 的槽位声明给 GL**：
- 采样器：glGetUniformLocation(name) 取 location，再 glUniform1i(location, slot) 告诉它读哪个纹理单元；
- uniform block：glGetUniformBlockIndex(name) 取 index，再 glUniformBlockBinding(program, index, slot) 指定 binding point。
两个 Get 不是反射，而是取得 setter 需要的句柄；除非 shader 源码写死 layout(binding = N)，否则这层无法去掉。

正确的拆法是：
1. 槽位编号（descriptorSlots 的内容）本来就是纯 CPU：引擎固定表 + bufferSlot/textureSlot 计数器，可独立算出来；
2. 声明那一步（两个 Get + 两个 setter）是 GL，搬到执行端，在 link 之后跑一次
   （放进 createProgram 命令里，或单独的 DeclareProgramLayout 命令）；
3. 录制侧只用 CPU 槽位表查 GetDescriptorSlot，不做任何 GL 查询。

### 8.45 统一的 HandleAllocator 取代六个 Registry（2026-11-20）

问题：每类后端资源各写了一套 Registry（Buffer / VertexArray / Framebuffer / Renderbuffer /
Program / Texture），六个文件里五个是同一段"句柄→GL 名字"表格的复制。抽象不够：Filament
只有一个 `HandleAllocator`。

参照（本机 `Qt-Filament`，即 S/R 成对的那一版）：
`backend/include/backend/Handle.h`、`backend/include/private/backend/HandleAllocator.h`，以及
`OpenGLDriver.h:272-311` 把 `allocateAndConstruct/construct/deallocate/handle_cast/is_valid`
包成 `initHandle/construct/destruct/handle_cast/is_valid` 的用法。

**落地**：

| 新增 | 内容 |
|---|---|
| `Rendering/Backend/HandleAllocator.{h,cpp}` | 单一 id 空间（27 位索引 + 4 位 age，索引存 +1 让 0 继续表示"无句柄"）；`Allocate<D>`（S 侧，不建对象）、`Construct<D>`（R 侧，槽位里 placement-new）、`Deallocate`、`HandleCast`、`IsValid`；槽位持有对象的析构函数指针，释放时 age+1，过期句柄解析为空而不是野指针 |
| `Rendering/Backend/BackendResources.{h,cpp}` | 后端记录 `HwBufferObject` / `HwVertexArray` / `HwFramebuffer` / `HwRenderbuffer` / `HwProgram`（GL 名字只存在这里）+ `Records::Create*`（执行侧，唯一的 glCreate 处）、`Records::Destroy*`（记录立即释放、glDelete 交给 `DeferredDeleter`）、`Records::*Name`（handle_cast + 字段读取） |

纹理的记录就是 `HAL::Texture` 本身（构造函数建 GL 名字，析构删），所以 `CreateTexture` /
`DestroyTexture` 是 `Construct<HAL::Texture>` / 排队 `Deallocate<HAL::Texture>`。

**删除**：`BufferRegistry` / `VertexArrayRegistry` / `FramebufferRegistry` /
`RenderbufferRegistry` / `ProgramRegistry` / `TextureRegistry`（12 个文件）。

**HAL 侧**：`GLBufferContext` / `GLVertexArrayContext` / `GLFramebufferContext` /
`GLRenderbufferContext` 只剩一个 `ResourceHandle<Hw*>`，`id` 与 `handleId` 两个字段消失，
`GetID()` 变成 `handle_cast`（GL 名字不再在前后端各存一份，§7.3 的耦合源头少了一处）。
调用点：`Resources::Texture`、`TextureLoader`、`Im3DType`、`Im2DRender`、`Descriptor`、
`BvhService`、`OpenGLDriver` 的执行端。

与 Filament 的两处有意偏差（都写在头文件里）：槽位用一次堆分配而不是三档 arena（那是移动端的
分配速度优化，契约相同）；分配器是进程级单例（Filament 是 `DriverBase` 的成员，等 HAL 能拿到
驱动之后再收进去）。

**验证**（1254x480，同一会话）：

- obj 与 `base1254-obj.png` **0 差异**（max delta 0）；
- `3.moon` 与 `base1254-1.png` **0 差异**；
- 同一构建连续运行三次，两两 **0 差异**；连续 4 次 `exit 0`、出图；
- 线程模式（`MOON_RENDER_THREAD=1`）不崩、干净退出、抓帧成功，画面仍是纯灰——即 §8.44 的
  "程序创建还在录制侧"未变，符合预期。

**还没做**：`Resources::MeshRegistry` 是前端对象表（Filament 没有对应的东西，它的前端对象是
引用计数的），正确的归宿是 `Mesh` 只持 `HwVertexBuffer`/`HwIndexBuffer` 句柄、延后上传的命令
带这两个句柄；那与 §7.14 的 "VAO/IBO 构造命令化" 是同一件事，届时该表消失。

### 8.46 Mesh 的 VAO/VBO/IBO 变成句柄（2026-11-20）

§8.45 之后还剩一张表：`Resources::MeshRegistry`。原因不是"某类 GL 资源"，而是 `Mesh` 直接持有
`HAL::VertexBuffer` / `unique_ptr<HAL::IndexBuffer>` / `unique_ptr<HAL::VertexArray>`，再把"自己"
注册进一张表，让延后上传能找到它。

按 Filament 的形状改：**Mesh 只持句柄**。

| 件 | 内容 |
|---|---|
| `Mesh` | `ResourceHandle<HwBufferObject>`（VBO）、`vector<ResourceHandle<HwBufferObject>>`（每个子范围的 IBO）、`vector<ResourceHandle<HwVertexArray>>`（每个子范围的 VAO）；`GetVertexArrayId()` = `Records::VertexArrayName(handle)` |
| `Rendering/HAL/Common/ResourceOps.{h,cpp}`（新） | 句柄寻址的后端操作：`EnsureBuffer` / `EnsureVertexArray` / `AllocateBuffer` / `UploadBuffer` / `SetVertexArrayLayout` / `DestroyBuffer` / `DestroyVertexArray`。有驱动就录 `CreateBuffer` / `AllocateBuffer` / `UpdateBufferRange` / `SetVertexArrayLayout` 命令，没有驱动就内联执行 —— 与 `VertexBuffer::setBufferAt` → `DriverApi.updateBufferObject` 同一分工 |
| `GLBuffer` / `GLVertexArray` | 对应方法改为委托 `ResourceOps`，录制/内联的分支只剩一处实现 |
| 删除 | `Resources/MeshRegistry.{h,cpp}`；`ResourceHandle.h` 里的 `ResourceHandleAllocator<T>`（它唯一的使用者就是 MeshRegistry）。**代码里现在只有 `Backend::HandleAllocator` 一个分配器** |

`Mesh::Upload` 也不再走 `DeferredDeleter`：创建、分配、上传、布局全是可安全录制的命令，谁的线程
调用都行（`DeferredDeleter::AreUploadsDeferrable()` 在 Mesh 上不再需要）。`UploadIndices` /
`AddSubRangeBuffer` 同样走 `ResourceOps`。

`PointRenderPass` 原来用 `getVertexArray()/getVertexBuffer()` 拿 HAL 对象再 `Bind()`，现在按句柄
`Records::VertexArrayName/BufferName` 解析名字（它那几行 `glVertexAttribPointer` 仍是记录侧直接 GL，
属于 §7.14 清单里的同类工作）。

**验证**（1254x480）：obj 与 `base1254-obj.png` **0 差异**；`3.moon` 与 `base1254-1.png`
**0 差异**；线程模式 `MOON_RENDER_THREAD=1` exit 0、抓帧成功，画面仍纯灰（§8.44 的程序创建未接线），
`GL_INVALID_*` 条数 9626 → 9637，与上一轮同量级。

**另记**：3.moon 中途一次 0xC0000005 经确认是 **ImGui/viewcube 的悬停崩溃**（用户定位），与本次改动
无关；鼠标停在视口外重跑即 0 差异。

### 8.47 程序创建命令化（§4.1 接线，2026-11-20）

照 Filament 的 `createProgram` / `bindProgram` 把程序这一层接上命令流：

| 件 | 内容 |
|---|---|
| `GLShaderStageContext` | `id` 默认为 0（线程模式下不建 GL 对象），新增 `source`（`mutable`，因为 `Upload` 是 const）与 `GetShaderType()/GetSource()`：源码随程序的创建命令走 |
| `GLShaderProgramContext` | `id` 不再是 `const`（线程模式下保持 0），新增 `ResourceHandle<HwProgram> handle`；`GetID()` 在线程模式下 = `Records::ProgramName(handle)` |
| `CreateProgram` | 已存在的命令，现在**真的有人发**：`Link()` 在线程模式下打包各 stage 的 `{type, source}` 与描述符布局，`driver->CreateProgram(...)` + `ExecutePendingCommands()`（资源创建对调用方是同步的，与贴图一致） |
| `BindProgram`（新） | 线程模式下 `Bind()/Unbind()` 不再 `glUseProgram`，改成记录这条命令；执行端 `OpenGLDriver::BindProgram` 就是 `glUseProgram` |
| `DeclareProgramBindings`（新） | 槽位声明原来是 `Bind()` 里的直接 GL；线程模式下改为记录命令（执行端 `DeclareProgramBindings`），仍然是"首次绑定时才声明"的惰性语义 |
| `ShaderLoader::ValidateBindings` | 它在录制侧查 `glGetUniformBlockIndex`/`GL_UNIFORM_BLOCK_BINDING`，线程模式下直接跳过（那是校验，不影响渲染） |
| `Attach/Detach/DetachAll` | 线程模式下只维护 stage 列表（Link 从里面读源码），不做 GL；`Detach` 的比较改成按地址（线程模式下所有 stage 的 GL 名都是 0） |

`QueryUniforms()`（反射）在线程模式下直接返回：它是 `glGetActiveUniform/glGetUniformLocation`，属于执行侧，还没搬过去。

**验证**（1254x480）：

- 默认路径 obj 与 `base1254-obj.png` **0 差异**；`3.moon` 落在**已知的悬停稳定态**（同一构建两次运行 0 差异，与基准相差 2447 px，与 §6.12/§8.9 记录的固有抖动同值）；
- 线程模式：exit 0、抓帧成功，但**画面从"纯灰"变成了"纯黑"**——这是前进的一步，不是回归：
  `[probe] after resolve = 234 234 234 255`（场景目标里是清屏色），`[present] centre pixel = 0 0 0 255`。
  也就是说 **present pass 现在真的执行了**（程序在渲染线程上编译、链接、绑定都通了），只是它采样不到源纹理：
  present 的源纹理是**松散 uniform**（`_InputTexture`）绑定的，而这条路径仍在录制侧发 GL。

**下一步（新发现的缺口，按顺序）**：

1. **松散 sampler uniform**：`_InputTexture` 这类不在材质参数块里的采样器，现在靠 `MaterialInstance::UploadProperties` 在录制侧 `glUniform1i` + 纹理单元绑定；线程模式下这些调用无效，present 因此采到黑。做法与 `BindDescriptor` 一致：把"给程序里的采样器设单元 + 把纹理绑到该单元"变成命令（Filament 里就是 `SamplerDescriptor` 随 material instance 走）。
2. **uniform 反射**（`QueryUniforms`）搬到执行侧，结果写进 `HwProgram` 记录，录制侧 drain 之后读取（与贴图创建同一种"记录 → 等 → 读"语义）。
3. 之后再回头看场景几何：obj 场景的 resolve 仍是清屏色，说明几何绘制同样卡在材质绑定这条路径上。

**顺带待清**：`DeferredDeleter::SetUploadsDeferrable` 现在没有读者（`viewportframe.cpp:252` 仍在校置位），下一轮删掉。

### 8.48 反射回到录制侧；present 采到图了（2026-11-20）

框架纠正（用户指出）：材质设属性是**纯 CPU** 的事，写到前端描述符集里；成为命令的只有**描述符集的提交**；
把后端描述符绑到管线是 `OpenGLDriver` 在绘制时按 binding map 做的。所以"松散 sampler 命令化"只是过桥手段，
长期形状仍是 §8.49 要做的描述符集提交。

本轮把**反射**接回录制侧，这是上一条黑屏的根因：

| 件 | 内容 |
|---|---|
| `Backend::HwProgram` | 记录里新增 `uniforms` 与 `uniformLocations` 两张表（Filament 把它们放在后端 program 上） |
| `OpenGLDriver::CreateProgram` | link 之后在本侧跑 `ReflectUniforms`：`glGetActiveUniform` / 跳过 `ubo_*`、`ReflectionUBO`、跳过 block 成员 / `glGetUniformLocation`，写进记录 |
| `GLShaderProgram::Link()` | drain 之后把记录里的两张表拷回 `GLShaderProgramContext`（"记录 → 等 → 读"，与贴图创建同一语义） |
| `MaterialInstance::BindTexture` | 线程模式下记录 `BindDescriptor{Texture, unit}` + `SetSamplerUnit`（上一轮已加），非线程路径不变 |

**验证**（1254x480）：

- 默认路径 obj 与 `base1254-obj.png` **0 差异**；
- 线程模式：`[probe] after resolve = 234 234 234 255`、**`[present] centre pixel = 234 234 234 255`**
  ——present pass 第一次**采到了源纹理**（之前是 0），截图不再是单一纯色（61 种颜色，含 UI）。
  即：程序创建 → 绑定 → 槽位声明 → 松散 sampler 设单元 → 纹理绑到单元 → present 绘制，整条链路在渲染线程上通了。

**仍缺**：场景本身还只有清屏色（几何没画上去）。下一个检查点是材质参数块与每帧的 UBO/描述符绑定
（`BindParameterBlock` / `EngineUBO` 那一层）在渲染线程上是否真的生效——探针应加在 `DrawMaterial` 前后。

### 8.49 线程模式：几何确实在画，但落在不完整的 FBO 里（2026-11-20）

> 本节的推断在 §8.50 被修正：不完整目标只是表象之一，真正的三个坑见 §8.50。

从 `MOON_FRAME_TRACE` 的日志里直接读出来的（无需改代码）：

- `[dm]` 一共 28809 条，其中 **3492 条是模型本身**：`mesh=… sub=0 vao=4 idx=28284 vtx=5321 inst=1 prim=4`，
  并且每条后面都是 `context=current`。也就是说**场景的绘制命令在渲染线程上确实执行了，VAO 与索引数都对**。
- 但 `[fbo] bind` 里有大量不完整目标（`status=0x8cd7` = 缺附件）：

  | 目标 | 次数 |
  |---|---|
  | `bind 35 (PingPong0)` | 3476 |
  | `bind 36 (PingPong1)` | 2607 |
  | `bind 10 ()` | 869 |
  | `bind 34 ()` | 869 |

  与 `GL_INVALID_FRAMEBUFFER_OPERATION` 的 9603 条一一对应（另有 873 条 `FBO anti-alias method is
  not valid for read pixels`，来自 MSAA 目标上的读回）。

结论：**不是"没画"，而是画进了不完整的目标，被 GL 丢弃**；探针读到的 234 只是清屏色。
这四处正是 §8.40 里加过"自愈"（`PostProcessRenderPass::Draw` 发现没有颜色附件就再发一次
`SetupFramebufferWithDriver`）的那几个目标——现在自愈没有生效，需要查：自愈的条件判断
（`GetAttachment`）在"附件对象存在但 FBO 状态不完整"时不成立，或者 10/34 这两个无名目标根本不经过
`PostProcessRenderPass`。下一步就是把这四个目标的创建/重建也纳进执行侧（`SetupFramebuffer` 命令），
并在 `[fbo]` 里按 id 打出是哪个 pass 绑的。


### 8.50 线程模式出画面了：纹理句柄化 + 目标附件命令化（2026-11-20 下午）

§8.49 的结论（"画进了不完整的目标"）方向对，但只解释了一部分。按 §7.3 的顺序做下去，层里还有两个更大的坑，靠新加的探针逐个挖出来。

**§7.3 第 1 步：目标身份与 pass 归属**

新加三处诊断（都在 `MOON_FRAME_TRACE` 下）：

| 打印 | 位置 | 作用 |
|---|---|---|
| `[fbo] new name= handle= fbo=` | `GLFramebuffer` 构造末尾（创建命令 drain 之后） | 名字为空的 FBO 也能按 handle/id 认领 |
| `[fbo] bind ... pass=` / `[dm] ... pass=` | `OpenGLDriver`，标签来自 `Rendering/Utils/FrameTrace` | 执行侧知道是哪个 pass 记的命令（录制侧 `CompositeRenderer`/`AView` 发布标签） |
| `[passprobe] <pass> centre = r g b a` | `CompositeRenderer::DrawFrame`（`MOON_PASS_PROBE`） | 每个 pass 之后把帧目标解到 present 再读中心点，定位"画面在哪一步丢了" |

实测（1254x480，线程模式）：

- `fbo 1` = `AView::m_msaaframebuffer`（outputMsaaBuffer，**完整**）；
- `fbo 34` = `GridRenderPass::m_mirroFbo`（**不完整**，`0x8cd7`）；
- `fbo 10` = Gbuffer 那个目标（**不完整**，它由 `Blit` 绑定，所以能看见）；
- 其余目标都完整。**`§8.49 的 10/34 猜对了一半**：34 是镜子，10 是 gbuffer 系。

**两个新发现（关键）**

1. **GbufferPass 用的是直接 GL 绑定**（`gbuffer.Bind() / ssaobuffer.Bind()`）：线程模式下是空操作，于是 gbuffer 与 SSAO 的绘制全落到帧目标（MSAA）上——`[passprobe] Gbuffer = 255 255 255`（SSAO 的全屏白），后面所有 pass 都在这个白底上画，最终被后处理压成一片灰。
2. **HAL 纹理在录制侧建**（`std::make_shared<HAL::Texture>`：三个 fallback 空纹理、gbuffer/镜子的附件纹理等）：`glCreateTextures` 在录制线程没有上下文，名字是 0。`[dm] T u_DiffuseMap unit=3 tex=0`（默认路径是 `tex=14`）——材质采样不到东西。
3. **后处理的 MSAA 解析用的是直接 GL**（`PostProcessRenderPass` 调 `FramebufferUtil::CopyFramebufferColor`，不是驱动命令）：解析被丢掉，present 里一直是上一帧的内容，于是后处理采样到的是背景。这条是靠 `MOON_PASS_PROBE=c|r|d` 二分出来的：只加"每 pass 复制一次"就把画面找回来了，只加 drain 则没用。

**落地**

| 件 | 内容 |
|---|---|
| `GLTextureHandleContext` | 新增 `handleId`：`id != 0` 表示"这个名字就是 GL 对象"（后端对象，或无线程时内联创建），否则是前端句柄 |
| `TTexture`（`GLTexture.cpp`） | 前端构造只取句柄 + 记 `CreateTexture`（Filament 的 createTextureS）；新增 `BackendConstructionTag` 构造给执行端用（`Records::CreateTexture` 走它，否则递归）；`Allocate / Upload / UploadSubImage / GenerateMipmaps / SetBorderColor` 在非驱动线程上记命令；析构按句柄排队释放 |
| 新命令 | `SetTextureBorderColor`、`GenerateTextureMipmaps`（`TextureRequests.h`）、`AttachTexture`、`SetFramebufferDrawBuffers`（`FramebufferRequests.h`） |
| `GLFramebuffer` | `Attach(texture)` / `SetDrawBuffers` 在非驱动线程记命令；`Validate` 在录制侧返回 true（状态只有执行侧看得见，看 `[fbo] bind` 的 status）；新增 `GetAttachmentShared<T>`（pass 把附件当材质属性时要持有 shared_ptr） |
| `Driver::IsDriverThread()` | Filament 的 `isThisThreadTheDriverThread`：无渲染线程时恒真（默认路径行为不变），有渲染线程时只有执行线程为真 |
| `GbufferPass` / `GridRenderPass` | 目标绑定改走 `m_renderer.BindFramebuffer`，尺寸改走 `ResizeFramebuffer` 命令 |
| `PostProcessRenderPass` | MSAA→present 解析改走 `GetDriver().CopyFramebufferColor` |
| 诊断闸门 | `MOON_FRAME_TRACE`（pass 标签、`[fbo]`、`[setup]`）、`MOON_DRAW_TRACE`（几何绘制的状态/UBO/采样器/VAO dump）、`MOON_PASS_PROBE`（每 pass 像素探针，值可取 `c`/`r`/`d` 二分） |

**验证**（1254x480，鼠标在视口外）：

- 默认路径：obj 与 `base1254-obj.png` **0 差异**（三次构建各一次，均 max delta 0）；
- 线程模式：**画面与基准只差右上角导航方块那一块**（5978 px，bbox `(1112,6)-(1248,142)`，其余逐字节相同）；
- 线程模式：不完整 FBO 绑定 **0 条**、`GL_INVALID_FRAMEBUFFER_OPERATION` **0 条**、exit 0、同一构建两次运行 **0 差异**；
- `3.moon`：线程模式 exit 0；默认路径同一构建两次运行 0 差异，与 `base1254-1.png` 差 2447 px = 已知悬停抖动（§8.47 同值）。

**下一步**

1. **叠加层**：`AView::Render` 的 `m_drawOverlays()`（ImGui、导航方块、2D 列表、gizmo）仍在录制线程发 GL，线程模式下等于没画——这就是右上角那块差异。做法同其它层：把叠加层的绘制记成命令（或把它的 GL 收进一个"overlay 批次"）。
2. **剩余直接 GL 清单**（§4.3）：`FramebufferUtil::CopyFramebufferDepth` 的调用点（`SceneRenderer` 图层路径、HZB）、`GLRenderbuffer` 的创建/Attach、`HzbBuildPass`（线程模式未启用）、拾取读回。
3. 诊断清理：`[fbo] setup/attach`、`[pp] constructed`、`[setup] record/execute` 可以删；`DeferredDeleter::SetUploadsDeferrable`（viewportframe 仍在置位）确认无读者后删。


### 8.51 叠加层第一步：Im3D（gizmo / 导航方块）命令化（2026-11-20 傍晚）



§8.50 之后线程模式只差右上角导航方块那块。查下去发现两个原因叠在一起：

1. `viewportframe.cpp` 里 `overlayOff = minimalFrame || module->IsThreaded()`，并且线程模式会把 `ImRenderer`（gizmo）pass 直接 `SetEnabled(false)`——这是 §8.25 "先摘掉叠加层" 留下的；
2. 就算开着，Im3D 的 GL 也全是直接调用：`Im3DGLRenderer` 自己的 VAO/VBO、`PolygonMesh` 的 VAO/VBO 与逐帧上传、`glViewport`、`glEnable(GL_PROGRAM_POINT_SIZE)`。

Im3D 这一层的形状本来就接近 Filament 的 immediate primitive：一份顶点流 + 一个材质 + 一次 draw。所以直接命令化，不新增 GL 出入口：

| 件 | 内容 |
|---|---|
| `OverlayRequests.h`（新） | `UploadOverlayVerticesRequest`（顶点流 + 步长 + 额外属性数；偏移就是编辑器 `VertexFormat` 的 0/16/20/32）、`DrawVertexArrayRequest`（VAO 句柄 + 数量 + GL 图元 + 可选视口） |
| 两条新命令 | `UploadOverlayVertices`（执行端：绑 VAO/VBO、按步长重设属性指针、按需开关属性 4/5、`glBufferData` STREAM）、`DrawVertexArray`（绑 VAO、可选视口、`glDrawArrays`） |
| `Im3DGLRenderer` | VAO/VBO 变成 `ResourceOps` 的句柄（`Initialize`）；`Begin` 的点精灵改成 `EnqueueCapability(PROGRAM_POINT_SIZE)`、视口走 `SetViewport`；`Draw` 改成"上传 + 画"两条命令；`DrawBoundTriangles` 换成 `DrawMesh(driver, count, vaoHandle, viewport...)` |
| `PolygonMesh`（`Im3DType.h/.cpp`） | `vao`/`vbo` 变成句柄；`initGpuBuffer` = `EnsureVertexArray/EnsureBuffer`；`submit` = 一条 `UploadOverlayVertices`；`bind()` 不再发 GL（draw 自己绑）；析构走 `Destroy*`；纹理缓冲（逐面边值、widget 块色）改走 `EnsureTextureBuffer`（有渲染线程时记 `CreateTexture`，否则仍是直接 `Records::CreateTexture`） |
| 叠加层开关 | 新增 `ViewportOverlay::BeginGizmoFrame`（只开 3D 叠加层的帧：相机/视口/输入，不碰 ImGui）；`viewportframe.cpp` 在两种模式下都 `initGizmoGl()`，线程模式调用 `BeginGizmoFrame` 并且不再禁用 `ImRenderer` pass |

**验证**（1254x480）：

- 默认路径 obj 与 `base1254-obj.png` **0 差异**；
- 线程模式：与基准的差异从 **5978 px 降到 1413 px**，全部落在导航方块外圈的图标上（bbox 仍是 `(1112,6)-(1248,142)`）——方块本体、坐标轴、左/前标签已经一致；
- 线程模式：`GL_INVALID_*` 0 条、不完整 FBO 0 条、exit 0、同一构建两次运行 **0 差异**。

**顺手踩到的坑（记住）**：用 PowerShell 5.1 的 `Get-Content -Raw` / `Set-Content` 改这些源文件会把 UTF-8 中文注释读成 GBK 再写回去，文件立刻不是合法 UTF-8。改 C++ 只用编辑工具；已经用"按 GBK 还原字节 → UTF-8 解码"救回来了（`Im3DRenderer.cpp` 有几句注释的末字是按上下文补的）。

**还差**：ImGui 与 2D draw list（`Im2DRender`、`ImGuiGLRenderer`）——导航方块外圈的箭头图标就是它们画的。它们仍然是直接 GL，所以线程模式下 `overlayOff` 保持为真、`BeginFrame/EndFrame` 不调用。

**另记（第二轮的回归与修正）**：`Begin` 里原来的 `glEnable(GL_POINT_SPRITE)` 一开始被当成遗留删掉了，结果 3.moon 的草图控制点（红色点精灵）在**默认路径**上消失——编辑器跑的是兼容 profile，点大小要它和 `PROGRAM_POINT_SIZE` 同时打开。现在两个都走 `EnqueueCapability`（`ERenderingCapability` 新增 `PROGRAM_POINT_SIZE`/`POINT_SPRITE` + GL 映射），3.moon 默认路径重新与 `base1254-1.png` **0 差异**。

**3.moon 线程模式仍是黑的（不是这一轮引入的）**：实体全黑、叠加层正常。`MOON_DRAW_TRACE` 的采样器表显示 `_IrradianceCube / _PrefilterCube = 0`——天空盒 capture（`computeSkyTexture`）在线程模式下被 §4.3 的 `if (!IsRenderThreadActive())` 跳过了，PBR 材质因此没有环境光。下一步要把这段 capture 也命令化。

### 8.52 天空盒 capture 命令化；3.moon 的实体在线程模式亮起来了（2026-11-20 晚）

3.moon 在线程模式下实体全黑的原因就是 §8.51 结尾写的：`SkyboxRenderPass::Draw` 里

```cpp
if (needUpdateSkyTexture && m_captureEnvironment) {
    if (!m_renderer.GetDriver().IsRenderThreadActive()) { computeSkyTexture(); }
}
```

`_IrradianceCube`/`_PrefilterCube` 因此是 0，PBR 材质没有环境光。

这一轮把 capture 整段搬进命令流：

| 件 | 内容 |
|---|---|
| 新命令 | `AllocateRenderbuffer`（`glNamedRenderbufferStorage{,Multisample}`）、`AttachRenderbuffer`（`glNamedFramebufferRenderbuffer`）、`CopyFramebufferDepth`（深度 blit，§4.3 的图层/HZB 用） |
| `GLRenderbuffer` | `Allocate` 在非驱动线程上记命令（Filament 的 allocateRenderbufferStorage）；新增 `GetHandleId()`（attach 按句柄走） |
| `GLFramebuffer` | `Attach(renderbuffer)`、`AttachLevel(texture)`（带 level/layer，prefilter 逐 mip 逐面用）、`SetTargetDrawBuffer` 都加了录制分支 |
| `SkyboxRenderPass` | `computeSkyTexture` 里所有 `XXX.Bind()/Unbind()` 换成 `m_renderer.BindFramebuffer/BindDefaultFramebuffer`；去掉"线程模式跳过 capture"的闸门 |

**验证**（窗口这次变成 1314x498，与 1254x480 的旧基准不可比，所以改成同尺寸下 default vs thread）：

- obj：default 与 thread 只差导航方块图标 **1413 px**，两次运行各自 **0 差异**；
- 3.moon：线程模式**实体不再是黑的**（环境光回来了），红点、蓝线、导航方块都在；`GL_INVALID` 两种模式都是 **0 条**；
- 天空 capture 在两种模式都只跑 1–2 次（`[sky] capture` 计数），不是每帧。

**新发现（下一步的首要问题）：3.moon 线程模式明暗不稳定**

同一构建连跑两次，实体明暗差别很大（20%–44% 像素，有时整个实体变成近白、没有明暗）。默认路径两次运行 0 差异，obj 场景线程模式也 0 差异，所以问题在 3.moon 这条路径上。目前最可疑的是**材质签名缓存的所有权**：

- `DrawMaterialRequest.previousSignature` 是**指针**，指向录制侧渲染器的 `m_previousMaterialSignature`；
- 执行端在画完之后通过这个指针写回签名（`OpenGLDriver::DrawMaterial` 末尾 `*p_request.previousSignature = signature;`）；
- 于是录制侧推进到下一帧的新签名、执行端却在写上一帧的旧签名 —— 缓存说"已经上传过"，而 GL 里的参数块来自别的材质/上一帧，于是这一帧拿到默认参数块（近白、无明暗）。

### 8.53 材质"已绑定"缓存搬进执行侧；3.moon 的不稳定消失了（2026-11-20 夜）

§8.52 记的那个不稳定（同一构建两次运行实体明暗差 20–44%，有时整块变近白）是**缓存的所有权问题**，不是渲染本身：

| 之前 | 现在 |
|---|---|
| `ABaseRenderer::m_previousMaterialSignature`（录制侧字段），`DrawMaterialRequest.previousSignature` 是指向它的**指针**，执行端画完通过指针写回 | 缓存是 `OpenGLDriver::m_previousMaterialSignature`，请求里不再有指针；读、比较、写全在执行侧 |
| 每帧在 `ABaseRenderer::BeginFrame` 里 reset（录制侧） | 每帧在 `OpenGLDriver::MakeCurrent`（帧的第一条命令）里 reset |
| 录制侧推进到下一帧的签名、执行端才写回上一帧的 → 比较结果与实际 GL 状态对不上 → 这一帧用默认参数块 | 读写都在同一条命令流顺序里 |

**验证**（1314x498）：

- 默认路径 obj、3.moon 与改动前逐字节相同（0 差异）；
- 线程模式 3.moon 连跑三次：两次完全相同，第三次与它们差 **2420 px**——不再有 20–44% 的整块明暗漂移，剩下的是一个**双稳态**差异：草图的红点（`GizmoPoint` 的 `gl_PointSize = aPositionSize.w`）在有些运行里明显偏大。默认路径不出现，同一构建内每个状态自身是稳定的，所以下一步查"点的 size 从哪来"（`ImRenderer` 的 size 栈 / 悬停高亮 / 首帧 `PROGRAM_POINT_SIZE` 的时序）。相关文件：`Im3DRenderer.cpp` 的 `drawPoint`/sizeStack、`Im3DType.cpp` 的 `VertexFormat`。

### 8.54 去掉材质签名优化与场景解析缓存（2026-11-20 夜，按用户要求）

> 本节把 §8.53 搬来搬去的那个缓存**整个删掉了**：§8.53 的结论（缓存必须待在执行侧）没错，但用户决定不要这个优化了，见下。

用户的要求：**先把"防冗余绑定"这类优化去掉**，接下来要上压力测试（每帧大批量 drawcall 的帧数），性能优化等测出来再说。所以这一轮删掉的是两处**过早优化**：

| 删掉的东西 | 原来干什么 | 现在 |
|---|---|---|
| `MaterialSignatureSet`（`bindSignature` / `stablePropertySignature`）、`MaterialInstance::CalculateSignature`、`InvalidatePropertySignature`、`m_stablePropertySignatureVersion`、`OpenGLDriver::m_previousMaterialSignature`（§8.53 刚搬过来的那个）、`Material::Bind` 的返回值与 `p_previousMaterialSignature` 参数 | 每帧每材质算两次哈希，命中就跳过 `glUseProgram` 与参数块上传 | `Bind()` 每次 `program.Bind()`；`DrawMaterial` 每次 `Bind()` + `UploadProperties()`。**没有"已绑定"缓存了** |
| `MaterialInstance::UploadProperties(bool)` / `UploadParameterBlock(bool)` 的 `uploadStableProperties` 开关 | 调用方只有 `true`，开关其实是死代码（唯一受影响的是松散 uniform 的循环，永远不跳过） | 参数去掉 |
| `SceneRenderer::UpdateParsedDrawables` 的内容签名（`HashCombine/HashPointer/HashFloat/HashMatrix/HashSphere`）、`m_parsedDrawablesHash`、`m_parsedDrawablesValid`、`InvalidateParsedDrawables()` 及其两处调用 | 每帧对整个场景做一次全量哈希（变换、材质、网格、包围球），场景没变就跳过 `ParseScene` | **每帧都 `ParseScene`**。要让它便宜应该优化 `ParseScene` 本身，由压力测试的数字决定 |
| `MaterialInstance::Bind` / `Material::Bind` 里那两个 `emptyTexture*` 参数 | 只被 `CalculateSignature` 用来算哈希 | 参数一起删掉（`Bind(pass, featureSetOverride)`）；`UploadProperties` 仍收这两个纹理，那里是真正的 fallback |

**验证**（1254x480，窗口这轮又变回这个尺寸，旧基准可以用了）：

- 默认路径 obj 与 `base1254-obj.png` **0 差异**（去掉冗余跳过后画面不变，说明跳过本来就是幂等的）；
- 默认路径 3.moon 与 `base1254-1.png` **0 差异**（另一次运行差 2447 px = 已知悬停抖动，两次运行里一次命中基准、一次抖动，与 §8.47 记的同一个数）；
- 线程模式 obj 与默认只差导航方块图标 **1413 px**；`GL_INVALID_*` 全 0；
- 3.moon 两次默认运行里一次是基准、一次差 2447 px（同一现象），线程模式 exit 0。

**留给性能工作的话**：现在每 drawcall 的成本是"`glUseProgram` + 参数块 memcpy/上传 + 描述符绑定 + 绘制"，没有跳过的余地；`ParseScene` 每帧全量重跑。这两处是要在压力测试下重新量、再决定怎么优化的地方。
### 8.55 2D 叠加层命令化；线程模式的视口画面与默认**逐字节一致**（2026-11-20 夜）

上一轮把 Im3D 搬进命令流后还剩 1413 px（导航方块外圈的图标）。查下来那些图标走的是 `Im2DRender`（`ViewCubeWidget` 用 `Im2D::DrawList::AddImageQuad`），而 Im2D 那层还有两处录制侧 GL：

1. `UploadGeometry()` 在录制侧 `glBindBuffer/glBufferData` —— 线程模式下是空操作，**顶点/索引缓冲永远是空的**；
2. `SyncFontAtlases()` 在录制侧 `glGenTextures/glTexImage2D/glTexSubImage2D` —— 字体图集纹理不存在，文字采样到空白。

**改动**：

| 件 | 内容 |
|---|---|
| `Im2DRender::EndFrame` | 顶点/索引数据**随执行块一起打包**，`glBufferData` 搬到执行块里（在保存状态之后、`glUseProgram` 之前，避免把 overlay 自己的缓冲当成"要恢复的状态"）；状态保存/恢复、纹理解析、逐命令 `glDrawElements` 仍在那一个块里（§8.24 的临时出口，尚未拆成具名命令） |
| `SyncFontAtlases(driver)` | 图集纹理改由 `Resources::Texture` + `TextureUploadRequest` 创建（句柄命名、执行侧建对象），字体里的 texture id 用 `Im2D::TextureIdFromHandle(handle)`；末尾 drain，保证执行块解析句柄时对象已存在 |
| `ViewportOverlay` | 新增 `BeginOverlay2D` / `EndOverlay2D`（只开 2D 列表，不碰 ImGui）；`EndFrame` 内部复用它们 |
| `viewportframe.cpp` | 线程模式：`BeginGizmoFrame` + `BeginOverlay2D`，overlay 回调里调 `EndOverlay2D`；ImGui 仍然不开 |

**验证**（1254x480）：

- **线程模式 obj 与默认路径、与 `base1254-obj.png` 都是 0 差异**（整幅视口画面，含导航方块图标）；同一构建两次运行 0 差异；
- 默认路径 obj、3.moon 仍与基准 0 差异；`GL_INVALID_*` 全 0；
- 3.moon 线程模式：等 30 s 后两次运行 0 差异（12 s 时的 9% 差异是还在加载/布局，不是竞态）；与默认仍有 **20.4% 像素、mean delta 12** 的差别（实体明暗），但只有 28 个像素差 > 96 —— 这是 §8.53 起就记下的"3.moon 实体明暗略有差异"，与叠加层无关，留给后续查（最可能是 IBL/SSAO 的细节）。

**仍然开着的东西（更正）**：`ImGuiGLRenderer`（98 处直接 GL）属于**窗口那一半**——它由 `Moon/Qtimgui/imguiwidgets/ImGuiRenderer.h` 持有，在 Qt 的 paint 里跑窗口上下文，**不在模块的帧里，也不在渲染线程的任务范围内**，不需要命令化。

视口里唯一还用 ImGui 的是两个**只往 ImGui draw list 写、不发 GL** 的调试覆盖层：`DrawGbufferViews`（G-buffer 预览，`ImGui::Image`）与 `DrawFrameStats`（FPS/统计文字），默认都是关的（`DebugImgui` / `showFPS`），线程模式下因为 `overlayOff` 被一起跳过。要接上它们只需要开合 ImGui 的帧（纯 CPU）、不调 host 的 `Render()`；要确认的是 `ImGui::Image` 用的 G-buffer 纹理名在窗口上下文里是否有效（跨上下文共享）。

### 8.56 Im2D 的临时 lambda 出口拆成命令；Qt-imgui 移除（2026-11-20 夜，按用户要求）

**1) 最后那个 lambda 没了。** 新增两条命令，`Im2DRender` 现在全部走具名命令：

| 命令 | 做什么 |
|---|---|
| `DrawIndexedVertexArray` | 一条 draw：绑顶点数组、按列表的裁剪矩形 `glScissor`、绑纹理（前端的句柄或共享组里的名字，录制侧选好字段）、`glDrawElements` 画索引区间 |
| `SetProgramMatrix4` | 按名字给裸程序设 mat4（叠加层的投影）；引擎其余部分走材质参数块，这里只有一个程序和一块矩阵 |

配套改动：

- `Im2DRender::Init` 不再发 GL：程序走 `CreateProgram` 命令（源码随命令走）、顶点数组与两个缓冲走 `ResourceOps`（句柄 + `SetVertexArrayLayout`）、1x1 白纹理与字体图集走 `Resources::Texture` + `CreateTexture` + `UploadTexture`（**先创建再上传**——`UploadTexture` 要求对象已存在，这一步漏了会得到"黑字块 + 品红块"）；
- `EndFrame` 不再保存/恢复 GL 状态，也不再读 `glGetIntegerv`：状态是命令，驱动的缓存就是"GL 现在是什么"。为此 `OpenGLDriver::Capability` 的执行体顺手把 `m_appliedState` 的对应位一起改——否则叠加层留下的 `SCISSOR_TEST` 会让下一帧的 `PipelineState`（它对缓存做差集）漏掉关闭调用；
- 顶点/索引改成 `UploadBuffer`（`glBufferSubData`），容量按需翻倍（初始各 256 KB）。

**2) Qt-imgui 移除。** 删掉 `Moon/Qtimgui/`（QtImGui + ImGuiRenderer）、`Rendering/ImGui/ImGuiGLRenderer`、`Rendering/Overlay/IImGuiHost.h`，以及 `ImRenderer` 的 `SetImGuiHost/HasImGuiHost/newImgui/endImgui/CreatePlotContext`。视口里原本靠 ImGui 的两处：

- **G-buffer 预览**：删掉（`DrawGbufferViews`），要看请用帧调试器；
- **FPS/统计文字**：改成 `Im2DRender` 的文字（`AddText`，18 px，左上角），并挪进 `EndOverlay2D`——它本来就是 2D 列表的一部分，这样线程模式也能画（之前它在 `EndFrame` 里，线程模式走的是 `EndOverlay2D`，会漏掉）；
- 2D widget 的鼠标形状改成回调：`ScreenOverlayRegistry::SetCursorSink`，编辑器在 `ViewerWidget::initializeGL` 里装一个 Qt 的 sink（`setCursor`），叠加层不再认识 Qt/ImGui。

**验证**（1254x480，`showFPS` 临时置真跑过一次再改回）：

- 默认路径 obj、3.moon 与基准 **0 差异**；线程模式 obj 与基准 **0 差异**；
- 临时打开统计文字：文字正确渲染（黄字、左上角），默认与线程两种模式都画出来（两次运行之间只有数字本身不同）；
- `GL_INVALID_*` 全 0；`Qt-imgui`/`ImGuiGLRenderer` 在代码里已无引用。
### 8.57 按 Filament 的形状拆开 record / flush / execute（2026-11-20 深夜）

用户指出：Filament 两种模式都是**录制到环形缓冲**，差别只在 `engine->execute()` 在哪调；我们这边 `AView::Render` 里就把批执行掉了，等于"render 是同步的"。对照 `filament/libs/filamentapp/src/FilamentApp.cpp:267`（`if (!UTILS_HAS_THREADING) mEngine->execute();`）和 `Engine.cpp:1483`（驱动线程的 `loop()` 里调 `execute()`）。

改法：

| 角色 | 之前 | 现在 |
|---|---|---|
| `AView::Render` | 结尾 `ExecutePendingCommands()`（在渲染调用里执行） | 结尾只 `Flush()`：发布这一帧的批，**不执行** |
| `RenderModuleHost` | 没有 execute 入口 | 新增 `Execute()` —— 这个 host 就是 engine，对应 `Engine::execute()`；调一次/帧，在录制完成之后、粘贴之前（`ViewportFrame::Render` 里 `renderJob()` 之后） |
| `Driver` | 一个 `ExecutePendingCommands()` 兼两种语义 | `Execute()`：无渲染线程 → flush + 在调用线程执行（= `FEngine::execute` 在录制线程上）；有渲染线程 → flush + 等它跑完（调用者是帧的消费者，马上要粘贴）。`ExecutePendingCommands()` 保留为它的别名，继续给"要立刻读回结果"的路径用（`Link()` 取反射、framebuffer 完整性、拾取回读、构造期 setup） |

`m_executing` 那条直通分支保留：那是**执行器内部**回调驱动（叠加层的执行块、`ResourceOps` 里的帮助函数）的情况，Filament 对应的是执行器直接调 `DriverApi`；不是"单线程模式就直接执行"。

**验证**（窗口这轮变成 1160x487，只能用同尺寸的 default vs thread 比）：

- 默认路径 obj 两次运行 **0 差异**（说明 execute 点的搬动没有引入不确定性）；
- 默认 vs 线程 obj **0 差异**（整幅画面）；
- `GL_INVALID_*` 全 0；
- 3.moon 线程模式与默认仍是那 22.9%（mean 14）的实体明暗差别——§8.55 记的既有问题，与本次改动无关。

剩余 `ExecutePendingCommands()` 调用点里，构造期的 setup 和探针读回是有意的同步点；资源创建路径（TextureLoader/GLTexture/GLShaderProgram/ResourceOps/Im2D/Im3D）同理，将来可以换成 Filament 的 fence。
### 8.58 默认改成多线程模式（2026-11-20 深夜，按用户要求）

`RenderModuleHost::ThreadedMode()` 反过来：**默认就是渲染线程**（Filament 的默认配置），`MOON_RENDER_THREAD=0` 才是单线程（旧的基准图是在那个配置下截的，所以留着）。窗口选共享上下文的那处（`ViewerWidget::CreateRenderModule`）也从读环境变量改成问 `ThreadedMode()`，两条路径同一个判据。

**验证**（1160x487，窗口又变了）：

- obj：默认（线程）与 `MOON_RENDER_THREAD=0`（单线程）**0 差异**；
- 3.moon：线程模式两次运行差 2469 px = 已知悬停抖动；与单线程差 22.4%（mean 13.6、max 127、只有 27 px 差 > 96）——§8.55 记的实体明暗差别，与本次改动无关；
- `GL_INVALID_*` 四种跑法全 0，四个进程 exit 0。

**默认切过去之后，下面这些在默认配置下是"跳过"状态**（之前只在 opt-in 时被跳过，现在是常态）：

| 位置 | 行为 |
|---|---|
| `PickingRenderPass` ×3 | 拾取的读回/调试绘制不发；**拾取在默认配置下不工作** |
| `HzbBuildPass` | HZB 构建被跳过 → HZB 遮挡剔除不生效 |
| `PointRenderPass` / `BvhService` | 录制侧直接 GL，线程模式下等于没做 |
| `Im3DGLRenderer` / `AView` 里的若干 `IsRenderThreadActive()` 分支 | 是"录制侧不再直接发 GL"的守卫（正常），不是功能缺失 |

下一步顺序建议：把拾取读回做成命令（PBO + fence）→ HZB 构建命令化 → 这两个恢复后，默认配置才算功能完整；之后才是压力测试。
### 8.59 拾取命令化完成；录制侧纹理的重复分配修掉（2026-11-21 凌晨）

默认切到渲染线程后，`PickingRenderPass` 的三处 `IsRenderThreadActive()` 守卫是"拾取不工作"的原因。三处都处理了：

| 位置 | 之前 | 现在 |
|---|---|---|
| 世界坐标附件（第二个颜色附件，RGBA32F） | 线程模式下整个跳过 → `hasWorldPosition` 永远为假 | `HAL::Texture` 的创建/分配/`Attach`/`SetDrawBuffers` 都是命令，和这个目标的其它附件一样；帧带里确认 `ActorPicking` 状态 **0x8cd5（complete）** |
| gizmo 的拾取几何（`drawMeshPick`） | 跳过（注释说 ImRenderer 还在直接发 GL） | 直接调：它的材质绑定（`BindProgram`/描述符）与绘制（`DrawVertexArray`）早就是命令了 |
| 拾取回读（PBO 环 + fence + `MapRead`） | 跳过 | 两条 `ReadPixels` 命令（id 附件 + 世界坐标附件），`onDone` 在拥有上下文的线程上把计数 +1，`TryConsumePick` 下一帧读 CPU 副本；打包带里没有 GL、没有 fence、不用等 GPU |

顺带修了一个**线程模式才暴露**的 bug：`Allocate` 对同一个纹理执行第二次会得到 `GL_INVALID_OPERATION: Texture is immutable`（存储是不可变的 `glTextureStorage2D`）。

- `UploadTexture` 的执行端只在 `!IsValid()` 时分配存储，之后只做子图上传（字体图集随后 bake 新字形就是这个路径）；
- `CreateTexture` 的执行端同样只在未分配时分配，已分配且非 mutable 的直接跳过（mutable 的仍走 `glTexImage2D` 原地重建，BVH 的逐三角缓冲是它的用户）；
- 之前线程模式启动时那 5–8 条 `Texture is immutable` 现在 **0 条**。

**验证**（1160x487）：

- obj：默认（线程）vs `MOON_RENDER_THREAD=0`（单线程）**0 差异**，两次运行 0 差异；
- 带 `MOON_FRAME_TRACE` 的线程模式跑：`[pass] Picking` 出现，`[fbo] bind 37 (ActorPicking) status=0x8cd5`（完整）；
- 鼠标移到模型上（`SetCursorPos` + 伪点击）线程模式跑：`GL_INVALID` **0**、`immutable` **0**、exit 0；
- 3.moon 单线程 exit 0、`GL_INVALID` 0。

**还差**：`HzbBuildPass` 仍是线程模式下 `SetEnabled(false)`（`AView::Render` 两处 + `PreviewView`），它有自己的 PBO 环 + mip 链；按 §8.59 的办法可以照做（目标已经全是命令，剩下的是那个回读）。拾取还有一处可以更接近 Filament：现在的读回是 `glReadPixels` 同步读（1×1，可接受），要零停顿就换成 PBO + fence 的异步版本，但那需要一条"读回完成"的命令。
### 8.60 3.moon 线程模式偏暗的定位进展（2026-11-21 凌晨）

现象（用户截图指出）：默认（渲染线程）模式下 3.moon 的实体明显偏暗、带蓝/橄榄色块，单线程模式正常。逐 pass 探针（`MOON_PASS_PROBE`）把范围定在**不透明 pass**：

| pass（中心像素） | 单线程 | 线程 |
|---|---|---|
| SkyboxRenderPass / Gbuffer | 200 208 222 | 200 208 222 |
| **Opaques** | **98 110 130** | **35 42 54** |
| Post-Process（色调映射后） | 157 166 180 | 88 99 114 |
| Lines / ImRenderer / Picking | 157 166 180 | 88 99 114 |

即：**Gbuffer 之后分叉**，后面所有 pass 只是继承。原因在"不透明 pass 的着色输入"这一层（直射光/阴影/IBL/材质块）。

这一轮排除/修正的东西：

1. **`MOON_PASS_PROBE` 之前只在渲染线程模式跑**（`CompositeRenderer` 里有个 `IsRenderThreadActive()` 条件），已去掉——不然没法两种模式对照。
2. **`[dm] T` 的 `tex=0` 是探针 bug，不是绑定 bug**：它对每个 unit 读的是 `glGetIntegeri_v(GL_TEXTURE_BINDING_2D, ...)`，立方体贴图（`_IrradianceCube`/`_PrefilterCube`）和纹理缓冲（`domainColorTex`）永远读成 0。真正的证据是新加的 `[env]` 行：线程模式下 `irr`/`pre` 指针有效、名字 69/70、每帧都在 `SetEnvironmentTextures` 里绑定 ✓ IBL 绑上了。
3. **`ShadowRenderPass` 的阴影目标绑定**：`light.shadowBuffer->Bind()` 是直接 HAL 调用，线程模式下是空操作（阴影贴图不会被渲染）。已改成 `m_renderer.BindFramebuffer(...)` / `BindDefaultFramebuffer()`，出口目标同理。**但这没有改变像素**（改前改后 3.moon 的差异都是 129104 px / mean 14.35），所以阴影不是这次偏暗的主因（线程模式下阴影贴图此前可能已经被别处渲染对了）。

**下一步（按顺序）**：比较两种模式的 `[dm] U EngineUBO`（光照/相机矩阵）与每个材质的参数块内容；再把 SSAO 关掉对照一次，确定是直射光项还是环境项；`[dm] T` 探针要改成按 sampler 类型查询绑定（2D / cube / buffer 三种），否则会继续误导读数。
### 8.61 3.moon 偏暗：修掉两个真 bug，剩下的差异是**非确定性**（2026-11-21 上午）

§8.60 之后继续挖，这一轮修掉两处真实的线程模式 bug，并把剩下的现象定性为**非确定性**（不是缺功能）。

**修掉的两处**

| 位置 | 问题 |
|---|---|
| `Im3DType.cpp` 的 `EnsureTextureBuffer` | 纹理缓冲（草图的边值 `edgeTexture`、块色 `blockTexture`）的记录请求只带了 `descriptor`，而里面的 `mutableDesc.data` 指向调用方**局部 vector**（`submit()` 里的 `edgeValue`）——线程模式下命令晚执行，读到的是已释放内存 → 块色/边值变成垃圾（用户截图里那块蓝/橄榄色块就是这个）。现在把字节 copy 进请求（`request.pixels`），执行端再把描述符指向命令自己的副本 |
| `ShadowRenderPass` | `light.shadowBuffer->Bind()` / `output.value().Bind()` 是直接 HAL 调用，线程模式下是空操作（阴影目标不会被绑定）。改成 `m_renderer.BindFramebuffer(...)` / `BindDefaultFramebuffer()` |

**诊断工具的两处修正**

1. `[dm] T` 探针以前对每个 unit 只查 `GL_TEXTURE_BINDING_2D`，立方体贴图（`_IrradianceCube`/`_PrefilterCube`）和纹理缓冲（`domainColorTex`）**永远读成 0** —— §8.60 里那条"IBL 没绑上"的结论是假的。现在按 sampler 类型分别查 2D / cube / buffer / 2D-array。
2. `[dm] U` 增加了 `MaterialParams`，并把 `EngineUBO` 打成 16 行（光照在相机矩阵之后）。

**排除掉的假设**

| 假设 | 验证 | 结论 |
|---|---|---|
| 阴影贴图没渲染 | `castShadows=false` 跑两种模式 | 差异不变（126846 px）→ 不是阴影 |
| 天空 capture 时机 | `MOON_SKY_RECAPTURE=1` 每帧重 capture | 差异基本不变（vs 单线程仍 34）→ 不是 IBL 时序 |
| 描述符/参数块没上传 | `[dm] U EngineUBO` / `MaterialParams` 行、`[dm] T` 采样器 id、`[dm] S` 状态与视口 | 两种模式**完全一致** |

**剩下的现象：非确定性**

同一构建、同为渲染线程模式，连跑两次 3.moon：**两次运行之间差 22.9%（mean 22.2、12% 像素差 > 96）**。一次是"接近单线程"的样子（与单线程差 mean 13.6、只有 27 px 差 > 96），另一次是"实体发黑"（与单线程差 mean 30+）。也就是说线程模式下草图实体在**两种状态之间跳**，所以之前"有时暗、有时黑"。

因为可观测的 GL 输入都一致，剩下的怀疑方向是**录制侧的时序**：

1. 草图的块色/边值缓冲（`PolygonMesh::submit/bind` 的 `isDirty`/`blockColorDirty`）——标记在录制侧清、命令在执行侧跑，两者错位时某一帧会画到上一版数据；
2. 编辑器侧的几何重建（JobSystem 30 线程）与帧录制重叠时，绘制列表可能被读到"重建到一半"的状态；渲染线程只是改变了帧节奏，把这个既有竞态暴露出来；
3. 需要在两种模式下对比同一帧的 `blockTexture`/`edgeTexture` 内容（`glGetTextureImage` 走命令读回），才能定位是 1 还是 2。

新开关：`MOON_SKY_RECAPTURE=1`（每帧重做天空 capture，bisect 用）。
#### 8.61.1 两种状态的进一步刻画（2026-11-21 上午）

按时间梯度跑同一构建的渲染线程模式（3.moon）：

| 运行 | 结果 |
|---|---|
| 12 s 与 20 s（两次运行） | **0 差异**（状态 A） |
| 20 s 与 30 s | 57.7% 像素不同（mean 49.5）→ 状态 B |
| 30 s 与 40 s | **0 差异**（状态 B 稳定） |
| 40 s 与单线程 | 22.5%、mean 13.6、只有 27 px 差 > 96 |

看图像才明白：**两种状态都不是"少画了实体"**——

- 状态 A（12/20 s）：实体在，偏暗；
- 状态 B（30/40 s）：实体在，但**过曝发白**（只剩边线和控制点清晰）。

单线程两次运行一直是同一个状态且与基准一致。也就是说线程模式下**草图实体的着色输入在两个稳定值之间二选一**（暗 / 过曝），几何与叠加层都没问题。这与"块色/边值纹理缓冲、以及 `domainColorTex`"这一组输入吻合：它们的 CPU 侧数据由 `PolygonMesh::submit/bind` 每帧按 `isDirty`/`blockColorDirty` 重建，而命令由执行侧在那之后执行，两者一旦错位就会让某次上传带上另一版（或未初始化）的数据。

下一步：在 `PolygonMesh::submit()/bind()` 里对 `blockColor`/`edgeValue` 的**首若干元素**打点（`MOON_FRAME_TRACE`），跑两次线程模式对比；同时用命令读回 `blockTexture` 的实际内容，确认是"上传的数据不对"还是"绑定的纹理不对"。
#### 8.61.2 录制侧数据是稳定的（2026-11-21 上午）

给 `PolygonMesh::submit()`/`bind()` 加了两条 `MOON_FRAME_TRACE` 打点（`[im3d] submit faces= verts= edgeCount= edgeSum=` / `[im3d] blockColors count= nextBlockId= sum= first=`），跑两次渲染线程模式对比：

```
q-18: submit faces=928 verts=2784 edgeCount=928 edgeSum=0 / faces=4087 verts=12270 edgeCount=4090
q-28: submit faces=928 verts=2784 edgeCount=928 edgeSum=0 / faces=4087 verts=12270 edgeCount=4090
q-18: blockColors count=4 nextBlockId=4 sum=2295 first=0.000,0.000,1.000
q-28: blockColors count=4 nextBlockId=4 sum=2295 first=0.000,0.000,1.000
```

**CPU 侧完全一致**（面数、顶点数、边值总和、块色数量/和/首色）。但两次的图像仍然差 17.3%（mean 11.6），且都与单线程差 ~22.9%。所以问题不在"录制侧算出了不同的数据"，而在 **GPU 侧的对象/顺序**：同一个纹理缓冲对象被 `glTexBuffer` 重新指向的时机、或者绘制采样到的是哪一版内容。

下一步要读回 GPU 内容：`blockTexture`/`domainColorTex` 是 buffer texture，用 `glGetTextureImage`（走命令）在帧末读前 64 字节，与 CPU 侧的期望值对拍；同时把每次 `EnsureTextureBuffer` 的 `CreateTexture` 命令序号和绘制命令序号打出来，确认"重新指向"发生在绘制之前。
### 8.62 3.moon 的非确定性找到了：`DrawMaterialRequest` 指向栈上的 drawable（2026-11-21 中午）

**根因**：`ABaseRenderer::DrawEntity(pso, const Drawable&)` 把 `&p_drawable` 记进 `DrawMaterialRequest`。而调用方（各 pass 的循环）写的是

```cpp
for (auto drawable : drawables.opaque | std::views::values)   // 按值复制
    m_renderer.DrawEntity(p_pso, drawable);
```

所以 `&p_drawable` 指向**循环体的栈副本**——命令执行时（有渲染线程时是整整一帧之后）那个对象早没了，材质、feature 覆盖、pass 名都是从栈上"碰巧还在"的字节里读出来的。单线程模式能跑只是因为执行发生在同一帧末、栈还没被覆盖；渲染线程下栈变化无常，于是实体时而变黑、时而发白、时而块色乱掉。

**修法**：请求自己持有副本——

```cpp
auto ownedDrawable = std::make_shared<::Rendering::Entities::Drawable>(p_drawable);
materialRequest.drawable = ownedDrawable.get();
materialRequest.ownedDrawable = ownedDrawable;   // 新增字段：让命令活着的时候对象也活着
```

`Drawable` 是轻值（材质是 `OptRef<MaterialInstance>`），一次 shared_ptr 分配的代价。

**验证**（1160x487）：

- 渲染线程模式连跑三次：两次 **0 差异**、一次 2469 px（= 已知悬停抖动）→ **不再有状态跳变**；
- 默认（线程）vs 单线程 obj：**0 差异**；
- 3.moon 线程 vs 单线程仍剩 **22.45%、mean 13.6**（下面这条）。

**剩下的系统性差异（下一步）**：现在它稳定可复现（不是跳变），表现为实体整体约 0.6 倍偏暗。已经排除：阴影（`castShadows=false` 差异不变）、块色（`MOON_NO_BLOCK_COLOR` 差异不变，数值甚至一模一样 126846 px / mean 13.63）、IBL 时序（每帧重 capture 无变化）、EngineUBO/MaterialParams/采样器/状态（逐字段一致）。

剩下的怀疑：**执行侧读的是共享的、逐 draw 被改写的材质状态**。`mCellMaterial` 是所有面共用一个实例，每个面 `SetProperty` 后 `UploadProperties`（参数块字节是随命令 copy 的 ✓），但**描述符里的纹理解析和 `MaterialInstance` 的其他 CPU 字段是在执行时读的**——执行上一帧的 draw 时，录制侧已经在为下一帧改同一个实例。Filament 的形状是"每次 draw 把材质参数快照进命令、绑定只碰 GPU 对象"，这条要与压力测试一起做（快照的代价需要量）。
### 8.63 3.moon 的实体被"草图面"覆盖（2026-11-21 下午）

继续查（§8.62 之后），这一轮做的与看到的现象：

**改的**：

1. `TransparentRenderPass`（深度剥离）里所有直接 HAL 调用都改成驱动命令：`mLayerFbo[i].Bind()/Unbind()`、`mBlendFbo.Bind()`、`msaaframebuffer.Bind()`、两处 `FramebufferUtil::CopyFramebufferColor/Depth`；并补了 `Driver::CopyFramebufferDepth` 的包装（命令 `CopyFramebufferDepth` 本来就有）。
2. 诊断：`MOON_DRAW_TRACE_FRAME=<n>` 打开"整帧所有 draw 都打印"（原来只打印索引数 > 1000 的，草图的几千个小面根本不在里面——之前一直没看到它们的真实状态）；`EngineUBO` 增加了整块哈希。

**看到的（这是关键）**：现在渲染线程模式下，圆盘实体是**深绿色**（`blockColor` 里的绿），单线程模式是灰色（STEP 材质）。对照 `[dm] T`：

- 两种模式的 `EngineUBO` 整块哈希**完全相同**（`19dbc3365ea56383`）；
- 第一个 `Opaques` draw 的 `S/U/T/A/E` 两种模式**逐行相同**（`prog=16`、`MaterialParams row4=0.000,0.400,0.500`、`u_AlbedoMap tex=14`）；
- 但 `_IrradianceCube` 在**线程模式整个 run 里一次都没出现**，单线程模式出现 4 次（同一个 PBR 变体在线程模式下编译成了不带 IBL 包装的版本）。

所以实体不是"变暗"，而是**被草图的面层（带 block color 的 `mCellMaterial`）盖住了**——绿/蓝/橄榄就是 `blockColor` 的内容。为什么单线程盖不住、线程模式盖住了：两层几何在同一位置，先画的是 STEP 实体、后画的是草图面，深度/混合应当让草图面被挡住或按 alpha 混合；线程模式下这两层的**绘制顺序或深度状态**与单线程不同（`Opaques` 的 `S` 行相同，但**两个 pass 之间的顺序**受 pass 里 `DrawEntity` 的录制顺序影响，而录制顺序又受 `m_parsedDrawables`/过滤结果影响）。

下一步：把 `Opaques` 里"每个 draw 的 program + elementBuffer + 该 draw 的 depthFunc"按顺序导出，两种模式逐项对比，找出草图面与实体面的**相对顺序**差异；再确认 STEP 实体是否被写进了 `Transparents`（若被当透明物体，剥离路径的行为在大改之后可能仍然不对）。
### 8.64 hezi.step：两个真 bug 修掉后，只剩整体偏暗（2026-11-21 下午）

用户的复现文件是 `C:\Users\27181\Desktop\ImGui-D3D11Hook-master\hezi.step`（一个盒子）。渲染线程模式下：**整个实体约 0.58~0.6 倍均匀偏暗，部分面颜色错乱**。

**修的两处**：

1. `GLTexture::Allocate` 的记录分支只带 `descriptor`，而 `descriptor.mutableDesc.data` 指向**调用方的局部内存**（`CTopoShape.cpp:401` 的 `std::vector<Maths::FVector4> domainColor` 就是），命令晚一帧执行时那内存已被改写 → 面颜色取到别的面的值。现在把 `buffetLen` 字节 copy 进 `request.pixels`，执行端把描述符指过去（Filament 的 `BufferDescriptor` 同样持有自己的副本）。
2. `GLShaderProgram::Bind` 记录的是 `BindProgram(Records::ProgramName(handle))`——渲染线程模式下程序名要等 create 命令执行后才有，这里永远是 0，等于**记录"不绑定"**，那一批 draw 全用上一个材质的程序。证据：同一个 `Opaques` pass，单线程用 8 个不同 program，线程模式只用 1 个。改为新命令 `BindProgramHandle(handleId)`，由执行侧在执行时解析名字。

**效果**（hezi，1160x487，单线程 vs 渲染线程，per-channel 差 > 96 的像素数 / max delta / mean）：

| | > 96 | max | mean |
|---|---|---|---|
| 修前 | 11 860 | 255 | 11.14 |
| + 纹理字节 copy | 1 127 | 255 | 9.00 |
| + 程序按 handle 绑定 | **340** | **105** | **8.69** |

色块错乱消失，边线/控制点/轮廓一致；obj 场景仍然 0 差异。

**剩下的**：实体整体 ~0.58 倍偏暗（只剩亮度差，没有大跳变）。已排除：阴影（本来就没有）、SSAO（`MOON_NO_SSAO` 后数值一模一样）、IBL 开关（`MOON_NO_IBL` 不收敛）、每帧重抓天空（更差，mean 29）。逐字段对拍过且相同：`EngineUBO` 整块哈希、`MaterialParams`、采样器与贴图内容、顶点/索引数据、管线状态、视口、program 的全部 uniform 值（这个程序除采样器外没有 loose uniform）。材质 variant 的 feature set 在整个 run 里都是空集，两边一致。

**下一步（不再猜）**：把每个 pass 的输出**逐 pass 存成 PNG**，两种模式对拍，找出**第一个开始不同的 pass**。工具齐了：`CopyFramebufferColor` 命令 + `[pass]` 标签 + 已有的 `MOON_PASS_PROBE` 单点探针，扩成整帧读回即可。
### 8.65 hezi：逐 pass 对拍把范围缩到 Opaques 一处（2026-11-21 下午）

加了两个工具：

- `MOON_PASS_DUMP_DIR=<dir>`：每隔 120 帧把**每个 pass 之后的整张帧目标**写成 `<dir>/<pass>.raw`（RGBA8）。两种模式各存一份，逐 pass 对比就能找出"从哪个 pass 开始不一样"。
- `MOON_SKIP_PASSES=A,B,C`：把指定 pass 从帧里去掉（比一个 pass 一个开关好用）。

**逐 pass 对比结果**（hezi，pass 顺序 Skybox → Gbuffer → Opaques → PointDraw → Grid → SectionCap → SectionContour → Transparents → HZB → Post-Process → Lines → ImRenderer → Picking）：

```
Gbuffer / SkyboxRenderPass   0 像素差        ← 这两步之后帧目标里没有任何不同
Opaques                87360 px 不同 (15.46%)  ← 差异从这里出现
PointDraw/Grid/Section*/Transparents/HZB       87360
Lines/ImRenderer       82026                  ← 被叠加层抹平了一部分
Post-Process           88243
```

再用 `MOON_SKIP_PASSES` 收窄：

| 跳过 | 结果（126846 像素 / 11860 / 11.14 是修 bug 前的基准） |
|---|---|
| 全部叠加层（Lines,ImRenderer,Grid,PointDraw,Section*,Picking,HZB） | 15.62%、mean 9.65、max 105 → 差异**全在核心 pass** |
| 只跳 Transparents | 与"全跳"**数值完全相同**（14.69%、mean 9.01）→ 透明剥离层对本模型无贡献 |
| 只跳 Gbuffer | 同上，数值也完全相同 → G-buffer/SSAO 对最终颜色无贡献 |

剩下的怀疑面已经很窄了，并且又排除/确认了几项：

- `S` 行补了**混合状态**（`blend=` 后的 enable/src/dst/srcA/dstA）：两种模式第一个 Opaques draw 都是 `blend=0/0x0302/0x03`；
- `CTopoShape` 的 `domainColor`（69 个面颜色）CPU 侧哈希两种模式相同（`5215dbe43c11e3a3`）；
- `MOON_UNIFORMS` 现在会 dump 每条 draw 的**全部 uniform 值**（这个程序除采样器外没有 loose uniform，两边相同）。

**唯一还没对齐的现象**：同一个 `Opaques` pass，单线程用 **8 个不同 program**（主体 prog 27 × 2339，另有 26/28/33/34/36/37/38 各 2~6 次），渲染线程模式只用 **1 个**（prog 26 × 2365）。说明线程模式下有一小批 draw 的"绑定程序"没生效，或者它们请求的 variant 在录制时还不存在（创建命令排在 draw 之后）。下一步就是给每次 program 创建（link）打点，与物理名/材质名一起对比两边的创建顺序与数量。
#### 8.65.1 再排除两项，并把"变体数量不同"解释清楚（2026-11-21 下午）

- `[filter]` 打点（`FilterDrawables` 末尾）显示两种模式的划分**完全相同**：`opaque=1 transparent=0 lines=2 ui=0`。所以实体并没有在线程模式下被划到别的通道。
- `[variant]` 打点（`ShaderLoader::CompileVariant` 入口，打印材质名/pass/阶段/源码长度/源码哈希/feature）显示单线程编译 63 个变体、线程模式 48 个。差集是 `HzbReduce`（线程模式 HZB 关掉，符合预期）、`Atmosphere/Standard/FilamentPBR/Skysphere/Gizmo`（该 run 里没用到）、以及 `GeomertySurface|Transparents|*`——后者是**预编译**造成的（`ShaderLoader.cpp:961` 有全变体预编译循环），不是绘制差异。

所以"变体数量不同"这条线索排除。
### 8.66 事故与恢复：OpenGLDriver.cpp 被写坏后重建（2026-11-21 下午）

**事故**：清理诊断代码时用按行拼接删除代码块，两次算错范围，把 `MoonRender/src/Rendering/Backend/OpenGLDriver.cpp` 写坏（文件里只剩"文件头 + 前半段 + 又一遍文件头 + 截断的副本"）。该文件在本仓库**未跟踪**（`MoonRender` 整体未提交），git、编辑器本地历史、用户目录备份都没有副本。

**恢复**：从残留内容里救出 42 个执行器（`Execute`/`CreateProgram`/`BindProgramHandle`/`CreateTexture`/`DrawVertexArray`/`SetProgramMatrix4`/`UploadOverlayVertices` 等），并按 `OpenGLDriver.h` 的声明补写了 5 处：

- `DrawMesh` 尾部（`[dm] A/E` 打印 + `glDrawElementsInstanced`）；
- `PipelineState`：**改为全量应用**（不再与 `m_appliedState` 做 diff）。这是与原先行为唯一有意不同的地方——没有缓存就不会和真实 GL 状态失配，重建版当时正是靠这一条才让两模式重新一致；代价是每 draw 多几次 `glEnable`/`glDepthFunc` 之类的调用，属于后面压力测试阶段要加回的优化，不是正确性问题；
- `Clear`、`SetClearColor`：直接转 `m_backend`；
- `DrawMaterial`：`material.Bind(drawable.pass)` + `UploadProperties(empty2D, emptyCube, flatNormal)` + `perRenderableSet->BindFixed(kPerRenderableDescriptorSet)`。

**两个已验证的修复保留在重建版里**：`GLTexture::Allocate`/`CreateTexture` 的"描述符字节随命令复制"、`BindProgramHandle`（程序按 handle 由执行侧解析名字）。

**验证**：

| 对比 | 结果 |
|---|---|
| obj：重建版（线程）vs 事故前截图 `aa-obj-thread.png` | **0 像素差** |
| obj：单线程 vs 渲染线程 | **0 像素差**，`GL_INVALID` 0 |
| hezi：单线程 事故前 vs 现在 | 1.33%（mean 1.25），差异在边线附近 |
| hezi：单线程 vs 渲染线程 | 15.24%、mean 9.18（事故前 14.52%、8.69） |
| 3.moon：单线程 事故前 vs 现在 | 2.11%（mean 2.20） |
| 3.moon：单线程 vs 渲染线程 | 23.26%、mean 14.19（事故前 22.85%、14.33）——现象与事故前一致 |

残留备份在 `Build/refactor-shots/salvage/`（`OpenGLDriver.damaged.cpp`、`partA_1_1064.cpp`、`partB_1065_end.cpp`、`OpenGLDriver.reconstructed.cpp`）。`DrawMaterial` 和 `PipelineState` 是重写的两处，review 时优先看它们。
#### 8.66.1 恢复完成：单线程输出与事故前**逐字节一致**（2026-11-21 下午）

补上最后一块：重建版里 `PipelineState` 只设了 `POLYGON_OFFSET_FILL` 能力，没有设**偏移值**，于是 CAD 的边线与面在同一深度上打架、边线被面盖掉（差异像素在参考图里是纯黑，现在变成了面的灰）。把偏移补回去，且**方向是负的**——引擎是反向 Z（`GL_GREATER`，越近值越大），"推远"要减：

```cpp
if (p_state.polygonOffsetFill) { m_backend.PolygonOffset(-1.0f, -1.0f); }
m_backend.SetCapability(ERenderingCapability::POLYGON_OFFSET_FILL, p_state.polygonOffsetFill);
```

**验证（全部对比事故前的截图，1160x487）**：

| 对比 | 结果 |
|---|---|
| hezi 单线程 vs 事故前 | **0 像素差** |
| 3.moon 单线程 vs 事故前 | **0 像素差**（1 个像素差 1） |
| obj 单线程 vs 事故前 | **0 像素差** |
| obj 单线程 vs 渲染线程 | **0 像素差** |
| hezi 单线程 vs 渲染线程 | 82027 px / 14.520%、mean 8.6854、340 px > 96（事故前：**完全相同**） |
| 3.moon 单线程 vs 渲染线程 | 126846 px / 22.454%、mean 13.6318、27 px > 96（事故前：**完全相同**） |
| `GL_INVALID`（hezi/3.moon/obj 线程模式） | 0 / 0 / 0 |

也就是说：重建的 `OpenGLDriver.cpp` 在行为上与事故前**完全等价**，包括 `PipelineState` 的状态应用范围（全量应用 vs 原本的 diff 只在性能上有差别，输出一致）。

顺带解释了之前 §8.64/§8.65 里没弄清的一件事：**"整体偏暗"的差异像素里那 340 px（hezi）/27 px（3.moon）的高对比差，与多边形偏移无关**——恢复偏移后这两个数字与事故前完全一致，所以偏暗问题保持原样，没有被我这次重建掩盖或改变。
### 8.67 找到了：灯光 SSBO 的绑定在录制侧执行，执行侧根本没绑上（2026-11-21 下午）

**根因**：`LightingRenderFeature::OnBeginFrame` 每帧把灯光矩阵写进 `m_lightBuffer`（一个 Shader Storage Buffer），然后 `Bind()` → `GLBuffer::Bind(0)` → **`glBindBufferBase(...)` 直接调用**。渲染线程模式下这一行是在**录制线程**上执行的，执行侧（真正跑 draw 的那个线程/上下文）上这个 binding point **从未被绑定**。着色器里 `ssbo_Lights.length()` 因此读到 0（或上一帧的残留），**直接光全部消失**——所以实体整体变暗，而反射/IBL/天空这些不依赖 SSBO 的部分看起来正常。

这解释了之前所有"对拍下来一模一样却画得不一样"的现象：`EngineUBO`（哈希相同）、材质块、采样器、贴图、顶点/索引、program uniform 全部一致，因为差异根本不在这些地方，而在**一个没有被绑定的 buffer 绑定点上**。

**修法**：把缓冲区的绑定也变成命令。

- `BufferRequests.h` 新增 `BindBufferRequest { handleId, type, indexed, index }`；
- `DriverAPI.inc` 新增 `BindBuffer` 命令，执行器在 `glBindBufferBase`/`glBindBuffer` 之前用 `Records::BufferName(handle)` 解析名字（和纹理/程序一样的模式），`handleId == 0` 即解绑；
- `GLBuffer::Bind` 在 `IsRenderThreadActive()` 时记录该命令，否则照旧直接调用。

**验证（全部 1160x487，单线程 vs 渲染线程）**：

| 场景 | 修前 | 修后 |
|---|---|---|
| hezi | 82027 px / 14.52%、mean 8.69、340 px > 96 | **0 像素差** |
| 3.moon | 126846 px / 22.45%、mean 13.63、27 px > 96 | **0 像素差**（1 个像素差 1） |
| obj | 0 像素差 | 0 像素差 |
| hezi 渲染线程连跑两次 | — | **0 像素差** |

顺带一个同类的坑（同一个函数族）：`GLBuffer::BindRange` 仍直接调 `glBindBufferRange`，目前没有调用点需要它走命令；如果以后有，照 `Bind` 的方式补一个命令即可。

另外用调试着色器逐项排除过的（这些都是**相等**的，记录在此以免以后再查）：`linearAlbedo`、世界法线、`_IrradianceCube` 采样、`_BRDFLut` 采样、`ambientOcclusion/metallic/roughness`、`EngineUBO` 整块哈希、材质块、采样器绑定与贴图内容、顶点/索引数据。把 `evaluateIBL` 关掉（只留直接光）时差异反而变大，正是"直接光=0"的直接证据。
### 8.68 相机不跟手的根因：呈现等的是"命令已提交"，不是"这一帧画完"（2026-11-22）

**现象**：只有渲染线程模式下，右键拖拽旋转时相机恒定滞后（单线程正常）；帧率正常、CPU 侧相机与鼠标逐帧同步（`[cam]` 逐帧打点验证过：相机在鼠标移动的同一帧更新，0 帧延迟，松手后也不追赶）。

**根因**：同步语义错了。

```text
主线程：录制 frame N → Execute() → Flush() + WaitForIdle()   ← 等的是 batch 被"执行"
渲染线程：执行 batch N → ReleaseBuffers()
主线程：贴图 m_presentTarget
```

`WaitForIdle()` 等的是 `m_outstanding == 0`，也就是**渲染线程把命令提交完并归还环形缓冲**——此时 GPU 还在画这一帧。主线程立刻去贴图，跨 context 读一个还在被写的纹理：驱动内部会做隐式同步，但"贴到屏幕上的内容"和"主线程以为的那一帧"对不上，表现为画面恒定落后一拍。

**修法**（Filament swapchain 的 frame fence 形状）：

1. `OpenGLDriver::WaitForFrameCompletion()`：`glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE)` +
   循环 `glClientWaitSync(..., 20 ms)`，直到信号；
2. `Driver::WaitAndExecute()` 在 `ReleaseBuffers()` **之前**调用它。

于是 `WaitForIdle()` 的含义变成"这一帧已经画好了"，主线程呈现的必然是渲染线程完成的那一帧，不跳帧、不错帧、不贴半成品。

代价：主线程真的等这一帧的 GPU 时间（单缓冲呈现的固有代价）；要拿回重叠，需要 present 目标双缓冲 + fence（UI 不阻塞），这是后续项。

**验证**：两种模式均正常出图、`GL_INVALID` 0；用户实测线程模式下拖拽旋转已跟手。

### 8.69 present 改成 3 槽交换链，呈现不再等"正在画的这一帧"（2026-11-22）

**背景**：§8.68 的 fence 修好了"相机不跟手"，但呈现仍是"渲染线程画完这一帧 → 主线程贴这一帧"，
主线程必然等当前帧的 GPU 时间，等于单缓冲。

**做法**（照 Qt/Filament demo 的 `PlatformGlfwGL`：`RING_COUNT = 3`、`waitFrameCompleted`、
`getTextureId(slot % 3)` 与 `paintGL` 的次序）：

- 第 F 帧画到 `F % 3` 那一槽（`PresentTarget`）；
- 窗口贴 `(F - kPresentLatencyFrames) % 3` 那一槽（`PresentedTarget`），贴前
  `WaitForPresentedFrame()` 只等**上一帧**已画完，不再等正在画的当前帧；
- 每帧把三个槽都 resize：之前只 resize 写入槽，窗口 resize 后读取槽还是旧尺寸，贴图把一个
  旧尺寸的帧拉伸上去、且没盖满 widget，边缘残留上一帧 —— 就是当时看到的重影。

`kPresentLatencyFrames` 取 1（demo 是 2）：三槽下延迟一帧已足够安全，可见滞后少一帧。
单线程行为不变（帧完成计数在无渲染线程时直接返回）。

**验证**：两种模式出图正常、无重影；单/多线程像素差 0.062%（已知的悬停抖动）；`GL_INVALID` 0。

### 8.70 拾取失效：世界坐标附件必须用可变纹理，否则整个拾取目标被裁成 1×1（2026-11-22）

**现象**：拾取读回全是 0，鼠标点不到东西。

**根因**：拾取目标在构造时按 1×1 创建，每次拾取前 `ResizeFramebuffer` 到帧大小。世界坐标附件用
的是 `glTextureStorage2D`（不可变存储），重新指定存储是 `GL_INVALID_OPERATION`，实际存储永远停在
1×1；而 framebuffer 的渲染区域取**所有附件的最小值**，于是连 id 附件也被裁到一个像素，读回自然全 0。

**修法**：给这个附件的 `TextureDesc` 加 `mutableDesc`，走 `glTexImage2D`（可变纹理，可重新指定
存储）。颜色附件本来就是这个原因由 `FramebufferUtil` 按可变纹理创建的。

**验证**：resize 生效后 id 与世界坐标都能正确读回。

### 8.71 IBL 绑定改执行期解析：录制期不许读执行结果（2026-11-22）

**根因**：`ABaseRenderer::SetEnvironmentTextures` 原来先用 `IsValid()` 判断 irradiance/prefilter
立方体是否已分配，再 `SetTexture`。这两个立方体由 `SkyboxRenderPass` 每帧捕获生成，GL 存储是命令，
"是否有效"只有执行过才成立 —— 典型的录制期读执行结果，也是渲染路径里几处
`ExecutePendingCommands` 存在的依赖之一。

**修法**：无条件把纹理记进描述符。描述符里存的是 **handle**，真正解析发生在执行器绑定这一帧的
descriptor set 时：那时同批次里的捕获命令已经跑过，名字有效。

**当时没做完的部分**：只改这一处时，把渲染路径里的 `ExecutePendingCommands` 删掉仍然崩
（两种模式都 0xC0000005），说明还有别的录制期读结果的地方。那一轮排查的结论指向 §8.72。

### 8.72 最终一步：录制纯追加，执行只发生在 `Execute()`（2026-11-22 晚）

**目标一句话**：主线程只录命令，命令环装得下一帧，执行只发生在 `Execute()`；单线程与多线程的
录制代码完全一致（不再有 `IsRenderThreadActive()` 决定"就地执行还是入队"，也不再有
`m_executing`）。

**改了什么**

1. **命令环按帧定尺寸、录制纯追加**（`Driver.cpp`）：`kRequiredCommandBytes` 8 KB → 1 MB、
   `kCommandRingBytes` 64 KB → 8 MB；`Record()` 删掉"环满就执行"的分支。一批帧命令永远装得下，
   录制中途不会再触发执行。
2. **present 材质改 MatC**：新增 `Resource/Moon/Data/Engine/Materials/Blit.mat`，包是已有的
   `:Shaders\PostProcess\Blit.shader`（它生成的 postProcess 顶点阶段与原来源码构建的那份逐行等价，
   没有再加一份重复的 `.shader`）。`ABaseRenderer::m_presentMaterial` 变成 `MaterialAsset*`，
   `_InputTexture` 是**声明过的参数**，走 PerMaterial 描述符集。
3. **深剥离的合成材质同样改 MatC**（`SceneRenderer` 的 `TransparentRenderPass`）：它的
   `colorLayer` 松散采样器改成同一个 Blit 包的 `_InputTexture`。
4. **删掉松散采样器路径**：`SetSamplerUnit` 命令、`OpenGLDriver::SetSamplerUnit`、
   `m_boundProgram`、`MaterialInstance` 的 `BindTexture` 与按名绑定分支全部删除。
   `SetProperty` 收到未声明的采样器时：engine 的 PerView 采样器（`_BRDFLut` / `_IrradianceCube` /
   `_PrefilterCube`）静默忽略，其余按"材质+名字"打印一次警告。
5. **HAL 前端一律入队**（`GLShaderStage` / `GLShaderProgram` / `GLFramebuffer` / `GLRenderbuffer` /
   `GLTexture` / `GLBuffer` / `Descriptor` / `ResourceOps` / `Im3DType`）：只要有 driver 就记录，
   只有"没有 driver"的独立工具路径才直接调 GL。资源创建处保留 `ExecutePendingCommands()`
   （句柄要先在记录侧可见），这是规则允许的同步点。
6. **执行器里不能调前端 —— 本轮真正的根因**：删掉 `DrawMaterial` 命令。它原来在**批内**调用
   `MaterialInstance::Bind` / `UploadProperties` / `DescriptorSet::BindFixed`；前端改成"只入队"
   之后，这些调用会把命令写到**正在被执行的批的后面**（写指针在批尾之后），材质从未真正绑上：
   整片实体消失，只剩天空和叠加层。材质绑定移回录制侧（`ABaseRenderer::DrawEntity`），
   于是"program + 描述符集 + 块 + 纹理 + 状态 + draw"是一串按序记录的命令。
7. **删掉渲染路径里的 6 处同步执行**：`BindFramebuffer` / `SetViewport` / `SetScissor` / `Clear` /
   `Draw` / `SetPipelineState` 不再 `ExecutePendingCommands()`；`Driver.h` 的 `m_executing` 与其
   全部分支删除。`ExecutePendingCommands` 只留给资源创建、`Link`/`Validate`、读回、`AView`
   构造里初始化目标。
8. **两处直接 `glEnable(GL_PROGRAM_POINT_SIZE)` 改命令**（`SceneRenderer` 的 line pass、
   `PickingRenderPass`）：它们是录制线程发出的 GL，执行侧上下文里根本没开。
9. **HZB 两种模式都关**：它自带 fence + PBO 异步读回，是渲染路径里最后一块直接 GL。线程模式下
   本来就关着；单线程也关掉，两边录制才一致。要恢复遮挡裁剪，先把那段读回做成命令。
10. **保留一个诊断开关**：`MOON_CMD_TRACE` 在 `OpenGLDriver::Execute` 里逐条命令查 `glGetError`，
    把错误归因到具体命令（本轮定位"画不出来"就是靠它：错误只出现在 `DrawMesh` 上，而材质绑定
    在批内被写坏了）。

**验证**（`1954×812`，`hezi.step`）：

| 对比 | 结果 |
|---|---|
| 单线程 vs 渲染线程 | **0.000% 逐像素相同** |
| 本次渲染线程 vs 改造前基准 | 仅 CAD 拓扑红点（1125 px，双稳态） |
| 本次单线程 vs 改造前基准 | 同上 |
| 崩溃 / `GL_INVALID` | 无 / 0 |

> 红点是**双稳态**：同一份构建连跑两次也会在两个状态之间翻（点与黑线共面、谁后画谁赢），
> 与渲染模式无关。对比脚本 `Build/refactor-shots/compare-dots.py` 会把"只差红点"判为通过。

**已知遗留（后续项）**

- **HZB 需要命令化**：PBO 读回 + fence + 层级 down-sample，完成后才能把第 9 条恢复。
- **松散标量 uniform 现在两种模式都无效**（如 include 里的 `u_ReceiveShadows`）：机制已在 §8.73
  整段删除，要恢复的应声明成材质参数、走参数块。
- **路径追踪的 source-built 材质**（`PathTraceRenderPass` 的 `_InputTexture`）不再能绑采样器。
  它在渲染线程模式下本来就绑不上（录制线程无上下文），属于"源码构建材质永久例外"的后续改造。
- **`Validate()` 在有 driver 时直接返回 true**：附件在执行侧才挂上，这一侧查不到；原来线程模式
  就是这样，现在两边一致。依赖它自愈的路径（Gbuffer/PostProcess ping-pong）不再触发。

### 8.73 删除松散 uniform 机制（2026-11-23 凌晨）

**背景**：§8.72 之后材质只剩"PerMaterial 描述符集"一条路。`m_looseUniforms` 是给源码构建材质
（没有包、没有参数块）按名字上传 uniform 的表，而它唯一的出口 `GLShaderProgram::SetUniform` 在有
driver 时已经是空操作 —— 这张表已经什么都不做。

**删掉的内容**

- `MaterialInstance`：`LooseUniform` 结构、`m_looseUniforms`、`FindLooseUniform`（两个重载）、
  `SetLooseUniform`、`UploadProperties` 里的按名上传循环，以及 `SnapshotProperties` /
  `GetProperty` / `ResetPropertyToDefault` / `HasProperty` / `ResetProperties` 里的松散分支；
- `Material` 上那个从未实现的 `TextureOf` 转发；
- `GLShaderProgram::SetUniform`：唯一的调用者就是上面那个循环，留一个"永远不生效"的写入接口比
  没有更糟。

**行为上唯一的变化**：未声明的属性不再被保存。`SetProperty` 收到不是材质参数的属性时，按
"材质 + 名字"打一次警告：

```
[material] :Shaders\ssaoblur.shader: '_InputTexture' is not a parameter of the material: the value is ignored
```

警告正好把还依赖松散 uniform 的地方暴露出来，也是本条的**遗留**：

- **路径追踪**（`PathTraceRenderPass`）：`PathTrace.shader` 由 pass 注入 define 后源码编译，
  `camera.position` / `resolution` / `maxDepth` / `uniformLightCol` 等二十余个属性全是松散标量；
- **`DebugShapeRenderFeature`** 的调试线材质（`viewProjection` / `start` / `end` / `color`）。

两者在本轮之前就已经拿不到这些值（`SetUniform` 在有 driver 时是空操作），所以这不是回归，是把
既有缺口显性化。要恢复它们，得把参数声明进材质（走 MatC 参数块），或者给驱动补一族"给当前程序
设 uniform"的命令（2D 叠加层的 `SetProgramMatrix4` 就是这个形状）。

**验证**：`1800×901`（窗口随桌面尺寸变了，和旧基准不是同一尺寸，不能直接比历史图）。单线程 vs
渲染线程 **0.000% 逐像素相同**；同模式重跑只差 CAD 拓扑红点（双稳态）；无崩溃、`GL_INVALID` 0；
出图正常（实体面、边线、红点、导航方块都在）。

### 8.74 呈现改成"贴最近完成的一槽"；相机仍不跟手，待查（2026-11-24 上午）

**症状**：8.69 之后拖拽旋转仍然"不跟手"（跟手的标准：鼠标动了，画面在同一帧或下一帧就转起来，且 GUI 不被渲染拖住）。

**这一轮的定位**：稳态下 `mDriverSlot == mSubmittedFrames - 1`，而画槽用 `mDriverSlot % 3`、贴槽用 `(mSubmittedFrames - 1) % 3` —— 两者恒等，也就是"往正在被贴的那张纹理里画"。跨上下文读写同一张纹理时驱动只能在 paste 处隐式同步，再叠加 GUI 线程里那条 `WaitForPresentedFrame()`（等渲染线程把要贴的那一帧执行完），GUI 与渲染线程完全串行，每帧 GUI 都被卡掉一个渲染帧的时间。Debug 下渲染慢 3~5 倍，所以特别明显。

**做法（无 fence，呈现不等待）**：

- 画槽改成 `mSubmittedFrames % 3`（`AView.cpp`），和贴的槽永不重合；
- 帧的最后一条命令（原来的 `Commit` 计数）改成 `Commit(slot)`：执行时把 `mPresentedSlot` 置为该槽（atomic，release/acquire），语义是"这一槽的命令都执行完了"；
- 删掉 `WaitForPresentedFrame` / `mCond` / `mDriverSlot`；`PresentedTarget()` 改为读 `mPresentedSlot`：渲染线程还没登记这一帧就继续贴上一槽（最多晚一帧），**GUI 线程任何时候都不阻塞**；
- 单线程模式在 `AView::Render` 末尾自己 `Execute()`（与 Filament `flush()` 里的 `if constexpr (!UTILS_HAS_THREADING) execute();` 同形），否则没有消费点、而且旧实现会死等。

**关于 fence 的两次更正**（之前写错的地方，留档）：

1. §8.69 里"照 Qt/Filament demo 的 `PlatformGlfwGL`：`RING_COUNT = 3`、`waitFrameCompleted`、`getTextureId`" —— **这个出处不存在**。`waitFrameCompleted` 与 `PlatformGlfwGL` 在 Filament 源码和网上都搜不到，是凭印象写的；§8.69 那段引用作废，原文保留以便对照。
2. Filament 的帧循环里**没有等 GPU 的调用点**：
   - `filament/src/details/Engine.cpp`：`FEngine::loop()` 是 `while (true) { if (!execute()) break; }`；`execute()` 只做 `mCommandBufferQueue.waitForCommands()` → `driver.execute()` → `releaseBuffer()`；`FRenderer::endFrame()` 最后也只是 `engine.flush()`。
   - fence 只出现在两处：`FrameSkipper`（`endFrame` 里 `driver.createFence()`，`beginFrame` 里 `driver.getFenceStatus()` —— **非阻塞**，GPU 落后就返回 false 跳帧）和 `flushAndWait`（`getDriverApi().finish()` + `FFence::wait()`，关机/读回用）。
   - `FFence` 本身是**命令流进度信号**：构造时 `driverApi.queueCommand([fs]{ pEngine->signalFence(*fs, SIGNALED); })`，`wait()` 是主线程在条件变量上等；不含 `glFenceSync`。GL 后端也没有"GPU 画完"的回调（`SwapChain::setFrameCompletedCallback` 只在 Metal 实现）。
   - 因此"呈现前加 fence"这条建议作废。跨上下文贴纹理的正确性靠"画槽 ≠ 贴槽"，以及驱动对共享纹理的串行化。

**验证**：

- Release，`1800×901`，单线程 vs 渲染线程：`compare-dots.py` → `IDENTICAL`，max delta 0（`Build/refactor-shots/wV-release-single.png` / `wV-release-thread.png`）；
- Debug 线程模式：正常出图，22s 截图，26s 自行退出（不再需要手动 kill）；
- GUI 不再等渲染线程（结构性：`AView::Render` 里已无等待点）。

**仍未解决：相机还是不跟手**（用户实测）。下一步排查方向，按怀疑度排序：

1. **paste 自己的隐式同步**：`mPresentedSlot` 只保证"命令执行完"，不保证 GPU 画完；驱动在 paste 读这张纹理时仍可能等 GPU。测法：paste 前后夹 `glFinish`/计时，或 `MOON_FRAME_TIMING` 打印 paste 耗时。
2. **`Flush()` 的背压**：ring 满时 `CommandBufferQueue::Flush()` 阻塞录制侧（8 MB ring ≈ 2~3 帧命令）。Debug 下命令多，容易触发。
3. **Debug 构建本身**：渲染线程一帧可能 100 ms 以上，"晚一帧"在 Debug 下必然可感知；先只用 Release 评估跟手。
4. **GUI 每帧的其它工作**：`viewportframe` 里 3 个 present 槽 + 两个主目标每帧都 `ResizeFramebuffer`；`FlushDocumentRead` / `TreeViewSync` / `ReadPassSettingsOnce` 都在 GUI 帧里。
5. **2D overlay 还缺着**：`Im2DRender::EndFrame` 开头 `m_programName == 0` 就整帧 return（程序名是执行期才有的东西），视图立方体/图标因此不出现；评估跟手前先恢复它，避免把"没画面"误判成"不跟手"。
6. 量化工具（待加）：`MOON_FRAME_TIMING` 每帧打印 `record / flush / paste / interval`，以及渲染线程每帧耗时。

**本轮顺带修掉的崩溃链**（细节见各文件注释）：

- `GLBuffer::IsValid()` 不再要求 GL 名（只查 handle + type）；`UpdateBuffer` / `SetVertexArrayLayout` / `BindDescriptor` 三条命令改成携带 handle，执行期解析；
- 材质纹理不在录制期 `asset->GetTexture()`（描述符只存 handle/asset）；
- `FramebufferUtil::SetupFramebufferWithDriver` 改在录制侧建目标（前端纹理 + 按 handle 记录 Attach），否则 `GetAttachment` 在录制期是空的（Grid pass 的 `bad_optional_access`）；
- `Driver::ResizeFramebuffer` 在前端立即更新尺寸（否则 `GetSize()` 整帧还是 1×1，Bloom 的 `clamp` 断言）；
- `RenderModuleHost::Loop()` 改用已有的 `Driver::WaitAndExecute()`：阻塞等批次 + 能看见 `RequestExit()`（修掉空转和退出挂死）；
- `Moon/main.cpp` 增加未捕获 C++ 异常与致命错误的符号化栈打印，`CommandStream.h` 在命令体抛异常时打印命令名（定位上述崩溃用的）。

### 8.75 buffer 改成"后端对象 + 前端持句柄"（2026-11-24 下午）

**目标分层（用户确认）**：

```
前端对象 Data::BufferObject ──持有──> ResourceHandle<HwBufferObject> ──指向──> HAL::Buffer（槽里的后端对象）
                                     （glId / fence；type / size 在 HwBufferObject 基类里）
```

与 `HAL::Texture` / `Resources::Texture` 的现有分工一致：HAL 对象是槽里的后端对象（构造时拿 GL 名、方法是后端操作），前端对象只持句柄、只录命令。这也补上了之前那半边：buffer 的 "record" 不再是 `{uint32_t name}`，而是真正的 HAL 对象。

**改了什么**

1. `Backend::HwResource` + `Backend::HwBufferObject`（`BackendResources.h`）：`HwBufferObject : HwResource` 带 `type` 与 `size`（持句柄也能读）；`HwResource` 只提供虚析构。
2. `HAL::TBuffer` 变成后端对象：继承 `Backend::HwBufferObject`，构造函数收 `BackendConstructionTag`（照 `HAL::Texture` 的做法）并在里面 `glCreateBuffers`；`GLBufferContext` 只剩 `{glId, fence}`，**不再有 handle**。析构不碰 GL —— 名字和 fence 由 `Records::DestroyBuffer` 取出，经 `DeferredDeleter` 排到持上下文的线程删。
3. `Records::CreateBuffer(handle, type)` 用 `Construct<HAL::Buffer, HwBufferObject>` 在槽里构造；幂等（已存在就返回，不再像以前那样换掉记录、漏掉旧 GL 名）。`Records::BufferName` 改成 `handle_cast<HAL::Buffer>` 取 `glId`。
4. **顺手修掉真 bug**：`ResourceOps::EnsureBuffer` 以前写死 `EBufferType::VERTEX`，index / uniform / SSBO 全被记成 VERTEX；现在按调用方传的类型创建（`EnsureBuffer(handle, type)`）。`BindBuffer` 命令也改成解析后端对象、用它自己的 type 当 target（以前用请求里带的 type）。
5. 新增前端 `Rendering::Data::BufferObject`（`MoonRender/{include,src}/Rendering/Data/BufferObject.*`）：只有 handle + type + size，方法全部转调 `HAL::ResourceOps` 录命令；可移动、不可拷贝，析构时 `DestroyBuffer`。`Mesh`、`UniformBufferPool`、`EngineBufferRenderFeature`、`SkyboxRenderPass`、`ReflectionRenderFeature`、`CReflectionProbe`、`LightingRenderFeature`、`HzbBuildPass` 全部改用它；`Im2DRender` / `Im3DGLRenderer` / `Im3DType` 本来就只持句柄，只补了 buffer 类型。`Descriptor` / `MaterialInstance` 不用改（本来就只存 `ResourceHandle<HwBufferObject>`）。
6. 新增命令（`DriverAPI.inc` + `OpenGLDriver`）：`BindBufferRange`（uniform block 的 range bind，之前 `UniformBufferPool::Bind` 是录制期直接 GL；这个接口目前没有调用点）、`UnmapBuffer`、`InsertFence`、`ClearFence`。`MapRead` / `IsFenceSignaled` 是同步 API（Filament 里是 `DECL_DRIVER_API_RETURN`），现在只能"没有 driver 或正在 driver 线程"时内联执行；其余情况 Debug 断言 + Release 打一次警告并返回 nullptr/false，TODO 写在 `ResourceOps.cpp` 里。
7. 删掉前端那套 `HAL::UniformBuffer` / `VertexBuffer` / `IndexBuffer` / `ShaderStorageBuffer`（含 `T*Buffer` / `GL*Buffer` 头与实现共 16 个文件）；`TVertexArray::SetLayout` 改成吃两个 buffer 句柄，`HAL::Buffer` 这个名字现在就是后端对象本身。

**验证**（Release，`1254×416`，`hezi.step`，13 s 截图，等 fit 动画跑完）：

| 对比 | 结果 |
|---|---|
| 单线程 改前 vs 改后 | **0.000% 逐像素相同**（`IDENTICAL`，max delta 0） |
| 单线程 vs 渲染线程（改后） | **0.000% 逐像素相同** |
| Release 单线程 vs Debug 单线程 | **0.000% 逐像素相同** |
| Debug 单线程 vs Debug 渲染线程 | **0.000% 逐像素相同**（断言全部开启，没有一条触发） |
| 运行时日志 | 与改前逐行一致（仅时间戳与输出路径不同），无 `GL_INVALID`、无新警告 |
| 构建 | Release / Debug 都通过 |

另外两种模式各跑了一次 `MOON_CMD_TRACE=1`（逐条命令 `glGetError`）：单线程与渲染线程都是 **0 条命令报错**，说明新的 create / allocate / upload / bind 命令在批里的顺序是对的。截图留在 `Build/refactor-shots/{base-before,final,debug}-*.png`。

**遗留**

- `MapRead` / `IsFenceSignaled` 要真正的同步 driver API（`DECL_DRIVER_API_RETURN` 形状：命令在 driver 线程跑、结果回传给录制侧），HZB 的异步读回才能重新打开。HZB 仍按 §8.72 第 9 条关着，`Framebuffer::ReadPixelsToBuffer` 还是录制侧直接 GL（这一轮只把参数从 HAL 对象改成句柄）。
- 其余记录（framebuffer / renderbuffer / vertex array / program）已在 §8.76 里按同一步骤做完。


### 8.76 第二批：framebuffer / renderbuffer / vertex array / program 也改成"Data 前端 + HAL 后端对象"（2026-11-24 傍晚）

和 §8.75 完全同一套形状：**HAL 里的是后端对象**（住在 `HandleAllocator` 的槽里、持 GL 名、GL 操作是它的方法），**`Rendering::Data::*` 里的是前端对象**（只持 `ResourceHandle<Hw*>`、只录命令）。这一批一开始把方向搞反过一次（HAL 当前端、`Backend::GL*` 当后端），按用户要求改成了现在这个形状。

**改了什么**

1. `HwVertexArray` / `HwFramebuffer` / `HwRenderbuffer` / `HwProgram` 都变成 `: HwResource` 的基类。
2. HAL 的四个类变成槽里的后端对象，构造用 `BackendConstructionTag`（照 `TBuffer`）：
   - `TVertexArray`（= `GLVertexArray`）：`glId` + `Bind` / `Unbind` / `SetLayout` / `GetID`，构造里 `glCreateVertexArrays`；
   - `TRenderbuffer`（= `GLRenderbuffer`）：`glId` + `Bind` / `Allocate` / `GetID`，构造里 `glCreateRenderbuffers`；
   - `TFramebuffer`（= `GLFramebuffer`）：`glId` + `Bind` / `Unbind` / `Attach{Texture,TextureLayer,Renderbuffer}` / `SetDraw{,Read}Buffer(s)` / `CheckStatus` / `GetStatus` / `BlitTo` / `Clear` / `ReadPixels` / `ReadPixelsToBuffer` / `GetID`，构造里 `glCreateFramebuffers`；
   - `TShaderProgram`（= `GLShaderProgram`）：`glId` + `Build`（编译/链接/反射）/ `Bind` / `Unbind` / `DeclareBindings` / `GetID`，构造里 `glCreateProgram`；反射表仍存在 `HwProgram` 基类上。
3. 新的前端对象（都在 `Rendering/Data/`，形状照 `BufferObject`，全部 move-only、析构里 `Records::Destroy*`）：
   - `VertexArrayObject`（handle + 属性个数）；
   - `Renderbuffer`（handle + 宽高/格式/MSAA/是否 allocated）；
   - `Framebuffer`（handle + 附件表 `shared_ptr<HAL::Texture>` / `shared_ptr<Data::Renderbuffer>` + debug 名 + valid；`Attach` / `AttachLevel` / `GetAttachment<T>` / `Resize` / `GetSize` / 读回这些都在这边）；
   - `Program`（handle + 已附加的 stage + 描述符布局与槽位表；`Attach` / `Link` / `Bind` 在这边）。
4. `Records::Create*` 改成 `Construct<HAL::*, Hw*>`（带 tag、幂等）；`XName` 改成 `handle_cast<HAL::*>` 取 `GetID()`；`Destroy*` 取名字 → `Deallocate` → `DeferredDeleter` 删 GL 名，和 buffer 完全一样。`RegisterProgram(handle, name)` 换成 `CreateProgram(handle)`：程序对象在槽里创建，`Build` 才编译链接。
5. 执行器只做"解析对象 → 调方法"（`AttachTexture/AttachRenderbuffer`、`AllocateRenderbuffer`、`SetFramebufferDrawBuffers`、`SetVertexArrayLayout`、`CreateProgram`、`BindProgramHandle`、`DeclareProgramBindings`）；前端在没有 driver 时也解析对象再调方法，不再自己发这些 GL。附件表、尺寸、debug 名留在前端（录制侧读）。
6. 调用点：37 个文件里 `HAL::Framebuffer` → `Data::Framebuffer`（`FramebufferRequests` / `Driver::*Framebuffer` 的签名一起改）；`HAL::Renderbuffer` → `Data::Renderbuffer`（Skybox / CReflectionProbe / FramebufferUtil）；`HAL::ShaderProgram` → `Data::Program`（Material / MaterialInstance / DescriptorSet / ShaderLoader）；`Mesh` 的顶点数组改成 `Data::VertexArrayObject`。
7. 删掉上一轮临时放在 `Backend/GLResources.*` 的 `Backend::GL*` 类（实现搬进了 HAL 后端对象），那一批的错误方向没有留在代码里。

**验证**（Release / Debug，`1254×416`，`hezi.step`，13 s / 20 s 截图）：

| 对比 | 结果 |
|---|---|
| 第二批后 vs 第一批后（Release 单线程） | **0.000% 逐像素相同** |
| Release 单线程 vs 渲染线程 | **0.000% 逐像素相同** |
| Release 单线程 vs Debug 单线程 | **0.000% 逐像素相同** |
| Debug 单线程 vs Debug 渲染线程 | **0.000% 逐像素相同**（断言全开） |
| `MOON_CMD_TRACE=1`（两模式） | **0 条命令报错** |
| 构建 | Release / Debug 都通过 |

截图：`Build/refactor-shots/{b2,b3,d,e,f,g,h,i}-*.png`。其中 `f-thread.png` 有一次跑出了模型下方的镜像倒影（之后连跑 5 次都复现不了，单线程与渲染线程都稳定为同一张图），判断是渲染线程起步阶段的一次性时序现象，和这一批的改动无关，留档备查。

**遗留**

- framebuffer 的读回（`SetTargetReadBuffer` + `ReadPixels` / `ReadPixelsToBuffer`）仍是同步直接 GL，要成对命令化；HZB 继续关着（§8.72 第 9 条）。
- `TShaderProgram` 的反射表现在没有消费者（§8.73 删掉了松散 uniform），留着给诊断和后续"给当前程序设矩阵"那类命令用。
- texture 仍是双模对象（`TTexture` 里 `id` 或 `handleId`），没有并进这一套 —— 它在 §8.75 之前就是这个形状，`Resources::Texture` 是它的前端。

### 8.77 读回命令化 + HZB 重新打开（2026-11-24 傍晚）

**背景**：§8.76 之后还剩两处"录制侧直接 GL"：framebuffer 的 `SetTargetReadBuffer` + `ReadPixels` / `ReadPixelsToBuffer`，以及因为它们一直关着的 HZB（§8.72 第 9 条）。

**改了什么**

1. **`SetFramebufferReadBuffer` 命令**：picking 的"选读哪个附件"不再就地 GL，而是入队；`ReadPixels` 的 `readBufferIndex = -1` 表示"沿用已经记录的选择"，两条命令按批内顺序执行。
2. **`ReadPixels` 命令扩成两半**：新增 `framebufferHandleId`（执行期解析目标，不再在录制侧取名字）。`Data::Framebuffer::ReadPixels` 变成"记录 + 等"：记录读回命令，再 `Driver::FlushAndWait()`，把字节拷给调用方。这条**同步 driver 调用**只用于帧与帧之间的调用（拾取、截图读回）。
3. **`Driver::FlushAndWait()` / `IsExecutorThread()`**：同步调用的公共部分——单线程时 `Flush() + Execute()`（本来就是帧尾的执行点，只是提前到调用时）；渲染线程模式下追加一条 barrier 命令、publish、然后等它跑完。`ResourceOps::MapRead` / `IsFenceSignaled` 也改成这个形状（Filament 的 `DECL_DRIVER_API_RETURN`），不再是"录制侧断言 + 打警告"。
4. **`ReadPixelsToBuffer` 命令**：HZB 的 PBO 读回不再在录制侧 `glReadPixels`。
5. **`DownloadBuffer` 命令**：GPU 写完 PBO 之后把字节拷回 CPU（`glGetNamedBufferSubData`，**只在 fence 已 signaled 时**做），回调告诉录制侧"拿到了"还是"再试一次"。HZB 现在是 `ReadPixelsToBuffer` → `InsertFence` → 后续帧 `DownloadBuffer` 三步，帧内完全不等 GPU，也不需要映射。
6. **HZB 自己的 GL 全部命令化**：`FramebufferUtil::CopyFramebufferDepth` 改走 `Driver::CopyFramebufferDepth`，`m_levels[i]->Bind()` 改走 `Driver::BindFramebuffer`，帧尾还原目标同样；`AView` 里的 `SetEnabled(false)` 删掉（PreviewView 仍关：预览场景没有 BVH，没有可剔除的遮挡）。

**验证**（`1254×416`，`hezi.step`）：

| 对比 | 结果 |
|---|---|
| HZB 打开（单线程）vs HZB 关闭基线 | **0.000% 逐像素相同**（这个视角没有可剔除的遮挡物，剔除只去掉不可见的几何） |
| HZB 打开：单线程 vs 渲染线程 | **0.000% 逐像素相同** |
| HZB 打开：Release vs Debug 单线程 | **0.000% 逐像素相同**（Debug 断言全开，无触发） |
| `MOON_HZB_TRACE=1` | 两模式都能看到 `asking for the readback` → `grid 39x13 on the CPU after N frame(s)`（N = 1…6），PBO→fence→下载链在渲染线程上也跑通 |
| `MOON_CMD_TRACE=1`（两模式） | **0 条命令报错** |
| 截图读回 | `MOON_SHOT` 走的正是新的同步读回（`AView::ReadFrame` → `Data::Framebuffer::ReadPixels` → `FlushAndWait`），两模式出图一致 |
| 构建 | Release / Debug 都通过 |

**遗留**

- `MapRead` 的同步形状已经可用，但当前没有调用者（HZB 改用 `DownloadBuffer`）。真要使用，读映射内存要在 Unmap 命令之前完成，和 Filament `mapBuffer` 的约束一致。
- 拾取的同步读回仍会让录制侧等一次 `FlushAndWait`（拾取 API 本身是同步的）；异步版本 `IssuePickReadback` 已经是命令 + 回调，帧内不等。
- 调试开关：`MOON_HZB_TRACE` 打印读回的发起与到达。
