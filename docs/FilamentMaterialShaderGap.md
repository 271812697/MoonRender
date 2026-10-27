# Filament 材质 / 着色器对齐度盘点

> 时间：2026-10-24。方法：逐文件读代码，与本地参考 `D:\Project\C++\filament`（shaders/、libs/filamat/、
> libs/filabridge/、filament/src/）对照。**本文只做盘点，不含代码改动。**
>
> **进展（2026-10-24，批次 A 第一、二刀已落地）**：
> - `MATERIAL_HAS_*` / `MATERIAL_NEEDS_TBN` / `GEOMETRIC_SPECULAR_AA` / `CLEAR_COAT_IOR_CHANGE` 已由
>   MoonMatc 生成（扫描材质代码里写的 `MaterialInputs` 字段），JSON 里也反射出来了 → §1 的机制**已对齐**，
>   剩下的是让更多 shader 真的去吃这些 define（§12 批次 A3）。
> - `shadingModel : lit` 已能生成 `evaluateMaterial` / `evaluateSurface`（`Lighting/Filament/*` 作为 lit
>   预置，引擎采样器由 matc 注入参数表）→ §2 **已闭合**。`FilamentPBR.shader` 是骨架 form 的样板，
>   **`GeomertySurface.shader`（CAD 面）也已迁到 `lit`**（raw fragment，保留 domain color / 边线展开 /
>   clip / 深度 peel，改调 `evaluateSurface`）；`FilamentPBR.shader` 保留为 unlit 手写对照。
> - 骨架现在带**切线帧 varying**（`MATERIAL_NEEDS_TBN` 为 1 时声明 `mat3 TBN`，顶点阶段读
>   TANGENT/BITANGENT），`m.normal`（切线空间）由 `evaluateMaterial` 转到世界空间；退化切线帧回落到几何法线。
> - `MaterialInputs` 已按 `surface_material_inputs.fs` 补齐（含 guard），`initMaterial` 默认值一致，
>   unlit 的 `evaluateMaterial` 改成 Filament 的 `baseColor + emissive.rgb` 语义 → §3 的结构**已对齐**，
>   但 shader 里那套 `FilamentMaterialInputs` 还在（§12 A3 合并）。
> - 构建期新增 **程序链接校验**（`glslangValidator -l`，见 `Tools/ShaderPack/ValidateGlslStages.cmake`）：
>   "声明了但没定义"这类错误现在在构建期就失败，不用等到运行期第一帧 → §7 的语义分析缺口补了一小块。
> - **可选特性的 bool 全清了（迁到 lit 的两个材质）**：DSL 新增 `fallback : flatNormal`（`SHDRFALL` chunk +
>   运行期的 1×1 扁平法线纹理），法线贴图改成无条件采样；`MOON_DEBUG_SAMPLERS=1` 可以核对每个槽实际
>   绑到的是材质贴图还是哪张回退。→ §1 的"消费面"收口。
> - 变体模型（§4）、材质实例（§5）、光照/阴影/IBL（§9）未动。
>
> 前提：离线打包（MoonMatc ≈ matc、ShaderPack ≈ resgen）、chunk 容器 + 反射、参数块绑定这三层
> **已经对齐**，本文不再重复。下面的缺口按"设计骨架 → 语义 → 运行时"三层排列，每节给
> Filament 依据、我们的现状、判定与影响。
>
> 已知的**永久差异**（不要为了"对齐"抹掉）：GL 几何阶段、CAD 的 domain color / 边线展开 / 剖切 /
> 透明深度剥离、HZB 遮挡剔除。这些 Filament 本来就没有。

---

## 0. 速览

