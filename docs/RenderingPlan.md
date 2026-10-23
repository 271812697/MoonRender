# 渲染计划（CAD）

> 更新时间：2026-10-22
>
> 本文件是 CAD 渲染的**工作计划**：
> - §1 目标与边界；
> - §2 设计架构（主要参考 Filament）—— 2.1~2.5 是**已落地的现状**，**2.6 是待做的对齐目标**；
> - §3 计划表（效果 / 性能 / 交互），条目从 2.6 与各来源拆出来；
> - §4 工作方式：**未经明确"动手"不开始写代码**。

---

## 1. 目标

这是 CAD 领域的渲染，不是通用游戏渲染。工作集中在三个点上：

| 抓手 | 含义 |
| --- | --- |
| **渲染效果** | CAD 视图里"看对了没有"：剖切截面与截线、边线与高亮、拾取反馈、材质外观（PBR/IBL）、抗锯齿、深度精度、大装配体的可读性 |
| **性能** | 大模型（百万级三角形、上万面）下的帧时间：场景解析与剔除、绘制提交、状态切换、批次合并、变体数量、显存占用 |
| **交互** | 视口操作手感：相机、选择/悬停、gizmo、剖切控件、任务面板联动、输入延迟与重绘 |

边界：不做与 CAD 无关的效果（SSR、fog、sheen/anisotropy/subsurface/cloth 等高级材质特性后置）。

---

## 2. 设计架构（主要参考 Filament）

整体按 Filament 的分层来做：**离线材质编译 → 运行时装包/选变体 → 描述符式的逐 draw 数据 → pass/feature 组成的渲染器 → Filament 的 PBR/IBL 着色实现**。
下面按这四层写"参考了什么、当前实现到哪"。

### 2.1 材质与着色器：对标 `matc` / `resgen`

| Filament | 本项目 | 状态 |
| --- | --- | --- |
| `tools/matc` + `libs/filamat` | `Tools/MoonMatc`：解析 `.shader`（`material { }` + `vertex` / `geometry` / `fragment`），按（pass × feature 集）展开变体并打包 | 已实现 |
| `.filamat`（chunk 容器） | 引擎 shader 包，chunk：`SHDRSTAG` / `SHDRGLSL` / `SHDRVARI` / `SHDRPROP` / `SHDRATTR` / `SHDRPARM` / `SHDRSTAT` / `SHDRNAME` | 已实现 |
| `tools/resgen` | `Tools/ShaderPack`：源码打包进 `shader.cpp`，随可执行文件发布 | 已实现 |
| `MaterialParser` + `filaflat/DictionaryReader` | `ShaderManager` / `ShaderArchive` 的 chunk 读取；`Shader::GetVariant(pass, featureSet)` | 已实现 |
| `surface_main.vs/.fs` 的骨架 + `MaterialInputs` | MoonMatc 生成 vertex/fragment 骨架：MVP/世界坐标/varying、`initMaterial` → `material()` → `evaluateMaterial`；材质可自己写整段 stage 覆盖骨架 | 已实现 |
| 变体键（Variant bits）+ 材质特性（features） | `EVariant`：`DIRECTIONAL_LIGHTING` / `DYNAMIC_LIGHTING` / `SHADOWING` / `FOG` / `PICKING` / `SSR` / `DEPTH`；材质特性编译期固定为 `MATERIAL_HAS_*` | 已实现（`SHADOWING` / `SSR` / `FOG` 暂无使用方） |
| 参数元数据（label/group/min/max/step/color） | `.shader` 参数声明即面板 schema，材质面板/Actor 属性面板直接生成表单 | 已实现 |
| 参数的 uniform 块 / 每 renderable 块 | 描述符式绑定：`EngineUBO`(binding 0) / `ReflectionUBO`(1) / 材质参数块(2) / `ObjectUBO`(3)，动态 offset 来自共享 UBO 池 | 已实现 |
| 材质 DSL 的 `constants` / `variables` / `outputs`（MRT） | — | 未落地 |

构建期还有 `glslangValidator` 逐 stage 校验（`Tools/ShaderPack/ValidateGlslStages.cmake`），编译错误在出包阶段就暴露。

### 2.2 渲染器结构

