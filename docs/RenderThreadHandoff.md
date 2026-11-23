# 渲染线程剥离 · 交接文档

> 交接时间：2026-11-20 上午。工作区未提交，约 70 个文件有改动。
> 详细逐条记录见 `docs/RenderThreadPlan.md`（§7.x 与 §8.1–§8.44），本文只写"接手要什么"。

## 1. 目标

把 `MoonRender` 的渲染从"调用线程直接发 GL"改成 Filament 的形状：

- **录制侧**（主线程）只往命令流写命令，不碰 GL、不需要上下文；
- **执行侧**（渲染线程）从命令流取批次执行，独占模块的 GL 上下文；
- 前端只持有 handle，GPU 对象在执行侧创建。

## 2. 已经完成并验证的部分

| 项 | 位置 | 验证 |
|---|---|---|
| `CommandStream` 录制 / `OpenGLDriver` 执行 | `Rendering/Backend/CommandStream.{h,cpp}`、`DriverAPI.inc` | 默认路径 0 差异 |
| 录制入口全部在 `CommandStream`（含命令名） | 同上，`DECL_DRIVER_COMMAND` 宏 | 同上 |
| `EnsureRoom` 已删除；环只按一批大小配置；溢出断言 | `CommandStream`、`CircularBuffer` | 同上 |
| `makeCurrent` 作为**帧的第一条命令** | `Driver::MakeCurrent` → `OpenGLDriver::MakeCurrent` | 线程模式日志 |
| 上下文只归驱动线程（非驱动线程 `MakeCurrent` 返回 false） | `QtGLContextHost::MakeCurrent` | 同上 |
| offscreen surface 在 GUI 线程准备、driver 线程复用 | `QtGLContextHost::PrepareSurface` + `CreateDriver` | 线程模式可运行 |
| framebuffer 创建/改尺寸/拷贝 命令化 | `SetupFramebufferRequest`、`ResizeFramebuffer`、`CopyFramebufferColor` | 默认 0 差异 |
| 顶点布局命令化（UAV 属性从未应用的 bug 修复） | `SetVertexArrayLayoutRequest` | 线程模式跑完整串 pass |
| 缓冲分配/上传命令化（负载堆上自持） | `AllocateBuffer`、`UpdateBufferRequest` | 默认 0 差异 |
| 描述符上传/绑定命令化 | `UpdateBuffer`、`BindDescriptor` | 默认 0 差异 |
| 读回命令化 | `ReadPixelsRequest` → `ReadPixels` | 用于探针 |
| `Mesh::GetIndexCount` 边界检查 | `Mesh.cpp` | 修掉一次崩溃 |
| `gladLoadGL` 只加载一次 | `GLBackend::Init` | — |
| Tracy GPU 上下文的线程判定 | `GLBackend::OnFrameCompleted` | **修掉了线程模式崩溃** |
| 后处理 PingPong 目标自愈 | `PostProcessRenderPass::Draw` | 目标变完整 |
| 程序创建的脚手架（**未接线**） | `ProgramRequests.h`、`Backend::HwProgram`（`HandleAllocator` 槽位，见 Plan §8.45）、`OpenGLDriver::CreateProgram`、`GLProgramBindings.{h,cpp}` | 能编译，未使用 |

默认路径（`MOON_RENDER_THREAD` 未设置）目前与基准截图 **逐字节一致**，这是硬约束：每次改动后都要复核。

## 3. 线程模式的现状

启动：

```powershell
$env:QT_OPENGL="desktop"; $env:MOON_RENDER_THREAD="1"
& .\Build\bin\Release\Moon.exe .\Resource\Moon\Data\Engine\Models\material_sphere.obj
```

- 不崩、干净退出、带 `MOON_SHOT` 能存图；同一构建两次运行逐字节相同。
- **场景画面已经与默认路径逐字节一致**（1254x480 下只差右上角导航方块那块，见 §7.2）。
- `GL_INVALID_FRAMEBUFFER_OPERATION` 0 条，不完整 FBO 绑定 0 条。
- 还缺的是**叠加层**（ImGui / 2D 列表 / gizmo / 导航方块）：它们的 GL 仍在录制线程发。

## 4. 剩余工作（按顺序）

> §4.1（程序命令化）与 §4.2（纹理命令化）都已完成，见 §7.1 与 Plan §8.47–§8.50；下面是当时写下的原始计划，保留作为背景。