| # | 维度 | Filament | 我们 | 判定 |
|---|---|---|---|---|
| 1 | 编译期特化 `MATERIAL_HAS_*` | 29 个宏 / 149 处引用，由 matc 从材质代码推导 | 生成机制已落地（扫描字段 → define → JSON），lit 预置在吃；迁到 lit 的两个材质已无运行期 bool | ✅ 已对齐（冻结的 `FilamentPBR` 除外） |
| 2 | `shadingModel` | 生成 `evaluateMaterial` → `generateSurfaceLit` 拼整套 surface 预置 | `lit` 可用，`FilamentPBR`（骨架 form）与 `GeomertySurface`（raw form）都已迁 | ✅ 已对齐（骨架细节未全抄） |
| 3 | `MaterialInputs` 字段 | 25+ 字段，按 shadingModel / `MATERIAL_HAS_*` 裁剪 | DSL 侧已按 `surface_material_inputs.fs` 补齐；shader 里仍有一套手写 `FilamentMaterialInputs` | ⚠️ 结构对齐、两套共存 |
| 4 | 变体模型 | 8 bit 固定枚举 + 有效表 + per-stage 过滤 + 按需编译 | `variantFilter` 名字的 **2^n 幂集**，运行时按**字符串集合**选 | ⚠️ 部分 |
| 5 | 材质实例 | `Material`（定义）+ `MaterialInstance`（参数/状态/sampler 参数） | 一个 `Material` 既是定义又是实例 | ⚠️ 部分 |
| 6 | 参数 / 描述符 / 反射 | UIB + SIB + Property 位图 + `constants` | 参数块已对齐；无 `constants` / `outputs` / `variables` | ⚠️ 部分 |
| 7 | 着色器产物 | SPIR-V 为主，cross-compile 到 GLSL/MSL/WGSL；语义分析 | 直接出 GLSL 450 文本；构建期 **编译 + 链接**校验 | ⚠️ 有意收窄 |
| 8 | 着色模型覆盖 | lit / subsurface / cloth / specular-glossiness + custom | standard 的一个子集，且**三套实现并存** | ❌ 未对齐 |
| 9 | 光照 / 阴影 / IBL | LightsManager + 光度单位 + CSM/PCSS/VSM + IndirectLight | 位打包灯光、单张方向光 shadow map（未接入）、irradiance cube | ❌ 未对齐（已定后置） |
| 10 | 后处理 / 帧图 / 线程 | FrameGraph + ColorGrading + 渲染线程 | 固定 pass 顺序 + ACES/dither | ❌ 未对齐（`RenderingPlan.md` 2.6 已列） |

---

## 1. 编译期特化：`MATERIAL_HAS_*`

**Filament**：`MaterialBuilder` 用 glslang AST 扫描材质代码里对 `material.<field>` 的读写，得到
`PropertyList`（`sca/ASTHelpers.cpp` 的 `PropertyFinder`）；`ShaderGenerator::generateSurfaceMaterialVariantProperties`
把它变成 `#define MATERIAL_HAS_<PROPERTY>`（`libs/filamat/src/shaders/ShaderGenerator.cpp:209-232`、
`CodeGenerator::generateMaterialProperty` :1023）。后果是**每个材质编译出的 GLSL 只包含它真的用到的分支**：
没有 clear coat 的材质里没有 coat 代码，没有法线贴图的材质里没有 TBN 与 tangent 属性需求，采样器集合也按
property 裁剪。`shaders/src` 下 29 个不同的 `MATERIAL_HAS_*`、149 处引用都建立在这条链上。

**我们（2026-10-24 起）**：`MoonMatc` 已经生成这套宏。`WrittenInputFields`（`Tools/MoonMatc/src/Matc.cpp`）
锚定"声明为 `MaterialInputs` 的变量"，扫 `m.<field> = ...`（含 `.rgb`/`+=` 这类），
`MaterialInputsPrelude` 据此输出 `#define MATERIAL_HAS_<FIELD>`，外加 `MATERIAL_NEEDS_TBN`、
`GEOMETRIC_SPECULAR_AA`（来自 `material { specularAntiAliasing }`）、
`CLEAR_COAT_IOR_CHANGE`（来自 `clearCoatIorChange`，默认 true）；同一份列表进 `--dump-json` 的
`defines`。lit 预置已经在用它们（`#if defined(MATERIAL_HAS_CLEAR_COAT)` 等）。

**还没铺开的是消费者**：其余材质仍把"有没有这张图/要不要 AA"做成参数 + 运行时分支：

| 位置 | 现在的写法 | Filament 的写法 |
|---|---|---|
| `FilamentPBR.shader:30` | `u_HasAlbedoMap` (bool) | `MATERIAL_HAS_ALBEDO_MAP` / SIB 里没有该采样器 |
| `FilamentPBR.shader:35,38,46` | `u_HasMetallicMap` / `u_HasRoughnessMap` / `u_HasAOMap` | 同上（采样器按 property 裁剪） |
| `FilamentPBR.shader:55` | `u_SpecularAA` (bool) | `GEOMETRIC_SPECULAR_AA`（材质级，编译期） |

`FilamentPBR` 已经冻结为"迁移前的对照实现"，它上面的 bool 不再演进。**其它材质已经没有这类 bool 了**：
`FilamentPBR` / `GeomertySurface` 的色/金属/粗糙/AO 贴图**无条件采样**（未指定时绑白色 1×1，等于乘 1），
法线贴图也**无条件采样**——靠 DSL 的新字段 `fallback : flatNormal` 绑一张 (0.5,0.5,1) 的 1×1，
解码成法线空间的 (0,0,1)，所以 `u_UseNormalMap` 也删了。这就是 Filament 的做法，顺带修掉了
"`u_HasAlbedoMap` 从来没人赋值 → 这几张贴图一直没生效"的老 bug。

运行期可以用 `MOON_DEBUG_SAMPLERS=1` 核对每个槽拿到的是材质贴图还是哪张回退纹理。

影响：

