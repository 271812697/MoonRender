# 着色器离线编译（对标 Filament matc）

> 状态：**规范草案**，待 review。本文只定义目标形态与语法，不改现有代码。
> 设计原则：**照抄 Filament 的成熟设计**，不自己发明。每一节都标出对应的 Filament 实现，
> 便于逐条对照。唯一的偏离是"shader 由我们手写入口函数 + matc 内置 prelude"，Filament 是
> 同样的模型（它也是入口函数 + 生成骨架），只是它的骨架更厚。

现有进度：

| 件 | 状态 |
|---|---|
| `Tools/ShaderPack`（blob → `shader.h` / `shader.cpp`，对标 `resgen`） | ✅ 已完成 |
| `ShaderArchive`（运行时按名字取源码，归档优先/磁盘兜底） | ✅ 已完成 |
| `#include` 展开 + `#line` 行号映射（对标 `Includes.cpp`） | ✅ 已完成 |
| `MoonMatc`（对标 `matc`）、新 `.shader` 语法、chunk 包、运行时 chunk reader | ⬜ 本文内容 |

---

## 1. 流水线（三级，一一对应）

```
X.shader ──[MoonMatc]──► X.ovshader ──[ShaderPack]──► shader.h + shader.cpp ──► 链接
        逐文件            每个 .shader 一个包        多包合并（已有）          进 exe
                            │
                            └─ 运行时：读 chunk → glShaderSource → glCompileShader
```

| Filament | 我们 | 说明 |
|---|---|---|
| `tools/matc` + `libs/filamat` | `Tools/MoonMatc` | 解析 + 变体 + 校验 + 反射 + 打包 |
| `.filamat` | `.ovshader` | chunk 容器 |
| `tools/resgen` | `Tools/ShaderPack` | 已实现，收窄为"只合并" |
| `filament/src/MaterialParser.cpp` + `filaflat/DictionaryReader` | `ShaderManager` 内的 chunk reader | 运行时只读、不解析源码 |

**收益**：`.glsl` 不再发布（prelude 编进 matc、include 构建期展开）；`shader.cpp` 里不再有
明文源码，只有编译包；运行时不再做 `2^n` 变体全组合、不再拼宏、不再扫文本。

---

## 2. 新 `.shader` 语法

照 `lit.mat`（`android/samples/sample-lit-cube/.../lit.mat`）的四个部分。文件名仍为 `.shader`，
重构完成后可整体改名 `.mat`。

### 2.1 三个块

```glsl
material {
    name        : Standard,
    shadingModel: lit,          // lit | unlit | subsurface | cloth ...
    blending    : opaque,       // opaque | transparent | add | mask ...
    culling     : back,         // back | front | none
    depthWrite  : true,
    depthCulling: true,
    doubleSided : false,

    requires    : [ tangents ], // 顶点属性需求

    parameters  : [
        { type : sampler2D, name : albedoMap },
        { type : float4,    name : baseColor },
        { type : float,     name : metallic },
        { type : float,     name : roughness },
        { type : float3,    name : emissiveColor },
        { type : float,     name : emissiveIntensity },
    ],
}

vertex {
    void materialVertex(inout MaterialVertexInputs material) {
        // 可选。不做变换，只允许改属性
    }
}

fragment {
    void material(inout MaterialInputs material) {
        prepareMaterial(material);
        material.baseColor = texture(materialParams_albedoMap, ...) * materialParams_baseColor;
        material.metallic  = materialParams_metallic;
        material.roughness = materialParams_roughness;
    }
}
```

- 支持"只有 fragment 块"（unlit 材质、后处理全屏 pass 都不需要 vertex 块）。
- `#include ":Shaders/Common/....glsl"` 仍允许，但 **prelude 不再需要手写 include**
  （见 §3）。

### 2.2 `MaterialInputs`（照抄 `shaders/src/surface_material_inputs.fs`）

按 `shadingModel` 裁剪字段——Filament 就是这么做的（`#if !defined(SHADING_MODEL_UNLIT)` 等）：

| 字段 | 出现条件 | 我们的 `Standard.shader` 今天怎么写的 |
|---|---|---|
| `vec4 baseColor` | 总是 | `u_Albedo * u_AlbedoMap` |
| `float roughness` | lit 且非 specular-glossiness | `u_Roughness * u_RoughnessMap` |
| `float metallic` / `reflectance` | 同上 | `u_Metallic * u_MetallicMap` |
| `float ambientOcclusion` | lit | `occlusionTex` |
| `vec4 emissive` | 总是 | `u_EmissiveColor * u_EmissiveIntensity` |
| `float clearCoat` / `clearCoatRoughness` | lit | 现在没有 |
| `float anisotropy` / `vec3 anisotropyDirection` | lit | 现在没有 |
| `float thickness` | subsurface / refraction | 我们的 `u_Transmission`/`u_RefractionIndex` |
| `vec3 normal` | 需法线贴图时 | `u_NormalMap` 的结果 |

