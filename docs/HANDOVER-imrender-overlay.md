# 交接：ImRenderer / Overlay 恢复后的遗留问题

> ## 本次会话已解决（2026-11-24 晚，原判断被推翻）
>
> **根因不是 UBO / 参数块喂错，而是纹理描述符整批被丢掉。**
> `DescriptorSet::SetTexture()` 用 `p_texture != nullptr` 判断"这个槽有没有东西"，而
> `SetProperty(name, Resources::Texture*)` 走的是"资产名 + 句柄为空"的路径：句柄为空 → 整个
> descriptor 被写成空 → 采样器槽从没进过描述符集。
>
> 后果：所有"用资产设置贴图"的材质，采样器读的都是上一个 pass 残留在该 texture unit 上的贴图
> （视口大小的 scene 贴图）。立方体没有文字、立方体/坐标轴光照不对、坐标轴发黑，全部是这一个
> 原因；场景材质也一直在吃 1×1 白兜底贴图（修前/修后整帧 18.2% 像素不同，几何完全一致）。
>
> 修法（`MoonRender/src/Rendering/Data/DescriptorSet.cpp`，7 行）：
> `p_texture != nullptr || p_asset != nullptr` 才 `MakeTexture`，否则才是空槽。
>
> **第二个问题：2D overlay 整帧没画。** `AView::Render` 把 `p_drawOverlays` 存进
> `m_drawOverlays` 后从来没调用它（`1d2e4ec0` 重构时丢了那一段），所以
> `Im2DRender::EndFrame` 一次都没跑：导航方块周围的 6 个轨道/翻转图标、2D 控件、FPS 都缺。
> 已在 `PresentTo` 之后、commit 命令之前恢复调用（`MoonRender/src/Rendering/View/AView.cpp`）。
>
> 验证：单线程 vs 渲染线程 `0.203%`（= 同模式两次运行的 fit 抖动噪声底，位置一致），
> 无 GL 错误；立方体 左/前 文字、六个图标、三色轴、场景着色全部恢复，
> 截图 `Build/refactor-shots/final-single.png` / `final-thread.png`。
>
> 排查用的临时探针（overlay draw 的 UBO/采样器 dump、2D 列表统计）只用于定位，未留在代码里；
> 下次要复现这条路径，按上面的根因直接查 `DescriptorSet::SetTexture` 的空槽判断与
> `Im2DRender::EndFrame` 是否被调用即可。
>
> 下面"仍未解决"的第 1 条与"另一个被暂停的问题"仍然有效，其它内容作为历史记录保留。

基线：`b8071d7a`（HEAD）。工作区有 **5 个未提交文件**，均已编译并实跑验证：
`viewportframe.cpp`、`GizmoRenderPass.cpp`、`Im2DRender.cpp`、`Im3DType.cpp`、`Im3DRenderer.cpp`、
`ViewCubeLabels.cpp`（图集注册顺序修复）。

## 已完成

1. **恢复 overlay 帧调用**（`viewportframe.cpp`）：`BeginFrame` / `BeginGizmoFrame` + `BeginOverlay2D` /
   `EndFrame` / `EndOverlay2D` 全部解注释；`GizmoRenderPass.cpp` 的 `renderer.endFrame()` 恢复。
   这同时修好了 `ImRenderer::newFrame`/`endFrame` 的配对、STEP 模型的边/顶点悬停高亮、
   以及面拾取（pick pass 里的 `drawMeshPick()` 依赖 ImRenderer 每帧重置的几何列表）。
2. **2D overlay 的程序名**（`Im2DRender.cpp`）：`m_programName` 之前只在 `EnqueueCreateProgram` 之后读一次
   → 永远是 0 → `EndFrame` 整帧 return。现在每帧从 handle 刷新（首帧跳过）。
3. **两处"立即上传 HAL 纹理"改成命令流**（`Im3DType.cpp` / `Im3DRenderer.cpp`）：`GetTexture().Upload(...)`
   → `delete` 旧纹理 + `TextureLoader::CreateMutable(...)` 重新录一条带像素的创建命令。
