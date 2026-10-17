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
| `MoonMatc`（对标 `matc`）、新 `.ovfx` 语法、chunk 包、运行时 chunk reader | ⬜ 本文内容 |

---

## 1. 流水线（三级，一一对应）

```
X.ovfx ──[MoonMatc]──► X.ovshader ──[ShaderPack]──► shader.h + shader.cpp ──► 链接
        逐文件            每个 .ovfx 一个包        多包合并（已有）          进 exe
                            │
                            └─ 运行时：读 chunk → glShaderSource → glCompileShader
```

| Filament | 我们 | 说明 |
|---|---|---|
| `tools/matc` + `libs/filamat` | `Tools/MoonMatc` | 解析 + 变体 + 校验 + 反射 + 打包 |
| `.filamat` | `.ovshader` | chunk 容器 |
| `tools/resgen` | `Tools/ShaderPack` | 已实现，收窄为"只合并" |
| `filament/src/MaterialParser.cpp` + `filaflat/DictionaryReader` | `ShaderManager` 内的 chunk reader | 运行时只读、不解析源码 |

**收益**：`.ovfxh` 不再发布（prelude 编进 matc、include 构建期展开）；`shader.cpp` 里不再有
明文源码，只有编译包；运行时不再做 `2^n` 变体全组合、不再拼宏、不再扫文本。

---

## 2. 新 `.ovfx` 语法

照 `lit.mat`（`android/samples/sample-lit-cube/.../lit.mat`）的四个部分。文件名仍为 `.ovfx`，
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
- `#include ":Shaders/Common/....ovfxh"` 仍允许，但 **prelude 不再需要手写 include**
  （见 §3）。

### 2.2 `MaterialInputs`（照抄 `shaders/src/surface_material_inputs.fs`）

按 `shadingModel` 裁剪字段——Filament 就是这么做的（`#if !defined(SHADING_MODEL_UNLIT)` 等）：

| 字段 | 出现条件 | 我们的 `Standard.ovfx` 今天怎么写的 |
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

---

## 3. prelude：我们的"生成骨架"

Filament 的骨架在 `filament/src/materials/`（`surface_shading_model_standard.fs`、
`surface_lighting.fs`、`surface_shadowing.fs` …），由 matc 拼进去。我们的骨架就是现有的
`Shaders/Common/*.ovfxh` + `Standard.ovfx` 里那段光照，整理成 matc 内置文件：

| prelude | 来源 | 内容 |
|---|---|---|
| `prelude/buffers.ovfxh` | `Common/Buffers/EngineUBO.ovfxh`、`LightsSSBO.ovfxh`、`ReflectionUBO.ovfxh` | 帧/灯/反射 UBO |
| `prelude/common.ovfxh` | `Common/Constants.ovfxh`、`Utils.ovfxh`、`Physics.ovfxh` | 常量与工具函数 |
| `prelude/material.ovfxh` | 新写（照 `surface_material_inputs.fs`） | `MaterialInputs` / `prepareMaterial()` / `materialParams` |
| `prelude/surface.ovfxh` | `Standard.ovfx` 的光照段 + `Lighting/PBR.ovfxh`、`IBL.ovfxh`、`Shadow.ovfxh` | 由 `shadingModel` 选中的着色模型 |
| `prelude/variants.ovfxh` | 新写（照 `Variant.h`） | 变体位对应的 `#define` |
| `prelude/output.ovfxh` | `Standard.ovfx` 末尾 | 色调映射/alpha 等收尾 |

matc 拼接顺序（照 Filament）：`#version` → 顶点/片元属性 → 变体位 `#define` →
`buffers` → `common` → `material` → `surface` → 用户入口函数（`main` 之前）→ 生成的 `main()`。

---

## 4. 变体模型（照 `Variant.h` + `MaterialVariants.cpp`）

Filament：8 位掩码 + 白名单 + **固定枚举**（`determineSurfaceVariants(filter, isLit, ...)`），
**从不做 2^n 全组合**。我们现在 `Standard.ovfx` 的 64 个变体就是这么来的（`AssembleShader` 的
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
`CreateFromSource`（PathTrace 在用）暂时保留，等那 4 个 shader 也迁移后再删。

---

## 7. 迁移计划（垂直切片优先）

1. **切片**：`Unlit.ovfx` 走通全链路（新语法 → MoonMatc → `.ovshader` → ShaderPack →
   运行时读 chunk → 画面正常）。此阶段新旧两套并存。
2. 后处理类（Bloom / FXAA / ToneMap / HZB / Blit …）：`shadingModel : unlit` + 全屏 pass，
   体量小、风险低——照 `filament/src/materials/` 里同名的 `.mat` 抄结构。
3. 工具类（Grid / Line / Picking / Section / Skybox）：大多也是 unlit，同上。
4. `Standard.ovfx` 最后做（PBR/IBL/阴影/变体最多）。
5. 全部迁完后再删旧解析路径，并把 `.ovfx` 改名为 `.mat`。

---

## 8. 已定的三个问题（2026-10 决策）

| 问题 | 决定 |
|---|---|
| `materialParams` 命名 | **先简单来**：参数名保持现有 `u_Albedo` / `u_Roughness` 这类，不引入 `materialParams.baseColor` 结构体访问，`SetProperty` 调用点不用改，先把流程跑通；重构后期再迁到 Filament 的形式 |
| `.ovshader` 内容 | **保留最终 GLSL 文本**（体积换可读性、可调试、`--dump-glsl` 可导出）；不出 SPIR-V（GL 4.5 消费不了） |
| 反射输出 | **同时输出 JSON**（`--dump-json`，等价 `matc -r`），材质编辑器直接吃 JSON，不必解析二进制 chunk |

## 9. 与 Filament 的已知差异（明确记录，避免误以为抄漏）

- **变体枚举**：Filament 是固定枚举（`determineSurfaceVariants`），我们第一版仍是 feature 的幂集（`Unlit` 有 1 个 feature = 2 个变体，可接受）；固定枚举随变体模型一起做。
- **深度/阴影 pass**：Filament 用 `DEP` 变体位，深度变体只编顶点阶段、片元阶段整个跳过；我们仍保留 `passes : [ SHADOW_PASS ]` 显示声明，片元阶段照编（`Unlit` 今天就是靠 `#if defined(SHADOW_PASS)` 变空）。等变体模型落地后统一。
- **材质 DSL**：Filament 的 `material { }` 有几十个字段（`requires` / `variables` / `subpass` / `stereoscopicType` …），我们只实现用得到的那些，字段名保持一致，后续按需补。