### 4.1 程序创建命令化（把画面找回来的关键）

原因：`GLShaderProgram` 的 `glCreateProgram / glAttachShader / glLinkProgram / QueryUniforms`、`GLShaderStage` 的
`glCreateShader / glShaderSource / glCompileShader` 都在 GUI 线程（文档加载时）执行；线程模式下没有上下文，
程序不可用，于是绘制全部无效。

脚手架已就位：

- `Rendering/Backend/ProgramRequests.h`：`ShaderSourceSpec{type, source}`、`CreateProgramRequest{programHandleId, stages, bindings}`；
- `Rendering/Backend/ProgramRegistry.{h,cpp}`：handle → GL 名字（照 `BufferRegistry`）；
- `OpenGLDriver::CreateProgram`：建 shader、编译、link、释放 shader、登记名字、**调用 `DeclareProgramBindings`**；
- `Rendering/HAL/OpenGL/GLProgramBindings.{h,cpp}`：把 binding map 声明给程序（`glGetUniformBlockIndex + glUniformBlockBinding`、
  `glGetUniformLocation + glUniform1i`）。**这是唯一的 GL 部分**，槽位编号本身是纯 CPU 记账。

要接线的地方：

1. `GLShaderStageContext` 加 `handleId` 与 `std::string source`；线程模式下构造不建 GL 对象，`Upload` 只存源码，
   `Compile` 直接返回 success（真正的编译在执行端）。
2. `GLShaderProgramContext` 加 `handleId`；线程模式下构造不 `glCreateProgram`；
   `GetID()` 改成 `ProgramRegistry::Find(handle)`；`Bind()` 里的 `glUseProgram(GetID())` 保持。
3. `GLShaderProgram::Link()`：线程模式下把已 attach 的 stage 的 `{type, source}` 打包，发一条 `CreateProgram` 命令
   （带上 `m_context.descriptorBindings`），返回 `{success=true}`；非线程模式走现有路径。
4. `ShaderLoader` 的调用顺序（`stage->Upload / Compile`，`program->Attach / Link / Detach`）不用改。
5. 全部接好后：线程模式应能出画面；再复核默认路径 0 差异。

### 4.2 纹理创建/上传命令化

**已完成**（Plan §8.50）：`HAL::Texture` 在非驱动线程上只取句柄，创建/分配/上传/改 border/生成 mipmap/析构都是命令；
原来的直接创建点（fallback 空纹理、gbuffer 与镜子的附件、天空盒立方体、图层、HZB、拾取、Im3D/2D）不用逐个改。

### 4.3 其余记录侧 GL

已用 `IsRenderThreadActive()` 临时跳过、需要命令化的：HZB 读回（PBO+fence）、天空盒 capture、
拾取的异步读回（`IssuePickReadback`）、gizmo 的 `drawMeshPick`、拾取的第二个颜色附件。
另外 §7.3 列出的 `CopyFramebufferDepth` 调用点、`GLRenderbuffer` 的创建/Attach 也在这一类。

## 5. 约定与陷阱（重要）

**构建**（必须单进程并行，沙箱里 `/m` 会静默失败）：

```powershell
cmake --build Build --target Moon --config Release -- /nologo /m:1
```

新增源文件后先 `cmake -S . -B Build`。

**截图验证**（窗口尺寸会变，只与同尺寸基准比）：

```powershell
$env:QT_OPENGL="desktop"; $env:MOON_SHOT_DELAY_MS="15000"; $env:MOON_SHOT="<png>"
& .\Build\bin\Release\Moon.exe .\Resource\Moon\Data\Engine\Models\material_sphere.obj
python Build\refactor-shots\compare.py Build\refactor-shots\base-obj.png <png>
```

基准：`base-obj.png`(1160x487)、`base1254-obj.png`(1254x480)、`base1314-obj.png`(1314x504)。

**诊断开关**（默认关闭）：`MOON_RENDER_THREAD`、`MOON_RENDER_MINIMAL`、`MOON_FRAME_TRACE`（含 `[fbo] [dm] [probe] [paste]`）、
`MOON_CMD_TRACE`、`MOON_PASS_ONLY`。

**修改文件的教训**：

