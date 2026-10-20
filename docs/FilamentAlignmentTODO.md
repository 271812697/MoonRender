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
- GeomertySurface 迁移（2026-10-20）：CAD 实际材质已把 `PBRLighting` 换成 Filament shading，
  参数集对齐 FilamentPBR；domain color / 边线 / clip / 透明深度 peel / `u_UseNormalMap`
  derivative TBN 保留。旧的 alpha clipping/dither、SSAO、UV tiling/offset、mask、内置
  tonemap/gamma、SPECULAR_WORKFLOW / Specular / Glossiness 已删除，后续再单独对齐。
  MoonMatc 8 variants，完整 `Moon` 构建通过。迁移前检查点提交为 `3c451d9d`。
- 材质参数元数据（2026-10-20）：`FilamentPBR.mat` / `GeomertySurface.mat` 的 UI 参数补齐
  `label` / `group` / `min` / `max` / `step` / `color`；颜色参数（`u_Albedo`、`u_EmissiveColor`、
  `edgeColor`、`u_SpecularColorFactor`、`u_Absorption`）显式标 `color : true`，引擎内部纹理/
  uniform 标 `ui : false`。MaterialDesigner 和 Actor Property 面板都改为读元数据，删掉了
  按参数名猜颜色的 `IsColorParameter`，并支持 `vec3` 颜色选择器。
- 默认 skybox 对齐（2026-10-20）：`SkyboxRenderPass` 的默认 `mPath` 从 `PureSky.hdr` 改为
  `Studio.hdr`，新场景的 IBL 和 Material Preview/FilamentPBR 预览使用同一张 HDR；项目或
  Setting 面板仍可覆盖。Reference Lighting 临时模式已移除（默认 IBL 已经一致）。
- GeomertySurface 默认参数（2026-10-20）：使用截图里的 CAD 材质基线：Metallic `0.8`、
  Roughness `0.4`、Reflectance `0.42`、Clear Coat `1.0`、Clear Coat Roughness `0.53`、
  Specular Factor `0.0`、Specular Color `#ffcfbf`、Thickness `10.0`、Refraction Index `2.94`、
  Emissive Intensity `1.0`、Emissive Color 黑、Transmission `0.0`；`TopoShapeActor` 不再覆盖
  metallic/roughness/emissive。Actor Property 的材质表单顶部有 **Reset to Defaults**，按 shader
  参数表重新初始化所有参数（保留 feature 和 renderer state）。
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

1. **GeomertySurface 新 shading 的 CAD 回归**
   - 在真实 CAD 视图里确认：domain color、边线（WITH_EDGE）、clip plane、透明深度 peel
     （Transparents）、`u_UseNormalMap` 法线贴图、正交视图、双面法线翻转。
   - alpha clipping/dither、SSAO、UV tiling/offset、mask、内置 tonemap/gamma 已移除，
     后续再单独对齐，不在这轮回归范围。
   - **多光源**：场景里放多个 point/spot/directional，确认都参与累积；`SHADOWING` 目前没开，
     CAD face material 的 `receive_shadows` 也一直是 false，需要阴影时再单独加 variant/define。
   - reflection probe 的 `ParallaxCorrect` / local OBB 裁剪目前还没接进 Filament IBL；如果场景
     里启用了 probe，需要先接受“无盒投影/局部限制”的退化，或补上。
2. **新增材质参数的视觉 A/B（小项）**
   - `u_SpecularFactor` / `u_SpecularColorFactor` / `u_Absorption` 需要 Qt-Filament 的
     `aiDefaultMat.mat` 和面板先暴露，再做和参考的对比；Moon 侧默认值中性，正常材质外观不变。
3. **材质 DSL 机制**
   - `constants` / `variables` / `outputs` 声明尚未支持（Filament 有）。
   - 多输出 MRT、postProcess 之外的 domain 对齐。
4. **贴图链路验证**
   - Qt-Filament 侧贴图用 stb 加载，Albedo/Emissive sRGB、其余 RGBA8；Normal 需要切线。用 brick/metal 贴图对比 UV、通道、法线。
5. **收尾**
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

## GeomertySurface 迁移（2026-10-20 已直接替换，待 CAD 视图验证）

- 角色：`FilamentPBR.mat` 是对齐 Filament / Qt-Filament 的参考材质；CAD 拓扑面实际走
  `GeomertySurface.mat`。用户要求把 FilamentPBR 的 shading 直接搬进 GeomertySurface。
- 已做：`GeomertySurface.mat` 的 `#include PBR.glsl` + `PBRLighting(...)` 已替换为
  `#include Lighting/Filament/surface_lighting.glsl` + `preparePixelParams` /
  `evaluateFilamentSurface`。新增 `u_Reflectance` / `u_ClearCoat` / `u_ClearCoatRoughness` /
  `u_Thickness` / `u_SpecularFactor` / `u_SpecularColorFactor` / `u_Absorption`，默认中性。
- 多光源：`evaluateFilamentSurface` 本身按 `ssbo_Lights.length()` 循环，支持 point / directional /
  spot / ambient box / ambient sphere。但 GeomertySurface 没有 `variantFilter`，引擎只给
  variant-keyed shader 加 `VARIANT_HAS_*`，所以点光/方向光/聚光的 case 原本会被编译掉（旧
  `PBRLighting` 同理）。现在在 fragment 顶部显式
  `#define VARIANT_HAS_DIRECTIONAL_LIGHTING` / `#define VARIANT_HAS_DYNAMIC_LIGHTING`，
  预处理后的 GLSL 里已有 `case 0/1/2`。`VARIANT_HAS_SHADOWING` 没有定义：CAD face material
  本来 `receive_shadows=false`，与迁移前一致；要阴影再单独加。
- 保留的 CAD 逻辑：domain color（`DomainId` + `domainColorTex`）、边线（`WITH_EDGE`）、
  clip plane、透明深度 peel（`Transparents` + `DepthPeelTex` / `OpaqueDepthTex`）、
  `u_UseNormalMap` derivative TBN、正交视线、双面法线翻转、emissive 的 sRGB 解码。
- 已移除、后续再对齐：alpha clipping/dither、SSAO、UV tiling/offset、mask、内置 tonemap/gamma、
  `SPECULAR_WORKFLOW` / `u_Specular` / `u_Glossiness` / `u_RoughnessMetallicMap`；
  `NORMAL_MAPPING` feature 已换成 `u_UseNormalMap` bool。
- 验证：MoonMatc 编译 GeomertySurface 8 variants（只剩 `WITH_EDGE` / `DISABLE_DOMAIN_COLOR`
  两个 feature）；完整 `Moon` 构建通过。提交检查点 `3c451d9d` 在迁移前。
- 已知差异 / 待做：
  - Filament IBL 路径还没有 `ParallaxCorrect` / local probe 的 OBB 裁剪（旧 `IBL.glsl` 有），
    启用 reflection probe 的场景会退化；需要时把旧函数接进 `surface_light_indirect.glsl`
    或给 GeomertySurface 单独包一层。
  - `normalFiltering` 现在也会作用到 CAD 面（和 FilamentPBR 一致），极高光/法线缝可能有轻微
    变化；如果 CAD 不需要，可加 feature 关掉。
  - ambient box/sphere 在 Filament 路径里乘 `pixel.diffuseColor`，旧 `PBRLighting` 是最后乘
    `ao`；CAD 场景如果用到 ambient light 要单独确认。
  - GeomertySurface 带 geometry stage，fragment 再加 Filament 全套会增大编译/寄存器压力；
    需要时给 clear coat/transmission 加编译期 feature 裁剪。

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
