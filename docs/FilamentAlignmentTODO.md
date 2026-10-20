# Filament 对齐进度与待办

> 记录时间：2026-10-20。工作区改动**尚未提交**。新开对话时先读本文件，再按下面的“待办”继续。
>
> 项目定位：CAD 建模与渲染，渲染侧参考 Filament。SSR、fog、sheen/anisotropy/subsurface/cloth
> 等高级效果按用户决定**后置**，当前优先保证基础 PBR/IBL、材质与贴图链路。
>
> 最终目标：`FilamentPBR.mat` 只作为对齐/验证材质；CAD 几何实际使用的 `GeomertySurface.mat`
> 逐步吸收同一套 Filament shading，迁移计划见下方“GeomertySurface 迁移”。

## 已完成（当前状态）

- 变体系统：`EVariant` 7 位（DIR/DYN/SHADOW/FOG/PICKING/SSR/DEPTH）、MoonMatc keyed 变体、`SHDRFEAT`、`--features`、`defaultFeatures`；材质 feature 编译期固定，设计师勾选触发重编译。
- 顶点属性：`.mat` 的 `vertexAttributes` + `HAS_ATTRIBUTE_*`，Standard/CAD/FilamentPBR 等已迁移。
- PBR/IBL 核心：GGX / Smith / Schlick / DFG LUT / 能量补偿 / prefilter LOD / SH9 漫反射 / specular AO / multiBounce AO / clear coat（直接光 + IBL）/ reflectance / transmission / refractionIndex / thickness / specular AA（normalFiltering）。
- 后处理：Filament ACES + triangle-noise dithering。
- 预览：Studio.hdr IBL、sun（强度 1.0、方向 -0.35,-0.85,-0.55）、曝光 1.2、背景 0.032、相机 3/4 姿态 (1.5,0.82,3.65)；Reset View 按钮已实现并由用户确认正常（2026-10-20）。
- 环境 cubemap 方位：已修并**目视确认**（2026-10-20，用户确认“正常了”）；结论与验证见下方“环境 cube 方位”。
- Filament shader 补齐（2026-10-20）：`D_GGX` 的 fp16 `min(..., 453.5)`；`specularFactor` /
  `specularColorFactor`（F0/F90 与 IBL DFG）；`absorption`（Beer-Lambert，折射路径）；
  点光/聚光的 Filament `getDistanceAttenuation` / `getAngleAttenuation`（仅 FilamentPBR；
  Standard 的共享 `Light.glsl` 未动）。MoonMatc 128 variants、ShaderPack 的
  `glslangValidator` 校验和完整 `Moon` 构建均通过。新参数已进 `FilamentPBR.mat` / `.ovmat`，
  面板会自动出现；参考侧尚未暴露，视觉 A/B 见待办 1。点光/聚光只有有限 effect range 用
  Filament 公式，`range == 0` 保留引擎的 inverse-polynomial（preview rig 的 constant fill），
  这是唯一的例外。
- Qt-Filament 参考工程（`D:\Project\C++\Qt-Filament`）：材质面板已对齐 Base Color / Metallic / Roughness / Reflectance / Clear Coat / Clear Coat Roughness / Transmission / Refraction Index / Thickness 及六张贴图槽；`aiDefaultMat.mat`（samples + filamentapp）已同步，运行时包会重新生成。

## 环境 cube 方位（2026-10-20，已确认）

- Filament 的 IBL cubemap 按约定 **在 X 轴上镜像**存储（Filament 手册 “Mirroring”；
  `IBLPrefilterContext::EquirectangularToCubemap` 默认 `mirror = true`，`cmgen` 默认也镜像）：
  cube 中方向 `(x,y,z)` 的纹素来自 equirect 的 `(-x,y,z)`。Qt-Filament 的反射和 diffuse
  都用这个镜像 cube。
- 旧实现：`SkyboxConvert.mat` 用 `u = (π + atan2(z,x))/2π`，把 +Z 内容放进 +X 面（相对
  Filament 的镜像 cube 表现为一次 90° 方位旋转）；`computeIrradianceSH` 受 stb 全局垂直
  翻转影响，第 0 行按顶行处理（上下颠倒），且没有跟着做 X 镜像。