1. 每个变体都带着所有分支，采样器槽位也全都留着（`#if` 掉的分支才会被驱动裁掉采样器）；
2. `EVariant.h:8-11` 的注释和 `RenderingPlan.md:41` 都写着"材质特性编译期固定为 `MATERIAL_HAS_*`"——
   与代码不符，属于**文档在描述一个没实现的设计**；
3. Filament 的 `MaterialBuilder` 还因此能校验"unlit 材质写了 clearCoat"这类语义错误，我们做不到（见 §7）。

**判定**：这是当前材质系统最大的设计空洞。做它需要三件事：MaterialInputs 补齐字段（§3）、matc 里做
字段使用分析、生成 define 并据此裁剪（§2 依赖它）。

---

## 2. `shadingModel`：`evaluateMaterial` 由预置生成

**Filament**：`surface_main.fs:38-63` 是固定骨架（`computeShadingParams` → `initMaterial` →
`material(inputs)` → `applyAlphaMask` → `evaluateMaterial` → postLightingColor → fog）；
`evaluateMaterial` 由 `ShaderGenerator` 按 `Shading` 选文件拼出来
（`generateSurfaceLit`，`ShaderGenerator.cpp:1151-1197`）：`surface_lighting.fs` + `surface_shadowing.fs` +
`surface_brdf.fs` + `surface_shading_model_*.fs` + `surface_ambient_occlusion.fs` +
`surface_light_indirect.fs` + `surface_light_directional.fs` + `surface_light_punctual.fs` +
`surface_shading_lit.fs`。**材质只负责填 inputs，光照由预置接管。**

**我们（2026-10-24 起，`lit` 已可用）**：

- `MaterialInputsPrelude` 对 `lit` 仍只**前向声明** `evaluateMaterial`，但 `BuildStage` 现在会在
  fragment 骨架之前调用 `LitSurfacePrelude()`（`Matc.cpp`）：
  `#include ":Shaders/Lighting/Filament/surface_lighting.glsl"` + 一个
  `evaluateMaterial(inout MaterialInputs m)` —— 它把 `MaterialInputs` 拷进 surface 栈自己的
  `FilamentMaterialInputs`，按 `MATERIAL_HAS_*` 决定拷哪些/给什么中性值，然后
  `preparePixelParams` + `evaluateFilamentSurface`，最后 `color += emissive`、alpha 取
  `m.baseColor.a`。这就是 `generateSurfaceLit` 的等价物；
- lit 材质需要的引擎资源（`_IrradianceCube` / `_PrefilterCube` / `_BRDFLut` / `_ShadowMap` /
  `_LightSpaceMatrix`）由 `InjectEngineGlobals` 自动加进参数表（`ui : false`），材质文件**不再声明**它们，
  运行时按名字照旧上传；
- `FilamentPBR.shader` 是第一个只用 `material()` 填输入、完全靠预置着色的材质。它的
  fragment 阶段没有任何光照代码，`FilamentPBR.shader` 是它的手写对照版（保留作 A/B 参考）。
- `GlslangValidator -l` 的**链接**校验已进构建：以前"声明了 `evaluateMaterial` 却没人定义"这种错误
  要等运行期第一帧，现在构建期就失败。

剩余：`FilamentPBR` 仍是 `unlit` + 手写 `main()`（保留作迁移前的对照，不再演进）；`Standard`（旧 PBR）
未动；fog / postLightingColor / applyAlphaMask / shadow 调试视图这些 `surface_main.fs` 的行为还没进骨架。
`m.normal` 的切线帧有了，但**引擎不给导入的模型生成切线**（只有 `ModelLoader` 的程序化网格调
`GenerateTangents`），所以普通模型的法线贴图会走"退化帧回落几何法线"；补 `GenerateTangents` 是渲染资源侧的
下一步。

---

## 3. `MaterialInputs`：两套结构并存

| | Filament（`shaders/src/surface_material_inputs.fs`） | DSL 生成的 `MaterialInputs`（`Matc.cpp`，2026-10-24 起） | shader 里手写的 `FilamentMaterialInputs`（`surface_shading_parameters.glsl:6-22`） |
|---|---|---|---|
| baseColor / emissive / normal | ✅ | ✅ | ✅（baseColor 拆成 vec3） |
| roughness / metallic / reflectance / ao | ✅ | ✅（`!UNLIT` 才算） | ✅ |
| clearCoat / clearCoatRoughness | ✅ | ✅ | ✅ |
| thickness / transmission / absorption / ior | ✅ | ✅ | ✅ |
| specularFactor / specularColorFactor | ✅ | ✅ | ✅ |
| sheenColor / sheenRoughness | ✅ | ✅（写了才出现） | ❌ |
| anisotropy / anisotropyDirection | ✅ | ✅（写了才出现） | ❌ |
| bentNormal / clearCoatNormal | ✅ | ✅ | 只有 `clearCoatNormal`（且 = 几何法线） |
| subsurfaceColor / subsurfacePower | ✅ | ✅（写了才出现） | ❌ |
| postLightingColor / postLightingMixFactor | ✅ | ✅ | ❌ |
| shadowStrength | ✅ | ✅ | ❌ |
| microThickness | ✅ | ❌（薄层折射还没接） | ❌ |