- **Pass + Feature 的复合渲染器**：`CompositeRenderer` 按 `ERenderPassOrder` 顺序执行 pass；feature 以 `ALWAYS` 或
  `WHITELIST_ONLY`（`.Include<Pass>()`）挂到指定 pass 上，在 `OnBeforeDraw` 里写逐 draw 数据。当前 pass 链（按顺序值）：
  `Skybox(10001) → Gbuffer(19999) → Opaques(20000) → SectionCap(25000) → SectionContour(25001) →
  Transparents(30000) → HZB(35000) → PostProcess(40000) → Lines(40002)`（见 `ERenderPassOrder.h`）。
- **Descriptor 传递**：`SceneDescriptor`（场景/frustumOverride/材质覆盖）、`SceneFilteredDrawablesDescriptor`
  （opaques / transparents / lines 分桶 + 排序键）、`LightingDescriptor` 等，pass 只读描述符。
- **一次 draw 的路径**：`ARenderPass::Draw` → `CompositeRenderer::DrawEntity` → feature `OnBeforeDraw`
  （`EngineBufferRenderFeature` 写 per-renderable `ObjectUBO`、`LightingRenderFeature` 绑灯光 SSBO）→
  `Material::Bind` 按（pass, feature 集）选变体并上传参数块 → `Driver` 应用 `PipelineState` → 绘制。
- **状态管理**：`PipelineState` 打包成 bitset 放在 driver 里做差量应用；材质状态（blend/cull/depth）由
  `.shader` 的 `material { }` 声明，实例可覆盖。
- **剔除**：HZB 遮挡剔除（深度金字塔 + CPU 侧 BVH 分层测试，`HzbOcclusionCulling.md`）+ 视锥剔除，
  结果决定 `SceneFilteredDrawablesDescriptor` 的内容。

> 待做项（渲染线程、FrameGraph、材质系统剩余部分）集中写在 **2.6 对齐目标**。


### 2.3 光照与 IBL：对标 Filament 的 surface shading

- `Lighting/Filament/` 下已落地：`surface_common.glsl`、`surface_brdf.glsl`（GGX / Smith / Schlick、
  `D_GGX` 的 fp16 钳位、specularFactor / specularColorFactor）、`surface_lighting.glsl`（方向光/点光/聚光、
  Filament 的距离与角度衰减）、`surface_light_indirect.glsl`（IBL：diffuse irradiance / prefiltered radiance /
  DFG LUT、specular AO、multiBounce）、`surface_shading_model_standard.glsl`、`surface_shading_parameters.glsl`。
- **IBL 资产**：`SkyboxRenderPass` 从 HDR 生成 irradiance / prefilter cubemap 与 128² DFG LUT；prefilter 的
  `omegaP` 用输出 base 宽度、mirror level 用 `baseLod`（与 Filament `IBLPrefilterContext::SpecularFilter` 同）。
- **材质侧**：`FilamentPBR.shader` 作为对齐/验证材质；CAD 实际用的 `GeomertySurface.shader` 已换成同一套
  Filament shading（保留 domain color / 边线 / clip / 透明深度剥离 / 法线贴图）。
- **色彩管线**：Filament ACES + triangle-noise dithering，sRGB/线性转换按材质与贴图分别解码。
- 残余差异与验证记录见 `docs/FilamentAlignmentTODO.md`。

### 2.4 深度与相机：对标 Filament 的 reversed-Z

- **reversed-Z**：`glDepthRange(1, 0)` + `glClearDepth(0)`，深度比较方向统一为 `GREATER` 系列；
  float32 深度缓冲，近平面随相机距离自适应。
- **透视相机没有远平面**：投影取 `far -> ∞` 的极限（与 Filament `FCamera::setProjection` 同），视锥也不再裁远；
  正交相机保留贴合场景的有限 far（线性深度下 `far-near` 就是精度预算）。详见 `docs/DepthPrecision.md`。
- 由此派生出一条硬约束：**任何手写 `gl_FragDepth` 的 pass 必须用近平面重建射线**，不能用远平面点
  （无限远平面下 `w = 0`，见 `docs/SectionRendering.md` §8）。

### 2.5 其他已落地的基础设施

- **批网格 / 拓扑显示**：按 Solid/Shell 分组的 face/edge/vertex 批网格 + CBatchMesh* 组件，支撑大模型的
  边线/顶点显示与拾取（`docs/BatchedMesh.md`）。