4. **图集注册的静态初始化顺序**（`ViewCubeLabels.cpp`）：`g_atlasBuilder` 改成函数内静态（`Builder()`），
   避免编辑器静态注册被引擎静态构造清空。
5. 加了诊断：图集为空时打印一次 `[viewcube] label atlas empty: the editor painter is not registered`。
   实测**没有打印** → 图集有像素、纹理已创建。

## 仍未解决（下个会话的主线）

**视图立方体面上没有文字，而且立方体/坐标轴的光照不对、坐标轴发黑。**

用户的判断（已认同）：不是贴图问题，是 **`GizmoCell.shader` 对应材质的 UBO/参数块喂错了**。

重点怀疑两条：

1. **per-material UBO 切片与 `UniformBufferPool` 扩容的顺序**：
   `MaterialInstance` 的 `MaterialParams` 是池子里的一段（`Acquire` → `Slice{offset,size}`，
   `Descriptor` 存 `pool.GetHandle()` + offset/size）。池子扩容走
   `UniformBufferPool::EnsureCapacity` → `m_buffer->Allocate(capacity)`（`glNamedBufferData`，整块 orphan）
   再按 CPU 镜像重传。如果扩容的录制点夹在别的材质的 `UploadBuffer` 之后，那些数据随扩容被丢弃 →
   参数块全 0 → 光照/视口参数全错。
2. **绑定到了错误的块**：`MaterialParams` 与 per-view EngineUBO 的 binding 冲突，或 `mCellMaterial` 的
   descriptor 被换成了 EngineUBO 的 handle/offset。

### 一次性定位方法

在画立方体之前打印 `mCellMaterial.DefaultInstance()` 的 descriptor：
`binding / handleId / offset / size`，以及 `UniformBufferPool::Get().GetHandle()` 与当前 `capacity`。

- offset+size 超出 capacity，或 handle ≠ 池子当前 handle → 第 1 条坐实；
- handle 是 EngineUBO 的 → 第 2 条坐实。

相关代码：`MoonRender/src/Rendering/Data/UniformBufferPool.cpp`（`EnsureCapacity` / `GetHandle`）、
`MoonRender/src/Rendering/Data/MaterialInstance.cpp`（`EnsureDescriptorSet` / `UploadParameterBlock`）、
`MoonRender/src/Rendering/Data/Descriptor.cpp`（`Bind` / `Upload`）、
`MoonRender/src/Rendering/Overlay/Im3DRenderer.cpp`（`mCellMaterial->SetProperty("uModelMatrix"...)` 一带）。

## 另一个被暂停的问题（不要忘）

**直接光照不生效，只有间接光照**：用户说直接光数据在 SSBO 里，用的是 `GeomertySurface.shader`。
排查状态：
- 代码里**没有**任何 `SetBuffer(..., Descriptor::BufferType::ShaderStorage, ...)` 调用 → 灯光 SSBO
  不在任何描述符集里；
- 唯一绑灯光 SSBO 的是 `LightingRenderFeature::Bind()`（裸 `glBindBufferBase`，且该 feature 疑似死代码）；
- `GeomertySurface.shader` 本身没有 `std430` 声明（SSBO 块由 MoonMatc 注入），需要用户提供
  "灯光 SSBO 在哪里 Allocate/Upload、binding 是多少"。
- 猜测：裸绑定被后续命令绑到同一 indexed point 覆盖（旧的"紧贴 draw 前绑"在命令流里不再成立），
  或者干脆没人绑。修法：改成 per-view 描述符集的 SSBO。

## 验证配方

- 构建：`cmake --build Build --target Moon --config Release -- /nologo /m:1`（**不要** `/m`）。
- 跑：`QT_OPENGL=desktop`，`MOON_SHOT_DELAY_MS`、`MOON_SHOT`；`MOON_RENDER_THREAD=0` 为单线程。
- 比较：`Build/refactor-shots/compare-dots.py A.png B.png`；fit 动画按帧推进（598 帧），要等它跑完再比。
- 截图现状：`Build/refactor-shots/xg-thread.png`（模型/红点/立方体/三色轴正常，**立方体无文字**）。