也就是说：DSL 侧的字段集**已经和 `surface_material_inputs.fs` 对齐**（order、guard、默认值都照抄），
但 shader 里实际用的 `FilamentMaterialInputs` 是另一个结构，`evaluateSurface` 开头做了一次拷贝
（`evaluateSurface` 是 raw fragment 也能调的入口，`evaluateMaterial` 是骨架版的薄包装）。
下一步是把 `Lighting/Filament/*` 的入参直接换成 `MaterialInputs`、删掉这次拷贝。

---

## 4. 变体模型：幂集 vs 固定枚举

**Filament**（`libs/filabridge/include/private/filament/Variant.h`）：

- 8 bit：`DIR/DYN/SRE/SKN/DEP/FOG|PCK/VSM/STE`，有效组合 **96 个**（表在文件头注释）；
- `isValidStandardVariant` / `isValidDepthVariant` 白名单；
- `filterVariantVertex` / `filterVariantFragment`：**每个阶段只编它需要的位**
  （例如 FOG 不影响顶点阶段，SKN 不影响片元阶段）；
- `filterVariant(variant, isLit)`：unlit 材质把光照位全摘掉；
- `Material::hasVariant()` 还会检查包里到底有没有这个变体的两个阶段；
- 运行时 `FMaterial::getProgram` 按 `variant.key` 从 `mCachedPrograms` 取，**首次用到才创建**，
  另有 `Material::compile(priority)` 与 `setVariantFilter()` 让应用决定预编译范围。

**我们**：

1. **生成**：`Matc.cpp:1476-1536` 的 `appendVariants` 分两条路，**两条都是幂集**：
   - 有 `variantFilter`：`2^(filter 名字数) × pass 数`，底子是 `defaultFeatures`。
     `FilamentPBR` 7 个名字 → **128**；`Standard` 3 个名字 × 3 pass → **24**，
     且它的 `defaultFeatures : []` 意味着归档包里 5 个可选 feature（PARALLAX_MAPPING 等）
     **一个都没编**，只有设计师用 `--features` 重编译后才存在（这是"feature 编译期固定"的代价：
     归档包与设计器重编译出来的包内容不同，排查差异时要知道）；
   - 没有 `variantFilter`：`2^(feature 数) × pass 数`（`Matc.cpp:1520-1536`）。`GizmoCell` 5 feature ×
     2 pass = **64**，`GeomertySurface` 2 × 2 = **8**，`Blur` 2³ = 8。
   46 个材质合计约 **291** 个变体（去重前；`FilamentPBR` 那 128 个里只有 3 个位有代码差异，
   加载期被 stage 去重合并）。
2. **运行时**：`FeatureSet = std::unordered_set<std::string>`（`Rendering/Data/FeatureSet.h:7`），
   选变体是一次"字符串集合哈希 + 哈希表查找"（`Rendering/Resources/Shader.cpp:17-52`），
   而不是 Filament 的 `uint8` 数组下标；
3. **per-stage 过滤没有真正用上**：`EVariant.h` 的 `FilterVariantVertex/Fragment` 只被
   `Matc::WriteVariantDump`（`Matc.cpp:1347`，构建期诊断文件）调用，打包路径不用；
   且 `EVariant.h:78-81` 的 `FilterVariantVertex` 只滤 `DEPTH` 一位，不是 Filament 的完整过滤；
4. **没有 `isLit` 过滤**：unlit 材质照样生成 lighting 变体（靠 `VariantDefines` 的
   "阶段文本里没提到这个名字就丢掉 define" 兜底，`Matc.cpp:1206-1228`）；
5. **7 个变体位里只有 3 个有消费者**：整个 `Shaders` 目录里只出现
   `VARIANT_HAS_DIRECTIONAL_LIGHTING / DYNAMIC_LIGHTING / SHADOWING`；`FOG / PICKING / SSR / DEPTH`
   没有任何 shader 引用，只为它们生成的变体最终会被 stage 去重合并；
6. **没有按需编译**：`ShaderLoader::AssembleFromPackage`（`Rendering/Resources/Loaders/ShaderLoader.cpp:904`）
   在加载时把包里每个变体都编译+链接（有 stage/program 文本去重，见 :913-917，所以 128 个只差 FOG 位的
   变体实际只编一份），代价主要体现在 feature 幂集大的材质上（启动 ~26 s，`ShaderPipeline.md` 已记）。

另外两个具体后果：