- **剖切与截面**：模板奇偶填充的实时截面 + 几何着色器截线；剖面线为屏幕空间固定周期
  （`docs/SectionRendering.md`）。
- **拾取**：独立 pick pass（颜色附件存 id + 世界坐标），不走 CPU 射线查询。
- **调试通道**：`MOON_CLIP_DEMO` / `MOON_SHOT` / `MOON_SECTION_DEBUG` 等环境开关，支持无鼠标复现与 A/B 截图。

---

### 2.6 对齐目标（待做，第 3 节计划表的来源）

从 Filament 借鉴、尚未落地的三项设计。每项写 **目标 / Filament 依据 / 我们的现状差距 / 验收标准**；
第 3 节的具体条目从这里拆。

#### 2.6.1 渲染线程：命令队列 + 资源句柄

**目标**

- 主线程只做**记录**，渲染线程做**回放**：主线程调用渲染逻辑 = 写命令到队列，渲染线程按顺序执行；
- **句柄即时、对象延迟**：主线程拿到的是资源句柄，句柄分配不阻塞；着色器编译、VBO/VAO/纹理创建这些
  GL 工作全部由渲染线程在它自己的上下文里按命令完成，不再占用主线程；
- **场景数据交接**：渲染线程不直接读主线程正在改的 `Scene`（快照或把场景变更也命令化）；
- **生命周期**：句柄在资源尚未创建时可用；销毁延迟到 GPU 不再引用（按帧号/fence 回收）；
- **代价要认**：命令队列会带来约一帧延迟，CAD 的连续拖拽/剖切要单独评估输入到画面的延迟。

**Filament 依据**（本地参考代码 `D:\Project\C++\filament`）

- `Engine` / `Renderer` 在主线程记录，`backend::Driver` 在渲染线程回放；
- `filament/backend/include/private/backend/CommandStream.h`、`CommandBufferQueue.h`
  （注释原文：*"A producer-consumer command queue that uses a CircularBuffer as main storage"*，
  内部 `Mutex` / `Condition` 做背压）、`DriverApi.h` / `DriverAPI.inc`（API 生成命令记录代码）。

**现状差距**

- 已经有**独立的共享 GL 上下文**（`QtGLContextHost`，`ViewerWidget::CreateRenderContext`），但绘制仍在
  GUI 线程的 `ViewerWidget::paintGL` 里；
- 没有命令队列，资源是裸指针（`Material*` / `Mesh*` / `Model*`），没有句柄层；
- 已经存在"跨线程发 GL 调用"的实例：STEP 导入在 JobSystem 工作线程里跑
  `Feature::makeDone()` → `CTopoShape::discretizationShape()` → `new Mesh` → 顶点缓冲上传
  （`Moon/io/io_occ_step.cpp` → `Moon/feature/Feature.cpp`）。这是**第一个要切的口子**：
  先只把"资源创建"命令化，再把整条 draw 提交搬走。

**验收标准**

- 主线程每帧不再出现 GL 调用（可用断言/跟踪检查）；
- STEP 导入期间主线程帧时间不抖（导入期间 p99 帧时间与空闲时差在实测确定的预算内）；
- 资源在渲染线程持有期间删除不出现 use-after-free（延迟回收生效）；
- 连续拖拽/剖切时，输入到画面的延迟在预算内（实测后定数值）。

#### 2.6.2 FrameGraph：依赖图 + attachment 复用（A + B 都做）

**范围（已确认 A、B 都做）**

- **A｜依赖与裁剪**：pass 声明自己读/写哪些资源，图负责无环校验、拓扑排序、裁剪不可达 pass；
  替代现在硬编码的 `ERenderPassOrder` 和"靠约定"读别处附件（截面/透明/HZB 都直接读 opaque depth）。
- **B｜attachment 复用与别名**：同一帧内按生命周期复用同一块存储；MSAA 主 FBO、深度剥离的 8 层 FBO、
  GBuffer/SSAO 的 FBO、HZB 金字塔这些特殊附件交给图统一创建/复用/销毁，而不是每个 pass 自己 new。

**Filament 依据**

- `filament/src/fg/`：`FrameGraph`、`FrameGraphPass`、`FrameGraphResources`、`PassNode`；
- 两类节点（Resource / Pass），三类边（resource→pass 读、pass→resource 写、resource→resource 子资源）；
  保证无环、可拓扑排序、**裁剪不可达节点**；