- 修正：`SkyboxConvert.mat` 用 `u = 0.5 + atan2(-x,z)/2π`、`v = 0.5 + asin(y)/π`；
  `computeIrradianceSH` 显式 `stbi_set_flip_vertically_on_load(true)` 并把第 0 行映射到 -Y，
  basis 的 x 取 `-x`。prefilter/irradiance 的面方向公式本来就和 Filament `iblprefilter.mat`
  一致，未改。
- 验证：`Build/_skytest/SkyID.hdr`（颜色编码方向）+ `cmgen --extract` 参考 cube：旧公式
  每面最大误差 71°–91°、平均 49°–80°；新公式最大 1.4°、平均 <1°；SH 与 cmgen cube 直接
  积分的相对差 0.7%。
- 注：Filament 的镜像约定意味着可见 skybox 本身也是镜像的。若以后要让天空和“真实”equirect
  一致而不再和 Filament 反射一致，把 `SkyboxConvert.mat` 的 `-d.x` 改回 `d.x`，并同步去掉
  SH 的 x 负号（两处必须一起改）。

## 待办（按优先级）

1. **新增材质参数的视觉 A/B（小项）**
   - `u_SpecularFactor` / `u_SpecularColorFactor` / `u_Absorption` 需要 Qt-Filament 的
     `aiDefaultMat.mat` 和面板先暴露，再做和参考的对比；Moon 侧默认值中性，正常材质外观不变。
2. **材质 DSL 机制**
   - `constants` / `variables` / `outputs` 声明尚未支持（Filament 有）。
   - 多输出 MRT、postProcess 之外的 domain 对齐。
3. **贴图链路验证**
   - Qt-Filament 侧贴图用 stb 加载，Albedo/Emissive sRGB、其余 RGBA8；Normal 需要切线。用 brick/metal 贴图对比 UV、通道、法线。
4. **收尾**
   - 删除 `Build\bin\Release\Moon_stale*.exe`。
   - 全量构建验证：`cmake --build Build --target Moon --config Release -- /nologo /m:1`。
   - 工作区改动较多，提交需用户明确说“提交”。

## 暂缓 / 后续（高级效果，CAD 优先级低）

- **SSR**：做法已讨论（Filament 的 forward SSR variant + 上一帧 HDR history + 当前 G-buffer
  structure + 主 shader 按 `Fr * (1 - a) + E * ssr.rgb` 混合）。Moon 目前只有 `EVariant::SSR`
  位和 `FilamentPBR.mat` 的 variantFilter，没有 pass / shader；等 CAD 基础链路稳定后再做。
- **fog**：未接；变体位已有，shader 未用。
- **sheen / anisotropy / subsurface / cloth**：独立的 BRDF / shading model，需要新的 material
  inputs 和变体，工作量大，后置。
- 点光/聚光的无限 range 例外见“已完成”；不是待办。

## GeomertySurface 迁移（最终目标，分阶段）

- 角色：`FilamentPBR.mat` 是对齐 Filament / Qt-Filament 的参考材质；CAD 拓扑面实际走
  `GeomertySurface.mat`（几何 shader 做边线内缩、domain color、裁剪、alpha、SSAO、透明深度
  peel、normal mapping、内置 tonemap 等）。最终把 FilamentPBR 的 shading 搬进
  GeomertySurface，而不是让 CAD 直接换用 FilamentPBR。
- 迁移原则：复用 `Lighting/Filament/*` 的 `FilamentMaterialInputs` / `preparePixelParams` /
  `evaluateFilamentSurface`；GeomertySurface 自己的 CAD 逻辑全部保留，只替换
  `PBRLighting(...)` 这一段的 shading。