- **CAD 材质根本没进 keyed 变体路径**：`GeomertySurface.shader` 的 material 块只有 `features`、
  没有 `variantFilter`，所以 `SceneRenderer` 不给它插 `VARIANT_*`（`SceneRenderer.cpp:1070-1080` 判断
  `UsesVariantKeys()`），于是它在 fragment 里**手写**
  `#define VARIANT_HAS_DIRECTIONAL_LIGHTING` / `VARIANT_HAS_DYNAMIC_LIGHTING`
  （`GeomertySurface.shader:211-212`）——永远两条光路都编译，且永远没有 `VARIANT_HAS_SHADOWING`。
- `DEPTH` 位的语义与 Filament 不同：Filament 的深度变体**只编顶点阶段**；我们靠 `passes : [SHADOW_PASS]`
  显式声明、片元阶段照编（`ShaderPipeline.md` §10 已列）。

---

## 5. 材质实例模型

| Filament | 我们 |
|---|---|
| `Material` 不可变（参数表、状态、变体集合）；`MaterialInstance` 持参数 + 逐实例状态 | 一个 `Rendering::Data::Material` 同时是定义和实例（`m_shader` + `m_properties` + `m_features` + 状态 + 序列化） |
| `MaterialInstance::setParameter` 是模板 + 按 UIB 声明类型校验 | `SetProperty(name, any)`（`Rendering/Data/Material.cpp:871`）**任意名字都能写**，写错名字只会在上传时被静默跳过 |
| 采样器可带 `SamplerParams`（wrap/filter/anisotropy），**同一张贴图可在不同材质用不同采样方式** | wrap/filter 是**纹理级**属性（`Rendering/Settings/TextureDesc.h:30`），材质侧无法覆盖 |
| 逐实例可设 scissor / stencil / depthFunc / polygonOffset(scale,constant) / maskThreshold / specularAA variance | 只有 bool（polygonOffsetFill / depthTest / depthWrite…），无 stencil、无 scissor、无阈值类 |
| `Material::getDefaultInstance()` + `MaterialInstance::duplicate` | 无；每个材质自己从参数表建默认值（`Material::UpdateProperties`） |
| `Material::compile(priority)` / `setVariantFilter()` / `getParameterCount`+`ParameterInfo` | 无编译控制；参数表反射有（`Shader::GetParameters`），够面板用 |

**判定**：对 CAD 现状（每个 TopoActor 一个材质实例）够用，但"同一材质资产被多处引用、各自覆盖少量参数"的
场景没有支持——目前只能整个复制。真正挡路的是**采样器参数**（brick 贴图想在一个材质里 repeat、在另一个
材质里 clamp 时做不到）和**类型校验**（拼写错误的参数静默失效）。

---

## 6. 参数 / 描述符 / 反射

已对齐且质量不错的（不建议重做）：

- 非采样器参数进一个 std140 块（`layout(std140, binding = 2) uniform MaterialParams`），
  offset/stride 由 matc 算（`Matc.cpp:1050-1108`），采样器按 `layout(binding = N)` 固定纹理单元
  （`Matc.cpp:1122-1157`）；
- 运行时用**共享 UBO 池 + range 绑定**（`Rendering/Data/UniformBufferPool.cpp`），换材质只改 range；
- 链接期一次性校验驱动布局与包的布局是否一致（`ShaderLoader.cpp:800-893`）——这一点比 Filament 更"早失败"；
- 引擎块分层与 Filament 的 descriptor set 对应：view(0) / reflection(1) / material(2) / object(3)，
  见 `Rendering/Settings/EBindingPoint.h`；
- 参数元数据（label/group/min/max/step/color/ui）比 Filament 的 matdbg 更贴合面板，是**加分项**。

未对齐：

| 项 | Filament | 我们 |
|---|---|---|
| `constants` | 编译期常量，折叠进 shader，每组常量值产生一个 program 变体（`Material::setConstant`） | 无。同一个材质想用两种常量配置只能出两份材质 |
| `outputs` (MRT) | `material { outputs : [ { name : ... } ] }` 声明，matc 生成 `layout(location)` | 无声明；`gbuffer.shader:44-46` 手写三个 `out` |
| `variables` | `Variable::CUSTOM0..4 / UV1 / COLOR / WORLD_POSITION` 等，matc 生成同名 vertex input | 只有 `vertexAttributes` 白名单（`Matc.cpp:1175-1182`，6 种含 CAD 专用的 `DOMAIN_ID`） |
| 参数上传粒度 | 逐实例脏区 + `commit()` | 每次签名变化**整块重写 + 整块上传**（`Material.cpp:590-634`）；签名本身是版本号哈希，开销可控 |
| `precision` / `featureLevel` | UIB 记 precision；FeatureLevel 0-3 决定可用特性 | 桌面 GL 4.5，无此概念（可接受） |

---

## 7. 着色器产物与后端