- 不要用 PowerShell 按行号/字符串批量改 C++（我因此两次改坏 `PostProcessRenderPass.cpp`、`PickingRenderPass.cpp`）。
  用编辑工具，改完 `Select-String` 复核。
- `Im3DType.cpp`、`BvhService.cpp` 是 **UTF-8 with BOM**，写成无 BOM 会破坏中文字面量。
- 命令里的 `MOON_EXPAND_ARGS` 宏不能处理空参数，无参命令（`MakeCurrent`）的录制方法要手写。

**设计纪律**（用户反复强调）：

- 不要自己发明；先读 Filament（`D:\Project\C++\filament`）与 Qt-Filament（`D:\Project\C++\Qt-Filament\test\PlatformGlfwGL.cpp`），照抄形状。
- `OpenGLDriver` 只做执行，不认识命令/命令流；录制逻辑全部在 `CommandStream`。
- 不要用 lambda 把 GL 藏进"自定义命令"来绕过命令化。
- 上下文只归一个线程；`MakeCurrent` 走驱动命令，是帧的第一条命令。

## 6. Git

工作区未提交（用户未说"提交"）。提交时用：

```powershell
git -c safe.directory="D:/Project/UseQt" add ...
```

`docs/ChamferHandoff.md` 是用户自己的未跟踪文件，**不要 stage**。

## 7. 续接点（2026-11-20 下午）

§1–§6 之后又推进了四块，**默认路径每步都复核过 0 差异**（窗口 1254x480，基准 `base1254-obj.png` / `base1254-1.png`）。

### 7.1 已落地

| 项 | 内容 | 文档 |
|---|---|---|
| 句柄层统一 | `Backend::HandleAllocator` 一个分配器取代六个 Registry（Buffer/VertexArray/Framebuffer/Renderbuffer/Program/Texture），`HwTexture` 就是 `HAL::Texture` | Plan §8.45 |
| Mesh 句柄化 | Mesh 只持 `ResourceHandle<HwBufferObject>`（VBO）、`vector<...HwBufferObject>`（IBO）、`vector<...HwVertexArray>`（VAO）；`MeshRegistry` 与旧的 `ResourceHandleAllocator` 删除；新增 `HAL::ResourceOps`（句柄寻址的 Ensure/Allocate/Upload/SetLayout） | Plan §8.46 |
| 程序命令化 | `CreateProgram` / `BindProgram` / `DeclareProgramBindings` / `SetSamplerUnit`；`GLShaderStage` 保存源码，`GLShaderProgram` 持 `HwProgram` 句柄；反射在执行侧做、结果写进 `HwProgram`、`Link()` drain 后拷回前端 | Plan §8.47–§8.48 |
| 纹理句柄化 | `GLTextureHandleContext.handleId`：`id != 0` 是 GL 对象本身，否则是前端句柄。前端构造只取句柄 + 记 `CreateTexture`；`Allocate/Upload/UploadSubImage/GenerateMipmaps/SetBorderColor` 与析构都记命令；执行端用 `BackendConstructionTag` 构造 | Plan §8.50 |
| 目标附件命令化 | `AttachTexture` / `SetFramebufferDrawBuffers` 命令；`GLFramebuffer::Attach(texture)`、`SetDrawBuffers`、`Validate` 在录制侧改记录；`GetAttachmentShared<T>`。`Driver::IsDriverThread()` 是"能不能直接发 GL"的唯一判据 | Plan §8.50 |
| 目标创建 | `SetupFramebufferWithDriver` 没有驱动时只打一行 `[fbo] SetupFramebuffer(...) without a render module` 并返回；`PathTraceRenderPass` 不再构造（后期重写） | 本文 §7.2 |

### 7.2 现状（**默认已经是渲染线程模式**）

`RenderModuleHost::ThreadedMode()` 默认返回真（Filament 的默认配置）；`MOON_RENDER_THREAD=0` 是单线程退出开关，旧的像素基准就是在那个配置下截的。