> 只抄我们真的会实现的字段；`sheen`/`subsurfaceColor`/`specularColor` 等先不放进表里，
> 需要时再按 Filament 的定义补——表结构已经和它一致，加字段不影响其它部分。

### 2.3 `MaterialVertexInputs` / `MaterialVertexOutputs`

照 Filament 的 `surface_vertex_inputs.vs` / `..._outputs`：位置/法线/切线/UV 由 prelude 提供，
入口函数只能改属性，不能改变换。我们目前的 `VERTEX_POS_NORMAL` feature 就是它的雏形。

### 2.4 本轮补上的四个语法（迁移 44 个 shader 时逐个逼出来的）

| 语法 | 用途 | 例子 |
|---|---|---|
| `vertex { void main() {} }` / `fragment { void main() {} }` | **整段自管**（raw stage）：块里定义了 `main()`，MoonMatc 就只给 prelude + 参数，属性/varying/`FRAGMENT_COLOR`/骨架 main 全都不生成，块自己声明、自己写 `gl_Position` | `GizmoLine` `SkyboxConvert` `Point` `Standard` |
| `geometry { ... }` | GLSL 第三阶段，**我们的扩展**（Filament 没有）。总是整段自管，块里定义 `layout(triangles) in` 等 | `GizmoLine` `GeomertySurface` `SectionFace` `GizmoCell` `SectionContour` |
| `{ type : float3, name : samples, count : 64 }` | 数组 uniform（SSAO 的 64 采样核） | `ssao` |
| `{ type : float, name : u_GridSize, default : 0.01 }` | uniform 的 GLSL 初始化值：老文件 `uniform float x = 0.01;` 的下发默认值必须保留，否则没人设的参数会静默变 0 | `Grid` `SectionCap` `Billboard` `Standard` |

另外两件配套的事：

- **`#include` 改成构建期展开**。老路径由运行时 `ShaderLoader::AssembleSource` 展开（对标
  Filament 的 `Includes.cpp`）；成了 package 之后 GLSL 直接进驱动，没有运行时那一趟了，所以
  `MoonMatc` 自己递归展开（`-I` 是 shader 根，`:Shaders/...` 前缀映射到它），每个文件每个阶段
  只展开一次（头文件没有 include guard，`PBR.glsl` 会二次够到 `Utils.glsl`），
  `Common/Buffers/EngineUBO.glsl` 和 `Common/Utils.glsl` 这两份 prelude 自带的直接丢掉。
  展开处插 `#line` 回指，报错仍然指到 `.glsl` 自己的行。
- **每个阶段带 `#define MOON_STAGE_VERTEX|FRAGMENT|GEOMETRY`**。`Common/Utils.glsl` 里的
  `ComputeTBN()` 用 `dFdxFine`，只有片元阶段能用；以前它被拼进所有阶段，NVIDIA 驱动容忍、
  glslang 不容忍。现在用 `#if !defined(MOON_STAGE_VERTEX) && !defined(MOON_STAGE_GEOMETRY)`
  把它挡在非片元阶段外（老路径不定义这些宏，行为不变），顶点/几何阶段才终于能离线校验。

---

## 3. prelude：我们的"生成骨架"

Filament 的骨架在 `filament/src/materials/`（`surface_shading_model_standard.fs`、
`surface_lighting.fs`、`surface_shadowing.fs` …），由 matc 拼进去。我们的骨架就是现有的
`Shaders/Common/*.glsl` + `Standard.shader` 里那段光照，整理成 matc 内置文件：

| prelude | 来源 | 内容 |
|---|---|---|
| `prelude/buffers.glsl` | `Common/Buffers/EngineUBO.glsl`、`LightsSSBO.glsl`、`ReflectionUBO.glsl` | 帧/灯/反射 UBO |
| `prelude/common.glsl` | `Common/Constants.glsl`、`Utils.glsl`、`Physics.glsl` | 常量与工具函数 |
| `prelude/material.glsl` | 新写（照 `surface_material_inputs.fs`） | `MaterialInputs` / `prepareMaterial()` / `materialParams` |
| `prelude/surface.glsl` | `Standard.shader` 的光照段 + `Lighting/PBR.glsl`、`IBL.glsl`、`Shadow.glsl` | 由 `shadingModel` 选中的着色模型 |
| `prelude/variants.glsl` | 新写（照 `Variant.h`） | 变体位对应的 `#define` |
| `prelude/output.glsl` | `Standard.shader` 末尾 | 色调映射/alpha 等收尾 |

matc 拼接顺序（照 Filament）：`#version` → 顶点/片元属性 → 变体位 `#define` →
`buffers` → `common` → `material` → `surface` → 用户入口函数（`main` 之前）→ 生成的 `main()`。

---

## 4. 变体模型（照 `Variant.h` + `MaterialVariants.cpp`）

Filament：8 位掩码 + 白名单 + **固定枚举**（`determineSurfaceVariants(filter, isLit, ...)`），
**从不做 2^n 全组合**。我们现在 `Standard.shader` 的 64 个变体就是这么来的（`AssembleShader` 的
幂集循环），这是最该替换的一处。