| Filament | 我们 |
|---|---|
| 默认出 **SPIR-V**，再用 spirv-cross 转 GLSL(ES)/MSL/WGSL；`GLSLPostProcessor` 做驱动 workaround/fixup、`SpirvFixup`、`ShaderMinifier` | 直接出 **GLSL 450 文本**（`Matc.cpp:1273`），只面向桌面 GL |
| `sca`（glslang AST）做**语义分析**：字段可写性、采样器绑定、`materialParams` 访问、uniform 类型 | 只有**编译**校验（`Tools/ShaderPack/CMakeLists.txt:51-64`，`glslangValidator` 逐 stage） |
| `TargetApi` / `TargetLanguage` / `ShaderQuality` / `FeatureLevel` 四个维度展开 | 无 |
| matdbg：运行时替换 shader、看 variant 生成 | `MaterialEditorPanel` 热重载（面板 Apply → 进程内 CompileSource → 重编 GLSL），属局部替代 |

**判定**：SPIR-V 化是**大工程且当前没有第二个后端**（`ShaderPipeline.md` §9 已决定保留最终 GLSL）。
真正值得补的是两件小事：语义分析（能在构建期报"参数类型/名字对不上"）与
`MaterialParameter` 的**类型校验**（§5）。

---

## 8. 着色模型覆盖：三套实现并存

| 材质 | 走哪条光照 |
|---|---|
| `Standard.shader`（通用/编辑器材质） | `Lighting/PBR.glsl` 的 `PBRLighting(...)`（**旧的、非 Filament 的 PBR**） |
| `FilamentPBR.shader`（对齐/验证用，冻结） | 手写 `main()` + `Lighting/Filament/*`，unlit 声明（**迁移前的对照实现**） |
| `FilamentPBR.shader` | **生成的 lit 预置**：`material()` 只填 inputs，`evaluateMaterial` 由 matc 拼 |
| `GeomertySurface.shader`（CAD 面） | 同上，但 fragment 是 raw stage（几何阶段的边方程 / domain color / clip / peel），调 `evaluateSurface` |

后果：

1. Filament 的改动要同步三处（或者只改两处、`Standard` 悄悄漂移）。CAD 与 lit 样板现在共用同一份
   生成的 `evaluateSurface`，漂移面缩到"`Standard`（旧 PBR） + `FilamentPBR`（冻结对照）"两处；
2. **衰减函数已经有第二份**：`Lighting/Light.glsl:136-149` 的 `CalculateAttenuation` 也改成了 Filament 的
   窗口化反平方，而 `surface_lighting.glsl:22-52` 又写了一份 `FilamentDistanceAttenuation` 给点光/聚光用；
   方向光走共享的 `CalculateDirectionalLightContribution`，点光/聚光走本地版本 —— 同一条链上两种实现；
   （`FilamentAlignmentTODO.md` 里"Standard 的共享 `Light.glsl` 未动"这句已经过期。）
3. 缺的字段（§3 表）在 `FilamentMaterialInputs` 里一个都没有，要加 sheen/anisotropy 得同时改
   MaterialInputs、parameters、`preparePixelParams`、`surfaceShading`、`surface_light_indirect` 和材质文件；
4. `surface_shading_model_standard.glsl` 相对 Filament 缺：`sheenLobe`、`anisotropicLobe`、
   `diffuseLobe` 的 `MATERIAL_HAS_REFRACTION` 分支（我们无条件乘 `(1 - transmission)`）、
   `fresnel(f0)` 无 f90 的退化分支（我们恒用 f90，等于恒开 `MATERIAL_HAS_SPECULAR_FACTOR`）。

**小项**：Filament 把 `MIN_PERCEPTUAL_ROUGHNESS` / `MIN_ROUGHNESS` / `MIN_N_DOT_V` 放在
`surface_material.fs:1-10`（**按 TARGET_MOBILE 分 fp16/fp32 两套**）；我们是在
`surface_shading_parameters.glsl:66,81` 硬编码 0.045/1e-4，`SectionFace.shader:221-226` 又抄了一份
（连同 `#define MATERIAL_HAS_NORMAL`、`HAS_ATTRIBUTE_TANGENTS`）。这些常量该进 prelude。

---

## 9. 光照 / 阴影 / IBL

### 9.1 灯光数据布局

Filament：`surface_lighting.fs:1-14` 一个具名 `Light` 结构体（`colorIntensity` / `l` / `attenuation` /
`worldPosition` / `NoL` / `direction` / `zLight` / `castsShadows` / `contactShadows` / `lightType` /
`shadowIndex` / `channels`），逐字段可读；光强度是"预曝光"的（`colorIntensity.w` 已含 EV100 换算）。

我们：**一个 mat4 打包**（`Rendering/Entities/Light.cpp:116-148` + `Lighting/Light.glsl:61-128`），
type 塞 `[3][0]`、intensity 塞 `[3][3]`、color 位打包进 `[2][0]`、range 借用 `[2][2]`、`castShadows`
塞 `[2][1]`。后果：

- 加一个字段（例如 `shadowStrength`、`direction` 的额外分量、per-light `layerMask`）要同时改
  `GenerateMatrix` 和 4 个 `Extract*`，而且很容易撞；
