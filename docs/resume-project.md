# MoonRender 简历项目部分（草稿）

> 本文由代码库实测数据整理，用于简历/面试材料的项目描述。
> 数据来源：`git log`、仓库文件统计、`docs/` 下 18 篇设计文档。
> 最后核对时间：2026-09-17（HEAD `3fc7efe3`）

---

## 0. 事实核对表（写简历前先确认这些）

| 项 | 实测值 | 出处 |
| --- | --- | --- |
| 项目周期 | 2022-12-04 起，持续至今（约 3 年 9 个月） | `git log --reverse` |
| 提交数 | 694 | `git log --oneline \| wc -l` |
| CMake 目标 | 4 个：`Moon`(编辑器)、`MoonRender`(渲染引擎)、`MoonGeometry`(OCC 几何层)、`MoonTracer`(离线路径追踪) | 根 `CMakeLists.txt` |
| 语言/标准 | C++20（MSVC v143 / VS2022） | `CMakeLists.txt:6` |
| 代码量（不含 `Extern`） | 约 22.9 万行：Moon 15.8 万（含 vendored ImGui/ImPlot 7.8 万，自研约 8 万）、MoonRender 2.6 万、MoonGeomerty 4.9 万、MoonTracer 7.4 万 | 逐文件行数统计 |
| 着色器 | 57 个（`.ovfx` / `.ovfxh`） | `Resource/Moon/Data/Engine/Shaders` |
| 设计文档 | 18 篇、约 350KB（含原理推导、时序图、实测数据） | `docs/` |
| 渲染 Pass 槽位 | 14 个（Shadows→Skybox→Reflections→Opaque→SectionCap→SectionContour→Transparent→HzbBuild→PostProcess→PathTrace→UI→Debug） | `ERenderPassOrder.h` |
| 绘制工具 | 16 种工具 / 20 个 Handler | `Moon/Interactive/Widgets/DrawSketchHandler*.cpp` |
| 交互控件 | 14+（ViewCube、ClipPlane、RotateCenter、PadTaskWidget、Measurement、Primitive* 等） | `Moon/Interactive/Widgets/` |
| 建模特征 | 7 类：Pad/Pocket/Revolve/Groove/Thickness/Fillet/Chamfer | `Moon/feature/` |

### 来源归属（**面试前必须想清楚怎么讲**）

这个项目不是"从零手写"，其中三块有明确的上游来源，简历表述要与之匹配：

| 模块 | 来源 | 证据 |
| --- | --- | --- |
| 渲染引擎 `MoonRender` | 架构源自开源引擎 **Overload**（OvRendering） | HAL 头文件仍有 `#include <OvRendering/HAL/None/NoneVertexBuffer.h>`；注释 "inherits from OvRendering Material"；`.ovfx`/`.ovmat` 资源后缀 |
| 几何层 `MoonGeomerty` | 基于 **FreeCAD Part** 改造（TopoShape / ElementMap / planegcs） | `MoonGeomerty/TopoShape.*`、`ElementMap.*`、`Moon/Sketcher/planegcs/` |
| 离线追踪 `MoonTracer` | 基于 **AGZ / Atrc**（AirGuanZ）改造，含 ReSTIR 实现 | `MoonTracer/tracer/src/core/renderer/restir*.cpp`、`docs/API.md` 自述 |

**建议的自述口径**：*"在开源渲染引擎架构上做二次开发与深度改造，自研实现了 …"*，
重点放在下面「实际由我完成」的部分（HZB、深度精度、剖面、PBO 回读、材质状态缓存、
Widget/拾取体系、CAD 全链路），而不是宣称"从零手写引擎"。

---

## 1. 标准版（推荐，用于简历主体项目）

**MoonRender｜面向 CAD 的实时渲染引擎与参数化建模系统**　2022.12 – 至今
个人独立项目（全部模块）｜C++20 / Qt5 / OpenGL 4.5 / GLSL / OpenCASCADE 7.9 / CMake / MSVC / Dear ImGui
代码：github.com/271812697/MoonRenderToy