我们的变体位（初版，够用即可，后续按需加）：

| 位 | 名 | 含义 |
|---|---|---|
| `0x01` | `SKN` | 蒙皮/变形（暂留空） |
| `0x02` | `SRE` | 接收阴影 |
| `0x04` | `DIR` | 有方向光 |
| `0x08` | `DYN` | 有点光/聚光 |
| `0x10` | `DEP` | 只写深度（阴影/拾取） |
| `0x20` | `NRM` | 有法线贴图 |
| `0x40` | `MSK` | alpha mask/dithering |
| `0x80` | `TRS` | 透射/折射 |

`isValidStandardVariant()` / `isValidDepthVariant()` 照 Filament 写白名单；`-V` 过滤对照
`variantFilter`。`Standard` 从 64 个压到 ~6 个。

---

## 5. chunk 格式（照 `MaterialChunkType.h`）

8 字节 ASCII tag + 数据，同 `filabridge/include/filament/MaterialChunkType.h` 的写法：

| tag | 内容 |
|---|---|
| `SHDR_GLSL` | 每 (pass, variant, stage) 的最终 GLSL |
| `SHDR_UIB` | uniform 块布局（偏移/大小/类型） |
| `SHDR_SIB` | sampler 绑定 |
| `SHDR_ATTR` | 顶点属性 |
| `SHDR_BIND` | binding 下标 |
| `SHDR_VARI` | 变体清单（位值 → stage 列表） |
| `SHDR_NAME` / `SHDR_UUID` / `SHDR_VERS` | 名字 / 内容 hash / 版本 |

`SHDR_GLSL` 内部分组成一块"字典"（blob 数组 + (pass, variant, stage) → blob 下标 的索引），
对应 Filament 的 `DictionaryChunk` + `MAT_GLSL`。

**实际实现的 tag**（8 字节，注意不是上面表格里带下划线的拼法）：

| tag | 内容 |
|---|---|
| `SHDRSTAG` | **每个阶段一份**的全文，变体 `#define` 的位置留成占位符 `//@MOON_VARIANT_DEFINES@` |
| `SHDRGLSL` | 每 (pass, variant, stage) 只存**自己那几行 `#define`** |
| `SHDRVARI` | 变体清单（pass + feature 集合） |
| `SHDRPROP` | **反射**：每个参数的 name、DSL 类型、GLSL 类型、数组长度、是否 sampler、声明处初值 |
| `SHDRATTR` | **反射**：顶点属性（location、GLSL 类型、名字），从生成的顶点阶段文本里读回 |
| `SHDRPARM` | 老的 name/type 表，保留兼容（`SHDRPROP` 在它之后写出并覆盖同名记录） |
| `SHDRSTAT` | `material { }` 里的状态字段（blending/culling/depthWrite …），目前只记录、运行时不读 |
| `SHDRNAME` | 材质名 |

`SHDRPROP` / `SHDRATTR` 对应 Filament 的 `MaterialUib` / `MaterialSib` / `MaterialAttributeInfo`，
但粒度按我们的实际情况收窄：Filament 的 UIB 要写块内 **偏移与大小**（它把参数打进一个 UBO），
SIB 要写 **binding 下标**（Vulkan 的 descriptor set）。我们用的是 uniform、按名字绑纹理，没有布局，
没有 descriptor set，所以一块带 flag 的列表就够——但面板要的东西（类型、是否贴图、数组长度、
默认值）都在里面，不需要运行时再问驱动（`glGetActiveUniform`）。

属性列表是**按材质**而不是按变体：某个阶段在 feature 分支里声明的属性，会把文本里出现过的声明
都算进去（Filament 的 attribute 列表同样来自 `material { requires : [...] }`，也是材质级的）。

除此之外还有两件事顺手做了：`--dump-json` 现在输出 `parameters`（含 `glslType`/`sampler`/`count`/
`default`）和 `attributes`；顶点属性的采集是从**生成的阶段文本**里正则读回来的，所以整段自管的阶段
（`GizmoLine` 的 `aPositionSize`/`aColor`、实例化属性）和骨架生成的 `geo_Pos/geo_TexCoords/geo_Normal`
都能被记录。

`SHDRSTAG` + `SHDRGLSL` 的拆分是**必须的**，不是优化：一个材质的每个变体除了那几行 `#define`
以外完全相同，`Standard` 有 192 个变体，不拆就是 192 份同样 64 KB 的源码 —— 打包出来
20.5 MB（`shader.cpp` 99.8 MB）；拆完 1.31 MB（`shader.cpp` 6.8 MB）。运行时
`ShaderPackage::Source()` 把两者拼起来（`Source` 因此返回 `std::string` 而不是 `string_view`）。
没有 `SHDRSTAG` 的旧包仍然按"blob 就是整段"读，所以格式是向后兼容的。

---

## 6. 运行时

照 `filament/src/MaterialParser.cpp`：`Shader` 不再持有源码，而是持有 chunk 视图。