- 分阶段：
  1. **Phase 1（可回退）**：给 GeomertySurface 加 `FILAMENT_SHADING` feature；fragment 里组
     `FilamentMaterialInputs` 并调用 Filament shading，旧 `PBRLighting` 路径保留作 fallback。
     新增 `u_Reflectance` / `u_ClearCoat` / `u_ClearCoatRoughness` / `u_Thickness` /
     `u_SpecularFactor` / `u_SpecularColorFactor` / `u_Absorption`，默认值取中性
     （0.5 / 0 / 0 / 1 / 1 / vec3(1) / vec3(0)），不改变现有 CAD 外观。
  2. **Phase 2（保留 CAD 行为）**：把旧 IBL 里的 `ParallaxCorrect` / local probe 盒子裁剪补进
     Filament IBL 路径；决定 `SPECULAR_WORKFLOW` 的映射；对齐 ambient box/sphere 的 AO、
     ortho 视线、双面法线翻转、shadow receiver。
  3. **Phase 3（验证与切换）**：同一 CAD 场景做 feature on/off A/B；材质球参数对齐
     FilamentPBR / Qt-Filament；看几何 shader + Filament fragment 的编译时间/寄存器/性能；
     验收后把 `FILAMENT_SHADING` 设为默认并删除旧路径。
- 已知差异/坑：
  - 旧 `Lighting/IBL.glsl` 有 `ParallaxCorrect` 和 local probe 的 OBB 裁剪，当前
    `surface_light_indirect.glsl` 只用 `ubo_ReflectionProbeData.brightness`；直接切会丢
    reflection probe 的盒投影/局部限制。
  - `SPECULAR_WORKFLOW` 目前是 `metallic = 1 - specular`、`roughness = 1 - glossiness` 的近似，
    不是 Filament 的 specular-glossiness 模型；先保留映射，后面再决定是否做完整模型。
  - GeomertySurface 的 `vertexAttributes` 没有 TANGENT/BITANGENT，法线贴图走 derivative TBN；
    FilamentPBR 的 `fs_in.TBN` 分支不能直接复用。
  - `u_Metallic` / `u_Roughness` 的默认值两边不同（GeomertySurface 1.0/1.0，
    FilamentPBR 0.0/0.5）；迁移时不要改 CAD 材质在 `TopoShapeActor` 里设置的现有默认。
  - GeomertySurface 带 geometry stage，fragment 再加 Filament 全套会增大编译/寄存器压力；
    Phase 1 先看编译和帧时间，再决定是否给 clear coat/transmission 加 feature 编译期裁剪。

## 关键文件

- shader：`Resource/Moon/Data/Engine/Shaders/FilamentPBR.mat`、`Lighting/Filament/*.glsl`、`SkyboxConvert.mat`、`SkyboxPrefilter.mat`、`SkyboxIrrandiance.mat`、`PostProcess/Tonemapping.mat`
- 天空盒/SH：`MoonRender/src/Core/Rendering/SkyboxRenderPass.cpp`
- 预览：`MoonRender/src/Rendering/View/PreviewView.cpp`、`Moon/editor/UI/MaterialPanel/MaterialPreviewWidget.cpp`、`MaterialEditorPanel.cpp`
- 参考工程：`D:\Project\C++\Qt-Filament`（`test/core/MaterialPreview.cpp`、`test/resource/shaders/DefaultMaterial.mat.txt`、`filament/samples/materials/aiDefaultMat.mat`）
- 参考源码：`D:\Project\C++\filament\out\shaders\minified\*.fs`（surface_brdf / surface_shading_lit / surface_light_indirect / surface_shading_model_standard）
- 方位测试（临时，未入库）：`Build/_skytest/SkyID.hdr` 方向编码 HDR、
  `Build/_skytest/skyid.py`（生成/读 HDR）、`Build/_skytest/verify_orientation.py`
  （对比 `cmgen --extract` 的参考 cube 与 shader 公式）、`Build/_skytest/ref/` 参考面。

## 调试原则

- 先加日志/复现，再改代码；不要靠“看起来一样”下结论。
- 对比截图前统一：IBL Rotation 0、同 HDR、同材质参数、同相机姿态。
- 环境方位别再靠肉眼猜：用 `SkyID.hdr`（颜色 = 方向）跑 `cmgen --extract`，再按 shader 公式
  重采样比对；能直接量出 90°/180°/镜像。