- **整体**：独立开发并持续迭代 3 年 9 个月（694 commits），搭建 4 个 CMake 目标——编辑器、
  渲染引擎、OCC 几何层、离线路径追踪器；自研主体约 11 万行，配套 57 个 GLSL 着色器与
  18 篇设计文档（含原理推导与实测数据）。

- **HZB 遮挡剔除**：以上一帧深度构建 R32F 深度金字塔（逐级 2×2 取最远、≤64×64 网格），
  配合 BVH 分层节点测试（AABB→屏幕 tile 最远深度比较）做遮挡剔除；在 2 万+ 实例的 CAD
  装配上剔除 1 万+ 实例，并通过 tile 扫描提前退出、遍历栈复用、容器预留将 CPU 侧剔除
  耗时从 ~4ms 压低；实例以 (Mesh*, actorID) 为键，避免复用网格的标准件被整批误剔。

- **异步深度回读**：把每帧同步 `glReadPixels` 改为 3 槽 pixel pack buffer + `glFenceSync`
  非阻塞取回，CPU 不再等待 GPU 完成整帧（实测稳态 pending 1 槽 / 延迟 1 帧），并为
  HAL 补齐 `PIXEL_PACK`、映射与 fence 能力。

- **深度精度治理**：针对大尺寸模型下相机移动时的 z-fighting 与线面冲突，同时引入动态
  近远平面（near/far 比从 1:10⁶ 收紧到 ~1:200）与 Reversed-Z（反转深度范围、GREATER
  深度测试、清屏为 0），并连带修正 polygon offset 方向与拾取深度错位。

- **剖切截面渲染**：用模板奇偶填充绘制截面（cap），用几何着色器逐三角形求交绘制截线
  （contour），解决"平面上某点是否在实体内部"的拓扑判定；早期双深度比较方案失败后
  改用模板方案并记录了失败原因。

- **实时渲染特性**：PBR + IBL（天空盒/辐照度/预过滤/BRDF LUT）、G-Buffer + SSAO、
  方向光阴影、depth peeling 多层透明（PingPong 逐层合成）、后处理链
  （Bloom/FXAA/Tonemap/AutoExposure），并实现 MSAA 深度解析供给遮挡剔除使用。

- **渲染性能工程**：按材质排序绘制列表 + 材质状态签名缓存（避免重复 `glUseProgram` 与
  uniform 上传）、三角形/线段批渲染、场景 BVH 服务、多线程 JobSystem；接入 Tracy 做
  CPU/GPU 剖析，并以屏幕叠加层暴露剔除/回读/帧时间等运行时统计。

- **参数化建模与草图**：实现 Pad/Pocket/Revolve/Groove/Thickness/Fillet/Chamfer 七类
  特征，参数变化时实时重建差异几何并半透明预览；移植并接入 FreeCAD planegcs 约束求解器，
  实现 16 种草图绘制工具、几何删除联动清理约束、拖拽 DogLeg 增量求解与约束-几何-参数
  三方映射。

- **拓扑命名**：移植并改造 FreeCAD 的 TopoShape/ElementMap 体系，为参数化特征的子元素
  （面/边/顶点）提供跨重算的持久标识，解决改参数后特征引用失配的问题。

- **交互与拾取体系**：自研 Im3D 立即模式渲染层 + 事件回调层 + 屏幕恒定尺寸缩放，
  实现 ViewCube、剖切平面、旋转中心、拉伸长度/角度手柄等 14+ 控件（各带完整状态机与
  高亮反馈）；用 ID 颜色编码渲染到离屏 FBO 实现 actor 级与 STEP 面/边级拾取，并与层级树、
  属性面板双向联动。

---

## 2. 精简版（一页简历用，4 条）

**MoonRender｜CAD 实时渲染与参数化建模系统**（个人项目，2022.12 – 至今｜C++20 / Qt / OpenGL / OpenCASCADE）

- 独立开发并迭代 3 年 9 个月（694 commits），构建 4 个 CMake 目标（编辑器 / 渲染引擎 /
  OCC 几何层 / 离线路径追踪），自研主体约 11 万行 + 57 个 GLSL 着色器 + 18 篇设计文档。