```
ShaderManager[key]
   └─ 读表项 { name, data, size }        ← 已有（ShaderArchive）
      └─ Unflatten + DictionaryReader    ← 新：切 chunk、填 blob 字典
         ├─ 变体清单（用于 variantFilter 校验）
         ├─ 反射（UIB/SIB/ATTR）→ 材质参数表 → 面板
         └─ GetVariant(pass, variant) → glShaderSource + glCompileShader（一次）
```

删除：`ParseShader`（扫 `#shader/#feature`）、`AssembleShader`（2^n 循环 + 宏注入）。
**这两个暂时不能删**：PathTrace 的 4 个 shader 还按"读文件文本 + 插 define"的旧方式编译，
`CreateFromSource`（内嵌字符串）更是 5 个绘制点在用。§8 已把 PathTrace 定为永久例外
（后续重构路径追踪器时一起改），所以这条"删除"要等那之后。

---

## 7. 迁移计划（垂直切片优先）

1. **切片**：`Unlit.shader` 走通全链路（新语法 → MoonMatc → `.ovshader` → ShaderPack →
   运行时读 chunk → 画面正常）。此阶段新旧两套并存。
2. 后处理类（Bloom / FXAA / ToneMap / HZB / Blit …）：`shadingModel : unlit` + 全屏 pass，
   体量小、风险低——照 `filament/src/materials/` 里同名的 `.mat` 抄结构。
3. 工具类（Grid / Line / Picking / Section / Skybox）：大多也是 unlit，同上。
4. `Standard.shader` 最后做（PBR/IBL/阴影/变体最多）。
5. 全部迁完后再删旧解析路径，并把 `.shader` 改名为 `.mat`。

完成情况：1-4 已做完（`Engine/Shaders` 下 44 / 48 个 `material{}` 形式，剩 4 个 PathTrace 定为
永久例外，见 §8）；第 5 步**不做**——旧的文件版解析路径还要留给 PathTrace，`.shader` 也因此不改名
`.mat`。

> 之所以停在"两条路并存"而不是彻底切换：剩下那 4 个路径追踪 shader 的加载方式是
> "运行时往源码里插 define 再编"，和构建期出包的模型天然冲突；决定等**路径追踪器整体重构**时
> 连加载方式一起换，现在迁移属于白改（详见 §8 "剩余"）。

---

## 8. 交接：当前进度、换算配方、已踩的坑

### 进度

| 组 | 状态 |
|---|---|
| 构建期 `Tools/MoonMatc`（= matc） | ✅ `material{}`/`vertex{}`/`geometry{}`/`fragment{}` 解析、`domain: surface\|postProcess`、变体展开、`uniform` 生成（DSL 名 → GLSL、`count` 数组、`default` 初值）、`#include` 构建期展开、整段自管阶段、阶段宏、`#line` 回指、`.ovshader` chunk、反射 JSON、未迁移文件透传 |
| 打包 `Tools/ShaderPack`（= resgen） | ✅ `name=path` 合并成 `shader.h` / `shader.cpp`，`MoonShader` 由 `MoonRender` 链接 |
| 运行时 `ShaderArchive` / `ShaderPackage` / `ShaderLoader` | ✅ 归档优先、`MOONSDR1` 分流、按变体清单编译（顶点/片元/几何三段，几何可选） |
| **已迁移（44 / 48）** | ✅ `Unlit` `Standard` `GeomertySurface` `GeomertySurfacePick` `GeomertyLine` `GeomertyPoint` `GizmoCell` `Gizmo` `GizmoLine` `GizmoPoint` `GizmoTriangle` `Grid` `Line` `Point` `Billboard` `Atmosphere` `Skysphere` `SkyboxConvert` `SkyboxIrrandiance` `SkyboxPrefilter` `BrdfLut` `ClearBackground` `PickingFallback` `OutlineFallback` `ShadowFallback` `MirrorPlaneShadow` `SectionCap` `SectionContour` `SectionFace` `SectionParity` `gbuffer` `ssao` `ssaoblur`；`PostProcess/` 全部 12 个 |

`Resource/Moon/Data/Engine/Shaders` 下共 48 个 `.shader`（+ 10 个 `.glsl`，共 58 个源打进归档）。

### 剩余（都已定案，不是待办）

**1. D 组 4 个（PathTrace）：定为永久例外，不迁。**

`PathTrace/Output` `PathTrace/PathTrace` `PathTrace/PathTraceLowRes` `PathTrace/ToneMap` 继续走
`CreateFromSource` + `#shader` 语法。这不是"没来得及"，是**明确决定不迁**（2026-10，后续路径追踪器
要整体重构，届时连加载方式一起改，现在动属于白改）。理由和数字留在这里，免得后来人重走一遍：

- `PathTraceRenderPass::UpdateShaders()`（`PathTraceRenderPass.cpp:590-742`）先
  `loadShaderSource()` 把文件读成字符串，再按渲染选项**往源码里插入 `#define`**，然后按 define 串
  缓存 `CreateFromSource` 的结果。文件一旦改成 `material{}`，这段"找 `#shader fragment` + 插
  define"就失效——迁它等于改这条渲染通路的加载方式；