- **obj**：默认（线程）与单线程 **0 差异**，两次运行 0 差异，`GL_INVALID_*` 0 条；
- **3.moon**：线程模式可重复（两次差 2469 px = 已知悬停抖动），与单线程差 22.4%（mean 13.6、max 127）——已知的实体明暗差别（Plan §8.55 末），待查；
- 帧的形状是按 Filament 来的（Plan §8.57）：`AView::Render` 只 `Flush()`，`RenderModuleHost::Execute()` 每帧一次（`ViewportFrame::Render` 里 `renderJob()` 之后），有渲染线程时它发布后等驱动线程跑完再粘贴；
- 拾取已经命令化（Plan §8.59）：目标、附件、两条读回都在命令流里，`[pass] Picking` 的目标状态是 complete；**拾取在两种模式下都工作**。还没恢复的是 `HzbBuildPass`（遮挡剔除）与 `PointRenderPass`/`BvhService`（录制侧直接 GL，跳过）。
### 7.3 下一步（按顺序）

1. **3.moon 的非确定性**（Plan §8.61）：线程模式下草图实体在"接近正确"和"发黑"两种状态之间跳，两次运行差 22.9%。可观测的 GL 输入（EngineUBO/MaterialParams 行、采样器 id、状态、视口）两种模式一致，所以问题在录制侧时序：优先查 `PolygonMesh` 的 `isDirty`/`blockColorDirty` 与块色/边值纹理缓冲（`glGetTextureImage` 经命令读回对比），再查编辑器几何重建（JobSystem）与帧录制重叠。
2. **HzbBuildPass 命令化**（遮挡剔除目前关着）：目标/附件已是命令，剩下 PBO+mip 链的回读，照 §8.59 的做法。
3. **压力测试与性能**：每 drawcall 都是"必然绑定 + 必然上传"，`ParseScene` 每帧全量跑（Plan §8.54）。
4. 其余直接 GL：`PointRenderPass`、`BvhService`（纹理缓冲绑定）、`ReflectionRenderPass`/`SceneRenderer` 图层路径里的 `Bind()`。
5. 清理诊断：`[fbo] attach`、`[pp] constructed`、`[setup] record/execute`、`[sky] capture`、`[tex] re-allocate`、`[env]`。

**已经做完、别再找**：Qt-imgui 与 `ImGuiGLRenderer` 已移除；G-buffer 预览已删；FPS 统计走 `Im2DRender::AddText`；Im2D 的 lambda 出口已拆成命令（§8.56）；record/flush/execute 按 Filament 拆开（§8.57）；默认模式=渲染线程（§8.58）；拾取命令化 + 纹理重复分配修复（§8.59）；`EnsureTextureBuffer` 悬垂指针与 `ShadowRenderPass` 目标绑定修复（§8.61）。
### 7.4 诊断开关与常用命令

| 开关 | 作用 |
|---|---|
| `MOON_FRAME_TRACE` | `[view]/[pass]/[setup]/[fbo]/[sky]/[probe]/[present]/[ova]/[ovl]`；`[fbo] new`、`[fbo] bind ... pass=`、`[dm] ... pass=` 的标签也在这里 |
| `MOON_DRAW_TRACE` | 几何绘制的状态 dump：`[dm] S`（目标/程序/深度/视口）、`U`（EngineUBO/ObjectUBO 的行）、`T`（采样器与绑到的纹理）、`A`（VAO 属性）、`E`（索引缓冲） |
| `MOON_PASS_PROBE` | 每个 pass 之后把帧目标解到 present 再读中心点；值取 `c`/`r`/`d` 可只跑复制/读回/drain（就是用它二分出上面的第 3 条） |
| `MOON_RENDER_MINIMAL` / `MOON_PASS_ONLY` | 见 §5 |

```powershell
# 默认路径（必须 0 差异）
$env:QT_OPENGL="desktop"; $env:MOON_SHOT_DELAY_MS="15000"; $env:MOON_SHOT="Build\refactor-shots\x.png"
& .\Build\bin\Release\Moon.exe .\Resource\Moon\Data\Engine\Models\material_sphere.obj
python Build\refactor-shots\compare.py Build\refactor-shots\base1254-obj.png Build\refactor-shots\x.png

# 线程模式
$env:MOON_RENDER_THREAD="1"; $env:MOON_SHOT_DELAY_MS="10000"; $env:MOON_SHOT="Build\refactor-shots\y.png"
```

截图前把光标移到视口外（`SetCursorPos(5,5)`）；`3.moon` 与基准差 2447 px 是**已知悬停抖动**（同一构建两次运行 0 差异即可确认）。
PowerShell 里 `python` 没有 Pillow 时，用 `C:\Users\27181\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe`。