- **没有 per-light `visibility`**：我们把 `(1 - shadow)` 直接乘进方向光的 radiance
  （`Light.glsl:160-177`，Filament 路径也在用这个共享函数），阴影只剩下一个乘数；而 Filament 的
  `Light` 还带 `castsShadows` / `contactShadows` / `shadowIndex` / `channels`，可见性在
  `surface_light_directional.fs:82` 里算成 `visibility`（阴影 × `computeMicroShadowing`），
  作为 `occlusion` 传给 `surfaceShading`，最后只乘在光照结果上——这意味着它可以叠加
  micro-shadowing，也能让 clear coat 走不同的可见性；
- 没有光度学单位：`intensity` 是裸数值，曝光靠后处理 `_Exposure`，IBL 亮度靠
  `ReflectionUBO.brightness` 手工标定（`FilamentAlignmentTODO.md` 记：环境 2.2、太阳 0.87，
  推导见 `PreviewView.cpp:425-427`），Filament 是 `iblLuminance` + `ev100` 一套体系。
  **观感能对上，但数值没有物理意义，改灯/换 HDR 就要重标**。

### 9.2 阴影

- `ShadowRenderPass` **没有接入 pass 链**（`SceneRenderer.cpp:711` 是注释掉的 `AddPass`），
  `kMaxShadowMaps = 1`（`ShadowRenderPass.cpp:11`），只支持一张方向光正交 shadow map；
  Filament 是 `ShadowMapManager`：方向光 CSM（多级、按视锥切分、`ShadowMapManager` 负责每帧挑级）、
  spot 阴影、PCF/PCSS/VSM 三种过滤、contact shadow、`transparentShadow`。
- 打开风险：`SceneRenderer.cpp:1054-1067` 只要场景里有方向光的 `castShadows == true` 就会给材质选
  `VARIANT_HAS_SHADOWING`，而 `ShadowRenderFeature::OnBeforeDraw`（`ShadowRenderFeature.cpp:36-61`）
  在 `light.castShadows` 时**不检查** `IsSetupForShadowRendering()` 就去解 `light.shadowBuffer`
  —— 阴影 pass 未接入时 `shadowBuffer` 为空（`Light::PrepareForShadowRendering` 才建），
  debug 下断言、release 下空指针。目前没炸只是因为场景里的方向光没开 `castShadows`。
- CAD 面材质本来 `receive_shadows = false`，且没有 `SHADOWING` 变体（见 §4），等于**阴影链路整体未启用**。
  这与用户"阴影后置"的决定一致，但代码应该显式关掉（在上面那条路径上兜住），而不是靠巧合。

### 9.3 IBL

| Filament `IndirectLight` | 我们 |
|---|---|
| diffuse 走 **SH9**（`setIrradiance`）；没有 SH 时才回退 prefilter 的 roughness-one mip | **irradiance cube**（`SkyboxRenderPass` 烘，`surface_light_indirect.glsl:37`）—— 已知偏差，文档记了原因（预览的 prefilter 高档 mip 曾为空） |
| `iblLuminance` + `iblRotation` + 曝光系统 | `ubo_ReflectionProbeData.brightness` + `rotation`（`ReflectionUBO`） |
| 多个 `IndirectLight` / reflection probe 可挂在实体上，`Reflections` 支持 box 投影与局部范围 | 全局一张 sky pass 烘的 cube；`CReflectionProbe` 存在，但**Filament 这条 IBL 路径没有接 box 投影 / 局部裁剪**（只有旧 `Lighting/IBL.glsl` 有，`FilamentAlignmentTODO.md` 已记） |
| `dfg` 由 Filament 生成、随引擎绑定 | `_BRDFLut` 每 draw 由 `Material::BindGlobalTexture` 绑定（`ABaseRenderer.cpp:446`） |

---

## 10. 后处理 / 帧图 / 线程（与材质链的关系）

这三项 `RenderingPlan.md` 2.6 已立项目标，这里只记与材质/着色器直接相关的部分：

- **无 FrameGraph**：pass 顺序硬编码在 `ERenderPassOrder`，材质侧也没有"pass 需要什么资源"的声明——
  Filament 的 `MaterialDomain::POST_PROCESS` + `subpass` 模型在我们这里对应的是 `domain : postProcess`
  （骨架只生成一个全屏 quad，`Matc.cpp:929-938`；varying 只有 `TexCoords`，`Matc.cpp:753-758`）；
- **无 ColorGrading**：Filament 的 `ColorGrading`（3D LUT + ACES + dither + 白平衡/对比度）我们只有
  `PostProcess/Tonemapping.shader` 里的 ACES 与 triangle-noise dithering；