- 12 个 `OPT_*` 布尔（`OPT_ENVMAP` `OPT_LIGHTS` `OPT_RR` `OPT_MEDIUM` `OPT_VOL_MIS`
  `OPT_ALPHA_TEST` `OPT_BACKGROUND` `OPT_TRANSPARENT_BACKGROUND` `OPT_HIDE_EMITTERS`
  `OPT_UNIFORM_LIGHT` `OPT_OPENGL_NORMALMAP` `OPT_ROUGHNESS_MOLLIFICATION`）当 features 用，
  幂集是 2^12 = 4096 个变体 × 每份 53 KB × 2 阶段；而且 `PropertyWidget` 允许运行时任意
  `AddFeature`，所以"只编真会用的组合"也不封闭；
- 规模：`PathTrace` / `PathTraceLowRes` 各 1926 / 1905 行、各 28 处 `OPT_*` 守卫（6 个 `#else`），
  `ToneMap` 2 处，`Output` 0 处。要把它们迁成"1 个变体 + uniform 布尔"，先得做完这件事再跑
  benchmark——留给路径追踪器重构；
- 附带好处：`OPT_RR_DEPTH` 那个带参数的 define 其实没被 shader 用到（`PathTrace.shader` 里只有
  `OPT_RR` 和一个 `RR_DEPTH` uniform），所以将来只需要处理 12 个纯布尔。

因为这条决定，**`ShaderLoader` 的文件版旧解析路径（`CompileShaderFromFile` → `ParseShader` →
`AssembleShader`）保留**，§7 的第 5 步（删旧路径）相应搁置。

**部署形态也跟着变了**：引擎着色器现在只在 exe 里（`Moon/CMakeLists.txt` 的资源拷贝步骤把
`Data/Engine/Shaders/` 排除掉了），运行目录里只剩 `Data/Engine/Shaders/PathTrace/*.shader`
（4 个，上面的永久例外）还要从磁盘读。改那段拷贝规则时别再一刀切。

**2. `Resource/Moon/Data/Editor/Shaders/` 下那 5 个老副本：已删。**

`Grid` `Gizmo` `Billboard` `PickingFallback` `OutlineFallback` 原来由 `EditorResources` 构造时
加载（`m_shaders`），但 `EditorResources::GetShader()` **全工程 0 个调用点**——它们只被加载、
从来没被用来画过任何东西（`GridRenderPass` 用的是 `ShaderManager[":Shaders/Grid.shader"]`，也就是
`Engine/Shaders` 那份；`DebugSceneRenderer` 只是 include 了头文件）。已连同 `m_shaders` 成员、
`GetShader()`、`CreateShader()` helper、两个无用的 `#include`、目录本身和 CMake 里的拷贝/IDE 挂载
一起删掉。顺带的好处：启动少编 5 个 shader，运行目录不用再带它们。

### 代价（本轮引入，必须交代）

| 项 | 迁移前 | 拆分前 | 现在 |
|---|---|---|---|
| 归档字节（打进 exe） | 683 KB | 20.5 MB | **1.31 MB** |
| `Build/generated/shaders/shader.cpp` | 0.7 MB | 99.8 MB | **6.8 MB** |
| `Moon.exe` | — | 30.1 MB | **11.9 MB** |
| 启动到首帧（开 1.moon） | ~10 s | ~25 s | **~26 s** |

体积那一列已经解决（`SHDRSTAG` 共享阶段文本，见 §5）：剩余 1.31 MB 对比原来的 683 KB，
基本就是变体数量和 `Standard`/`GeomertySurface`/`GizmoCell` 的真实增量。

**剩下的是启动那 ~15 s，它没被解决**：变体数没变，驱动就要编同样多的程序
（`Standard` 192 变体 × 2~3 阶段 + `GizmoCell` 64 × 3 + `GeomertySurface` 64 × 3 …）。
老路径其实也编这么多，所以严格说这不是迁移引入的，但现在的数字不好看。真正的解法是
**按需编译变体**（首次 `GetVariant` 时才编，而不是 `Shader` 构造时全编）——那是运行时行为改动
（首帧会卡一下），需要单独决策，本轮没动。

### 换算配方（已用 44 个文件验证）

```diff
- #feature X             →  material { features : [ X ], ... }
- #pass P                →  material { passes : [ P ], ... }
- #shader vertex 整段     →  删掉（postProcess 生成全屏 quad；surface 生成 MVP + materialVertex）
- #shader fragment
- #shader geomerty 整段   →  geometry { 原样 }（整段自管，MoonMatc 只给 prelude + 参数）
- uniform <type> <name>;  →  material { parameters : [ { type : <DSL 名>, name : <同名> } ] }
- uniform <t> <n> = V;    →  { type : <DSL 名>, name : <n>, default : V }   ← 初值必须留
- <t> <name>[N];          →  { type : <DSL 名>, name : <name>, count : N }
- void main() { ... }     →  fragment { void material(inout MaterialInputs m) { ... } }（或整段自管）
- FRAGMENT_COLOR = X;     →  m.baseColor = X;（整段自管时保持原样）
```