- pass 侧用 `builder.createTexture(...)` / `builder.write(res, Usage::...)` / `builder.declareRenderPass(...)`
  声明资源与附件，执行侧拿到 `FrameGraphResources` + `DriverApi`；
- 设计说明：`filament/docs_src/src_mdbook/src/notes/framegraph.md`。

**现状差距**

- pass 顺序硬编码在 `ERenderPassOrder`；
- 每个 pass 自己管 FBO：`TransparentRenderPass` 的 8 层 peel FBO、`GbufferPass` 的 gbuffer/ssao FBO、
  `HzbBuildPass` 的金字塔、主 MSAA FBO（`outputMsaaBuffer`）；
- 跨 pass 依赖是隐式的：`SectionCap` / `SectionContour` / `Transparents` / `HZB` 都直接用 opaque 的深度，
  谁先谁后、谁负责清深度都是约定。

**验收标准**

- 新增一个 pass 只需声明读写资源即可接入，不改 `ERenderPassOrder`、不改其它 pass；
- 临时 attachment 的数量与显存占用有前后对比（B 的收益要能测出来）；
- 一帧的依赖图可 dump/可视化，不可达 pass 自动跳过；
- 深度/模板这类跨 pass 共享的附件由图统一管理，清除时机不再靠人工约定。

#### 2.6.3 材质系统：剩余待做

已落地的部分见 2.1 的表（离线变体、材质 DSL、描述符式绑定、参数元数据）。剩余：

- **`constants` / `variables` / `outputs`(MRT)**：MoonMatc 尚不支持，Filament 的 matc 有；
- **后处理链与 PBR/IBL 的残余差异**：见 `docs/FilamentAlignmentTODO.md`；
- **shadowing**：`SHADOWING` 变体目前无使用方，`ShadowRenderPass` / `ShadowRenderFeature` 代码在但未接入。
  该项**暂不排期**（按用户决定先不做阴影）。

**验收标准**

- `constants` 声明的常量进包且被 shader 引用；`outputs` 支持至少两个颜色附件并在同一 pass 里落两张图；
- 与 Filament 的 A/B 清单全部通过（`FilamentAlignmentTODO.md` 待办清零）。

---

## 3. 计划表

> 条目待与用户逐条确认后填入；每条都要有"现状/问题、验收标准、优先级、状态"。
> 主要来源是 **2.6 的三条对齐目标**（渲染线程 / FrameGraph / 材质系统剩余项），
> 再按所在维度落到下面的表里（一条目标可能拆成多个条目，分别落在不同表）。

### 3.1 渲染效果

| # | 条目 | 现状 / 问题 | 验收标准 | 优先级 | 状态 |
| --- | --- | --- | --- | --- | --- |
| — | （待列） | | | | |

### 3.2 性能

| # | 条目 | 现状 / 问题 | 验收标准 | 优先级 | 状态 |
| --- | --- | --- | --- | --- | --- |
| — | （待列） | | | | |

### 3.3 交互

| # | 条目 | 现状 / 问题 | 验收标准 | 优先级 | 状态 |
| --- | --- | --- | --- | --- | --- |
| — | （待列） | | | | |

其他条目来源（供讨论时挑）：`docs/todo.md` 的"渲染"一节、`docs/FilamentAlignmentTODO.md` 的待办、
`docs/DepthPrecision.md`、`docs/HzbOcclusionCulling.md`、`docs/BatchedMesh.md`、`docs/mass-entity-render-perf.md`。

---

## 4. 工作方式

- **不擅自开工**：只有用户明确说"动手"才开始改代码；在此之前只做讨论、读代码、列计划。
- **先计划后实施**：条目进表、验收标准明确后再动手；动之前先说明要改哪些文件、为什么。
- **验证不靠"看起来一样"**：
  - 效果类：同一个文档、同一相机姿态做 A/B 截图（headless 通道 `Moon.exe <doc>` +
    `MOON_CLIP_DEMO` 自动剖切 + `MOON_SHOT` 存图；pass 级开关 `MOON_SECTION_DEBUG`）。
  - 性能类：Tracy 帧内各 pass 计时 + 场景统计（draw call / 三角形 / 剔除数）前后对比。
  - 交互类：实际操作复现路径 + 延迟/帧率数据。
- **不自动提交**：改动留在工作区，汇报改了什么、为什么、验证结果，等用户说"提交"再提交。