- 自研 **HZB 遮挡剔除**（上一帧深度金字塔 + BVH 分层 tile 测试 + PBO 三槽环形异步回读），
  在 2 万+ 实例的 CAD 装配上剔除 1 万+ 实例，并消除每帧 `glReadPixels` 的 GPU 同步 stall。
- 攻克大规模模型深度精度问题：动态近远平面 + **Reversed-Z** 组合，解决 z-fighting 与
  线面冲突；实现**剖切截面渲染**（模板奇偶填充 + 几何着色器求交截线）与 depth peeling 透明。
- 打通 CAD 建模全链路：7 类参数化特征、16 种草图工具、planegcs 约束求解、TopoShape/
  ElementMap 拓扑命名，以及基于 ID 颜色编码的面/边级拾取与交互 Widget 体系。

---

## 3. 详细版（技术自述 / GitHub README / 面试展开用）

### 3.1 渲染管线与效果

| 主题 | 实现要点 | 代码/文档 |
| --- | --- | --- |
| Pass 编排 | 14 个有序 Pass 槽位，`CompositeRenderer` 统一调度，Pass 可独立开关对比 | `ERenderPassOrder.h`、`SceneRenderer.cpp` |
| PBR / IBL | 天空盒、辐照度卷积、预过滤环境贴图、BRDF LUT | `Lighting/PBR.ovfxh`、`SkyboxRenderPass` |
| G-Buffer / SSAO | 延迟 G-Buffer + 深度感知 SSAO（可开关） | `GbufferPass.cpp`、`SsaoRenderFeature.cpp` |
| 透明 | depth peeling 多层剥离 + PingPong 帧缓冲逐层合成 | `SceneRenderer.cpp`、`PingPongFramebuffer.cpp` |
| 阴影 | 方向光 light space 矩阵 + 阴影贴图 | `ShadowRenderPass.cpp`、`ShadowRenderFeature.cpp` |
| 后处理 | Bloom / FXAA / Tonemapping / AutoExposure，栈式组织 | `Core/Rendering/PostProcess/` |
| GPU 路径追踪 | fragment shader 路径追踪 Pass（编辑器内实时预览） | `PathTraceRenderPass.cpp` |
| 离线路径追踪 | 独立可执行文件，JSON 场景驱动，OIDN 降噪，含 ReSTIR 相关实现 | `MoonTracer/` |

### 3.2 性能优化路径（有数据可讲的部分）

1. **剔除**：场景 BVH（手动/自动构建）→ 视锥剔除 → HZB 遮挡剔除；
   CAD 装配 2 万+ 实例剔除 1 万+，CPU 剔除 ~4ms → 优化后（tile 提前退出、栈复用、reserve）。
2. **回读**：同步 `glReadPixels` → PBO 3 槽环形 + fence，稳态延迟 1 帧、CPU 零等待；
   调试叠加层暴露 `pending / skipped / latency` 便于判读 GPU 是否落后。
3. **状态与提交**：材质键排序使同材质绘制连续，材质状态签名缓存跳过重复 program 绑定与
   uniform 上传；三角形/线段批渲染降低 drawcall。
4. **并行**：JobSystem（`Dispatch` + group 语义，类 compute 分发模型）用于 STEP 导入与
   几何处理；几何加载走离线转换的二进制中间格式。

### 3.3 CAD 建模链路

- **草图**：16 种工具 / 20 个 Handler（点、线、多段线、圆、圆弧、椭圆、多边形、腰形槽、
  圆弧槽、BSpline、矩形、旋转、对称、裁剪、圆角、偏移），吸附与构造线模式、连续绘制。
- **约束**：移植 FreeCAD planegcs（GCS），支持重合/水平垂直/平行/垂直/相切/相等/对称/
  尺寸类约束；几何删除时联动清理；拖拽走 DogLeg 求解。