- 参数类型写 **DSL 名**：`float/float2/float3/float4/bool/int/mat4/sampler2D/samplerCube`
  （工具转 GLSL）；表里没有的名字（`samplerBuffer`/`usamplerBuffer`）原样透传给 GLSL；
- `TexCoords`（postProcess）或 `fs_in.TexCoords` / `fs_in.FragPos` / `fs_in.Normal`（surface）
  由 skeleton 提供，`FRAGMENT_COLOR` 也不用声明；**整段自管时这三样都得自己声明**；
- helper 函数、常量数组、`#define`、`#if defined(FEATURE)` 都可以留在块内；
- 真的能用 skeleton 就别用整段自管：`materialVertex` 能改属性，改不了 `gl_Position`。

### 验证

```powershell
cmake --build Build --target Moon --config Release -- /nologo /m:1
```

**构建期就会校验 GLSL**（照 filament 的 matc：编译不过就构建失败，不让它活到运行时）。
`Tools/ShaderPack/CMakeLists.txt` 里 `find_program(GLSLANG_VALIDATOR ...)` 找到
`glslangValidator` 后，每个 `MoonMatc` 规则变成三步：

1. 清掉旧 shader 的 dump 目录（避免拿上一轮的残留阶段去校验）；
2. `MoonMatc --dump-glsl <dir>` —— 每个 (pass, 变体, 阶段) 一个 `.glsl`；
3. `ValidateGlslStages.cmake` 对每个文件跑 `glslangValidator -S vert|frag|geom`，有失败就
   `FATAL_ERROR`，并把 glslang 的输出原样打出来。

找不到 `glslangValidator` 时只打一条 `STATUS`（“will NOT be validated at build time”），
其它行为不变——所以这是一条可选依赖，不引入 glslang 库（filament 是自己链 glslang + SPIRV-Tools）。

前置的一件事：`MoonMatc` 现在在每个阶段开头（`#version` 之后）输出
`#extension GL_GOOGLE_cpp_style_line_directive : enable`（照 filament 的 `CodeGenerator.cpp:125-127`）。
没有它 glslang 直接拒绝带文件名的 `#line`，dump 出来的东西就喂不进 validator；驱动侧本来就接受。

实测（故意在 `ssaoblur.shader:27` 写坏一行）：

```
CUSTOMBUILD : error : ssaoblur.shader:27: '' :  syntax error, unexpected IDENTIFIER ...
ssaoblur.shader: 1 stage(s) do not compile (glslangValidator). The dump is under ...
```

行号指回 `.shader` 自己，构建 exit 1。**代价**：只有改动的 shader 会重新校验，但单个大
shader 不便宜——`Standard`（192 变体、380 个阶段）重新校验 + 重新打包 + 链接要 **50 s**；
全量清理后重建会校验全部 1002 个阶段。嫌慢的话把 `GLSLANG_VALIDATOR` 缓存变量置空即可关掉。

再**带一个工程启动**，让视口真的画帧（不开工程停在首页时，一半 shader 根本不会被加载）：

```powershell
Start-Process Build\bin\Release\Moon.exe -ArgumentList '"D:\Project\UseQt\SketchExample\1.moon"' `
    -WorkingDirectory Build\bin\Release -RedirectStandardOutput Build\run.txt
```
正常日志只有 `[Shader] sources are read from the embedded archive (58 files packed)`，
不应有 `[Shader Package]`（变体缺源）/ `[Shader Compile]`（编译失败）/ `[Shader Linking]`。
`Moon.exe <工程路径>` 是这一轮加的小功能（`Moon/main.cpp`，200 ms 后 `onReadFile`），就是为了让
这件事能脚本化——以前只能手点首页。

想单独验证某个 shader（构建期没开校验、或想手工看 dump）：

```powershell
Build\bin\Release\MoonMatc.exe -i <X.shader> -o Build\verify\X.ovshader -I Resource\Moon\Data\Engine\Shaders `
    --dump-glsl Build\verify --dump-json Build\verify\X.json
```
dump 出来的 `.glsl` **直接**就能喂 validator（阶段开头已经有那行
`#extension GL_GOOGLE_cpp_style_line_directive : enable`），不用再手工插。
`glslangValidator <stage>.glsl -S vert|frag|geom`。
三个阶段都必须过——`ComputeTBN` 的 `dFdxFine` 已经被 `MOON_STAGE_*` 挡掉，顶点/几何阶段不再是
"注定失败"，这里报错就是 shader 真有问题。

**阶段名要从文件名后缀判断**（`_vertex` / `_fragment` / `_geometry`），不能用
`-match 'vertex'` 这种大小写不敏感的包含判断：`_VERTEX_POS_NORMAL_fragment.glsl` 会命中
`vertex`，于是把片元当顶点编，报一堆假失败（本轮踩过）。