- **无 TAA，也没有按 `FeatureLevel` 分档的 AA 策略**：只有 FXAA 与 MSAA 缓冲；
- **无渲染线程**：`glCompileShader` 在主线程，这是启动 26 s 的直接原因（`ShaderPipeline.md` §8 已记）。
  材质/着色器侧的对策是"变体按需编译"（§12 批次 B）。

---

## 11. 顺手要修的

### 11.1 文档落后于代码

| 位置 | 说的事 | 实际 |
|---|---|---|
| `docs/RenderingPlan.md:41` | "`SHADOWING` / `SSR` / `FOG` 暂无使用方" | `SHADOWING` 已由 `SceneRenderer.cpp:1067` 逐 draw 选择 |
| `docs/ShaderPipeline.md:282,284` | 已迁移 44/48 | 实际 **47/51**（36 顶层 + 11 PostProcess；PathTrace 4 个仍是旧格式），其中 1 个是 `lit` |
| `docs/FilamentAlignmentTODO.md:21-22` | "Standard 的共享 `Light.glsl` 未动" | `Light.glsl:136-149` 已改成 Filament 的窗口化反平方 |

### 11.2 代码里的两处隐患

| 位置 | 问题 |
|---|---|
| `Resource/.../SectionFace.shader:221` | 手写 `#define MATERIAL_HAS_NORMAL`（同处还抄了 `MIN_*` 常量）。该宏按设计应由 matc 生成，现在既没人生成也没人读——等 §1 落地时要删掉，否则会和生成的定义冲突/重复 |
| `MoonRender/src/Core/Rendering/ShadowRenderFeature.cpp:36-61` | 假设阴影 pass 已经跑过：不检查 `IsSetupForShadowRendering()` 就解 `light.shadowBuffer`。`ShadowRenderPass` 目前没接进 pass 链，只要场景里有 `castShadows` 的方向光就会断言/空指针 |

---

## 12. 建议的落地顺序

不排优先级地"全抄"没有意义，按下面三批走，每批都能独立验证：

**批次 A｜把编译期特化立起来（对齐的核心，收益最大）**

1. ✅ `MaterialInputs` 补齐字段（照 `surface_material_inputs.fs` 的 order/guard/默认值）；
2. ✅ matc 扫"材质写了哪些 inputs 字段"，生成 `MATERIAL_HAS_*` + `MATERIAL_NEEDS_TBN` +
   `GEOMETRIC_SPECULAR_AA` + `CLEAR_COAT_IOR_CHANGE`，JSON 同步；
3. ✅ 把 `u_HasXxxMap` / `u_UseNormalMap` 从运行期 bool 改成材质级 define / 中性回退纹理：
   迁到 lit 的两个材质已经**一个 bool 都没有了**——色/金属/粗糙/AO 用白色 1×1 回退，法线用
   DSL 的 `fallback : flatNormal`（(0.5,0.5,1) 的 1×1，解码成 (0,0,1)），所以采样全部无条件；
   `specularAntiAliasing` 也从参数变成了 `material { }` 里的编译期开关。只剩冻结的 `FilamentPBR`
   为了保住对照还留着那几个 bool；
4. ✅ `shadingModel : lit` 可用，CAD 材质已迁（`GeomertySurface` 走 raw fragment + `evaluateSurface`）。
   剩下：删掉 shader 里重复的 `FilamentMaterialInputs`（让 surface 栈直接吃 `MaterialInputs`），
   以及给导入模型补切线生成。

**批次 B｜变体模型收口**

5. `determineSurfaceVariants` + `isValid*` + `filterVariant(Vertex|Fragment|isLit)` 替换幂集，
   包里的变体清单直接存 bit key；
6. 运行时把 `FeatureSet` 换成 `uint8` key（`Shader::GetVariant(pass, key)`），draw 路径去掉字符串哈希；
7. `variantFilter` 与用户过滤（`setVariantFilter` 等价物）分化开：前者是材质能力，后者是运行时选择；
8. 变体按需编译（首次 draw 才编，或后台线程预编），解决启动 26 s。

**批次 C｜材质实例与数据面**

9. `Material` / `MaterialInstance` 拆分（定义不可变，实例持参数块 + 状态 + sampler 参数）；
10. 采样器参数（wrap/filter/anisotropy）下沉到材质实例；
11. `constants` / `outputs` / `variables`；
12. 参数块脏区上传；`SetProperty` 加类型校验。

**暂不排期（维持现有决定）**：sheen/anisotropy/subsurface/cloth、SSR、fog、CSM/PCSS 阴影、光度单位、
FrameGraph、渲染线程。

---

## 13. 一句话总结

离线打包和"参数怎么进 GPU"这两层已经很像 Filament 了；**真正没对齐的是 Filament 材质系统里"编译期就知道
这个材质有什么"的那一半**——`shadingModel` 没实现、`MATERIAL_HAS_*` 一个都没生成、变体还是幂集、
`MaterialInputs` 有两套结构。这四件事互相咬合，应该作为一批做（批次 A/B），而不是分散补。