- **特征**：7 类特征 + 实时预览（差异几何、半透明品红）+ 任务对话框 + 参数属性系统。
- **拓扑命名**：TopoShape + ElementMap（MapElement）持久标识，参数重算后子元素引用仍可解析。
- **数据交换**：STEP/IGES/OBJ/GLTF 导入；离线工具把 STEP 转自研二进制格式（按 domain 拆分），
  运行时直接反序列化建场景。

### 3.4 编辑器与交互

- **3D 交互控件**：`EventWidget` 基类 + `RenderWindowInteractor` 事件分发 + 屏幕恒定尺寸
  缩放；命中方式两套（拾取 Pass 的颜色 ID / CPU 射线 `hitFace`、`hitPoint`）；
  控件自带状态机（Stop / Hot / 轴向拖拽 / 旋转）。
- **2D 覆盖层**：`ScreenWidget` 屏幕空间解析命中，与 3D 控件共享事件层，并处理
  "2D 与 3D 抢同一次点击"的仲裁。
- **拾取**：ID 颜色编码 → 离屏 FBO → 回读，支持 actor 级与 STEP domain（面/边）级，
  点选/框选/悬停高亮，与层级树、属性面板联动。
- **UI**：Qt Widgets（属性/设置/日志/层级树/自定义 Dock）+ Dear ImGui 1.93（FreeType
  字体、自研 Qt 后端、ImPlot），视口覆盖层与调试面板走 ImGui，正在推进整体迁移。

---

## 4. 面试可深挖清单（每个都能讲 5 分钟以上）

| 主题 | 一句话切入 | 证据位置 |
| --- | --- | --- |
| 为什么 Reversed-Z + 动态近远平面 | float32 的精度分布 + 深度非线性，near/far 比从 1:10⁶ → 1:200 | `docs/DepthPrecision.md` |
| HZB 判据方向 | Reversed-Z 下金字塔取 min、物体取最近角点，`tileMin > objNear + bias` | `docs/HzbOcclusionCulling.md` §2 |
| PBO 环形缓冲 | 为什么同步回读必然 stall、fence 何时 signal、槽位满了怎么办 | `HzbBuildPass.cpp`、docs §5.4 |
| 剖切截面为什么用模板 | 双深度方案为何失败、奇偶规则对开口网格的边界 | `docs/SectionRendering.md` |
| 拓扑命名 | 为什么需要持久标识、ElementMap 的 hash/编码方案、重算后如何解析 | `MoonGeomerty/ElementMap.cpp` |
| 约束求解 | planegcs 的子问题划分、DogLeg 与 BFGS 选择、拖拽时的增量更新 | `docs/SketchConstraints.md` |
| 材质状态缓存 | 签名由什么组成、为什么能跳过 bind/上传、跨帧失效风险 | `Material.cpp`、`ABaseRenderer.cpp` |
| 交互控件体系 | 事件分发链、两套命中方式的取舍、屏幕恒定尺寸的推导 | `docs/InteractiveWidget.md`、`docs/ScreenWidget.md` |

---

## 5. 表述红线（别写的话）

- ❌"从零手写渲染引擎"——HAL/Pass 架构源自 Overload，会被识破。
- ❌"自研约束求解器"——求解器是 FreeCAD planegcs，你做的是移植、集成与数据模型对接。
- ❌"自研路径追踪器"——`MoonTracer` 基于 AGZ/Atrc，你做的是接入、场景描述与扩展。
- ❌"实现了物理引擎 / 网络 / 材质编辑器"——仓库里没有这些。
- ⚠️ 数量类描述统一用"约"（代码行数、控件数），并且面试时要能说清统计口径。

---

## 6. 可选加分项（如果简历版面够）

- **文档能力**：18 篇设计文档含原理推导、时序图与实测数据（可直接作为附件/作品集）。
- **工程化**：CMake 多目标构建、Extern 依赖自构建、Tracy 性能剖析接入、GL 调试回调定位
  （`GL_INVALID_OPERATION` 一类问题的排查过程可讲）。
- **重构能力**：编辑器从 Qt 向 ImGui 迁移的过程（渲染器与 UI 解耦、事件层复用）。