干净状态下跑一遍全量（44 个材质、1002 个阶段）：
```
materials: 44 ; stages validated: 1002 ; failures: 0
```

### 已踩的坑

1. chunk tag **必须 8 字节**（9 字符会被截断、运行时永远匹配不上）；
2. `#line` **不能插在 `#version` 之前**（GLSL 要求它是第一条语句）；
3. 参数必须生成 `uniform`，类型要做 `float4 → vec4` 映射；
4. blob 键是 `pass/feat1|feat2/stage`（分隔符在 feature **之间**，无尾分隔符）；
5. 老的 geometry-stage 跳过 hack 依赖"`#version` 后第一行是 `#if defined`"，插 `#line` 会破坏它（已在扫描前跳过 `#line` / `//`）。
6. 入口点参数叫 `m`（`void material(inout MaterialInputs m)`），shader 里原来的局部变量 `m`
   **必须改名**：`BloomDownsampling` 的采样本来就叫 `m`，改名成 `mm`（局部变量会遮蔽同名的形参，
   就地改完也能编，但读起来是灾难）；
7. `uniform float u_X = 0.5;` 这种**声明处默认值**必须翻译成 `default :`，不能直接删：
   `Grid` 的 `u_GridSize`/`u_GridOpacity`/`u_FadeDistance`、`SectionCap` 的
   `u_Albedo`/`u_environmentIntensity`、`Billboard` 的 4 个、`Standard`/`GeomertySurface`/
   `SectionFace` 的一大票 —— 全都没有任何 C++ 或 `.mat` 设过，删掉就静默变 0（网格 spacing
   变 0 → `fwidth(coord/0)`，直接坏）。只有确认每帧 `SetProperty` 的（`ssaoblur` 的
   `u_BlurDepthTolerance`）才可以删；
8. **geometry 块默认"每个变体都有"**：MoonMatc 给声明了 geometry 的材质在**所有**变体里写几何
   阶段，而老路径有个 hack —— stage 体以 `#if defined(WITH_EDGE)` 开头时，feature 没开的变体
   整段跳过。现在的规则（`WantsGeometryStage`）：**只有当整个 geometry 块的第一个非空行是
   单 feature 的守卫、且配对的 `#endif` 就是块尾**时，才按变体裁掉；其余情况一律照写（拿不准就
   保守）。`Standard` 正好是这种"整块被包住"的形状，192 变体里 96 个不带几何阶段；
   `GizmoCell`/`GeomertySurface`/`SectionFace` 的守卫在 varying 声明之后，裁不了，仍然每个变体
   一份（它们体内那个 `#else` 直通分支就是为此保留的）。真要彻底省，得让 DSL 显式声明
   "本材质几何阶段需要哪个 feature"，那是一个新字段，还没加；
9. `mix(vec3, vec3, bool)` **不是 GLSL 4.5 的重载**（选择子必须是 `bvec3`）：老代码能编纯粹是
   NVIDIA 驱动放宽了。`Standard`/`GeomertySurface`/`SectionFace` 的
   `mix(pbr, ..., u_BuiltInToneMapping)` 因此在迁移时写成 `bvec3(u_BuiltInToneMapping)`
   ——逐分量选择，结果与驱动原来的行为一致，但这是本轮唯一动了表达式的地方；
10. 整段自管的阶段**不吃 skeleton 的任何东西**：属性、varying、`FRAGMENT_COLOR`、`main` 全都
   得自己声明。混用会被链接器抓住：raw 顶点 + 生成片段 → 生成的那份
    `in VS_OUT { FragPos, TexCoords, Normal } fs_in` 会对上一个从没写过这些 varying 的顶点阶段；
11. 参数里的 `default` 值可能自带逗号（`vec4(1.0, 2.0, 3.0, 4.0)`），参数表扫描器必须按括号
    深度找分隔逗号，否则 `default` 会被截成 `vec4(1.0`；
12. `#include` 现在是**构建期**展开的，每个文件每个阶段只展开一次，且
    `EngineUBO.glsl`/`Utils.glsl` 因为 prelude 自带会被丢掉——所以老文件里那两行 include
    留着是**静默无害**的（不会重复声明），但建议删掉。


---

## 9. 已定的三个问题（2026-10 决策）

| 问题 | 决定 |
|---|---|
| `materialParams` 命名 | **先简单来**：参数名保持现有 `u_Albedo` / `u_Roughness` 这类，不引入 `materialParams.baseColor` 结构体访问，`SetProperty` 调用点不用改，先把流程跑通；重构后期再迁到 Filament 的形式 |
| `.ovshader` 内容 | **保留最终 GLSL 文本**（体积换可读性、可调试、`--dump-glsl` 可导出）；不出 SPIR-V（GL 4.5 消费不了） |
| 反射输出 | **同时输出 JSON**（`--dump-json`，等价 `matc -r`），材质编辑器直接吃 JSON，不必解析二进制 chunk |

## 10. 与 Filament 的已知差异（明确记录，避免误以为抄漏）

- **变体枚举**：Filament 是固定枚举（`determineSurfaceVariants`），我们第一版仍是 feature 的幂集；
  `Standard` 现在是 3 pass × 2^6 feature = **192 个变体**（体积问题已用 §5 的 `SHDRSTAG` 解决，
  但变体数本身没有收敛）。固定枚举 + `variantFilter` 是下一件该做的事。
- **路径追踪 4 个 shader 不迁**（§8 "剩余"）：Filament 没有"运行时往源码插 define 再编译"的机制，
  它靠固定变体枚举 + 材质实例；我们的 PathTrace 恰好靠前者，所以永久留在旧路径上，等路径追踪器
  重构时连加载方式一起改。这也是整个迁移唯一未收口的地方。
- **深度/阴影 pass**：Filament 用 `DEP` 变体位，深度变体只编顶点阶段、片元阶段整个跳过；我们仍保留
  `passes : [ SHADOW_PASS ]` 显示声明，片元阶段照编（`Unlit` 今天就是靠 `#if defined(SHADOW_PASS)` 变空）。
  等变体模型落地后统一。
- **整段自管的阶段**（§2.4）：Filament 把变换锁死在生成的 `main()` 里，材质只能改属性；我们允许
  `vertex { void main() }` / `fragment { void main() }` 全权接管。这条是为救现有 8 个"自己写
  `gl_Position`/自带 varying"的 shader 而加的，代价是材料不再受骨架约束。Filament 的等价物是
  CPU 侧预变换（billboard）或自定义 vertex domain，属于以后可以逐个收回的债。
- **geometry 阶段**：Filament 的材质模型没有第三阶段（它靠 CPU/顶点着色器做线宽展开）。我们保留了
  GL 的几何阶段（`geometry { }`），因为 `GizmoLine`/`GeomertySurface`/`SectionFace`/`GizmoCell`/
  `SectionContour` 的边缘展开全在里面。这是永久性差异，不是待办。附带一条自创规则：geometry 块若
  整块被单个 feature 守卫包住，就只在该 feature 开的变体里生成（老 loader 的 skip hack 的等价物，
  见 §8 坑 8）——Filament 没有几何阶段，自然也没有这条。
- **参数默认值**（§2.4 的 `default`）：Filament 用 `variables` + 材质实例，我们直接把 GLSL 初始化值
  写回 uniform 声明，靠 GL 把初值反射进属性表（`Material::UpdateProperties`）。
- **材质 DSL**：Filament 的 `material { }` 有几十个字段（`requires` / `variables` / `subpass` / `stereoscopicType` …），
  我们只实现用得到的那些，字段名保持一致，后续按需补。

---

## 11. 材质编辑器（管线验证）

`Moon/editor/UI/MaterialPanel/MaterialEditorPanel`：dock 面板，选一个 `Engine/Shaders` 下的
`.shader`、改源码、Apply 热重载。它存在的意义是让“构建期出包 → 运行时只读包”这条链在编辑器里
跑通一遍，顺带证明反射/错误回指都真的能用。

链路：

```
源码 ──Moon::Matc::CompileSource（进程内，MoonMatcLib）──► 包字节
     ──ShaderLoader::CreateFromPackage（视口上下文里编 GLSL）──► Shader*
     ──Material::SetShader + 场景里用着 Shader 的材质一起换 ──► 视口/预览立刻更新
```

三条经验（都踩过）：

1. **编译 GLSL 必须用 current 的 GL 上下文**。面板是 Qt 回调，上下文归视口，所以“编译 Shader”
   / “渲预览”都走 `ViewerWidget` 的两个入口（`CreateShaderFromPackage` / `RenderMaterialPreview`），
   面板只拿 `Shader*` 与 `QImage`。直接在按钮回调里 `glCompileShader` 会把事件循环挂死
   （进程还在、画面停住），很难查。
2. **构造期不要 `setFloating(true)`**，也不要在此之前碰 GL——dock 面板是在编辑器面板还没建完时
   构造的，这两种操作都会让启动挂住。预览要等视口上下文就绪：面板用 300 ms 轮询
   `ViewerWidget::HasGLContext()`，就绪后再做第一次预览；
3. **NVIDIA 驱动不认 `GL_GOOGLE_cpp_style_line_directive`**，只给一条 warning 并忽略 `#line` 里的
   文件名。但实测报错仍然是 `Unlit.shader(39) : error C0000: ...`（编译器用的是 `#line` 的行号部分）。
   构建期 glslang 则是完整支持（`ssaoblur.shader:27` 那种）。所以行号回指在两个端都成立，只是
   驱动侧会多一行无害告警。

预览用的是 `PreviewView`：一个只属于它自己的小 `Scene`（默认球体，可切立方体/平面），灯架按相机
位置布置，后处理栈和视口同一条；面板用 `FramePaster` 把模块渲染出的帧贴到
`MaterialPreviewWidget` 上。和视口走同一个 `SceneRenderer`，屏幕上的差异就只可能来自材质本身，
而不是第二套渲染器。
