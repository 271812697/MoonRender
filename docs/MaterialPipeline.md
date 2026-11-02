# Material 流水线重构：离线生成 + 运行时 Material/MaterialInstance

目标是 Filament 那条流水线，落到我们的仓库：

```
.shader(.mat DSL) → MoonMatc → package → packagelist → resgen → 生成的 .h/.cpp（内嵌）
                                                                     ↓
                                            MaterialBuilder → Material → MaterialInstance
```

只关心 **PerMaterial** 一个描述符集合/布局；PerView 的东西（engine UBO、灯光 SSBO、阴影、IBL）这轮不动。

## 1. 离线生成：文件与类

参考 Filament 的三处划分：`libs/filament-matp`（解析）、`libs/filamat`（生成与打包）、
`libs/filabridge`（运行时共享的布局类型）。我们现在这些逻辑全在 `Tools/MoonMatc/src/Matc.cpp`
（约 2500 行）一个文件里，按下面拆开。

### 1.1 解析（对应 filament-matp）

| 文件 | 类 / 职责 |
|---|---|
| `Tools/MoonMatc/src/MaterialParser.h/.cpp` | `MaterialParser`：词法 + 按块分派。成员 `mConfigProcessor: map<string, MaterialConfigProcessor>`，键是 `material` / `vertex` / `geometry` / `fragment`，值是成员函数指针；每个函数把该块交给专门的处理器，并持有 `MaterialBuilder&` 累积结果 |
| `Tools/MoonMatc/src/ParametersProcessor.h/.cpp` | `ParametersProcessor`：`material` 块里每个参数（type/name/default/label/group/min/max/step/color/ui…）的回调表 `mParameters: map<string, ParameterProcessor>`；回调签名 `void(ParametersProcessor&, MaterialBuilder&)` |
| `Tools/MoonMatc/src/MaterialBuilder.h/.cpp` | 解析期唯一的累积对象：name / shadingModel / blending / culling / depth / **parameters（按 4 个域分桶）** / features / defaultFeatures / variantFilter / passes / 各 stage 源码 / 常量。`build()` 产出 `Package` |
| `Tools/MoonMatc/src/Includes.h/.cpp` | `#include ":Shaders/..."` 的解析与拼接（现在混在 Matc.cpp 里） |
| `Tools/MoonMatc/src/Matc.cpp` | 只留 CLI：参数解析、`MaterialParser` 驱动、错误输出、`--dump-glsl` |

四个参数域各自独立解析器（Filament 的分法）：

- `material` 域 → **PerMaterial** 的 UBO 与 sampler 声明；
- `vertex` / `geometry` / `fragment` 域 → 各自 stage 的“域级”参数（我们的 GLSL 里目前只有 material 域有参数，但结构先按四个域摆好，解析器互不依赖）。

### 1.2 生成与打包（对应 filamat）

| 文件 | 类 / 职责 |
|---|---|
| `Tools/MoonMatc/src/ShaderGenerator.h/.cpp` | `ShaderGenerator`：定义“一个变体怎么生成代码”的流程——prelude → 变体 `#define`（variant bits + features + block 参数）→ 各 stage 的 `material{}` / `vertex{}` / `geometry{}` / `fragment{}` 主体 → 行指令。`MaterialBuilder` 调它逐变体生成 |
| `Tools/MoonMatc/src/MaterialVariants.h/.cpp` | 变体枚举：passes × defaultFeatures × variantFilter 的幂集，产出“变体键 → 需要定义的宏集合” |
| `Tools/MoonMatc/src/UibGenerator.h/.cpp` | 从 `material` 域参数生成 **UboBlockInfo**：std140 offset/stride/size、总大小、每个参数的类型与 precision |
| `Tools/MoonMatc/src/SibGenerator.h/.cpp` | 从 sampler 参数生成 **SamplerBlockInfo**：参数名 → 单元（binding）、sampler 类型、fallback 种类 |
| `Tools/MoonMatc/src/DescriptorSetLayout.h/.cpp` | PerMaterial 的布局描述：`{BindingKind, binding, stages}` 列表（UboBlockInfo 占一个 binding，每个 sampler 各占一个），`Validate(resource) const` 供运行时校验 |
| `Tools/MoonMatc/src/Chunk.h/.cpp`、`ChunkContainer.h/.cpp` | 包的分块容器：type + 长度 + 数据；读取端按 type 分派 |
| `Tools/MoonMatc/src/LineDictionary.h/.cpp`、`BlobDictionary.h/.cpp` | **着色器代码压缩**：行字典去重 + 字符串 blob 表（Filament 的做法）。生成端把源码切成行、编号、只存唯一行；读取端还原 |
| `Tools/MoonMatc/src/MaterialPackageWriter.h/.cpp` | 把 `MaterialBuilder::build()` 的结果写成分块包（见 §2） |

### 1.3 resgen

| 文件 | 职责 |
|---|---|
| `Tools/ShaderPack`（现有） | 保留为我们的 resgen：把所有 package 拼成一个 blob，生成 `shader.h/.cpp`（内嵌）；文件名/接口保持，后续可改名为 `MaterialPack`，但不影响功能 |

## 2. package 格式（v2）

分块容器，每块 `{uint32 type, uint32 length, bytes}`：

| chunk | 内容 |
|---|---|
| `ChunkStageText` | 每个变体、每个 stage 的行字典编码源码（引用行号） |
| `ChunkLineDictionary` | 唯一行表（UTF-8，去重后） |
| `ChunkVariantTable` | 变体键（pass + feature 集合）→ 该变体的 stage 行号区间 |
| `ChunkMaterialState` | 渲染状态：blending / culling / depthWrite / depthTest / polygonOffset / 双面 / 透明模式 …（现在 `Shader::State` 的字段 + `.mat` settings 的字段） |
| `ChunkUboBlockInfo` | 名称、总大小、参数表 `{name, type, offset, stride, count, precision}` |
| `ChunkSamplerBlockInfo` | 名称、参数表 `{name, binding, samplerType, fallback}` |
| `ChunkDescriptorSetLayout` | PerMaterial 布局：`{kind, binding, stages}`（UBO 一块 + 每个 sampler 一块） |
| `ChunkShaderInfo` | 版本号、name、shadingModel、features/filter/passes 元数据 |

压缩策略：先用**行字典 + blob 表**（零外部依赖，Filament 同款，通常能压掉 40–60% 的重复 prelude）；
后续若需要再对行字典整体做 LZ4。我们 `Extern` 里没有 zstd/lz4，Tracy 自带一份 LZ4 但不应跨库依赖——
要引入就以单文件形式放进 `Extern/lz4`。

## 3. 运行时：三个类 + 描述符集

| 文件（MoonRender） | 类 / 职责 |
|---|---|
| `include/Rendering/Resources/MaterialDefinition.h` + `src/.../MaterialDefinition.cpp` | 从 package 反序列化出的**不可变数据**：`UboBlockInfo` / `SamplerBlockInfo` / `DescriptorSetLayout` / 渲染状态 / 变体源码。只回答“形状”，不持有 GL 对象、不持有值 |
| `include/Rendering/Resources/Material.h` + `.cpp` | 着色效果的**模板**：持有 `MaterialDefinition`、每个变体编译好的 `ShaderProgram`（惰性）、默认实例。由 `MaterialBuilder` 从 package 构造 |
| `include/Rendering/Resources/MaterialInstance.h` + `.cpp` | 模板的一个特例：**一个 PerMaterial `DescriptorSet`** + 参数写入接口。`SetParameter(name, value)` 通过 `Material` 的 `UboBlockInfo` 查 offset 写进 CPU 块；`SetTexture(name, tex)` 通过 `SamplerBlockInfo` 查 binding；`Commit()` 上传脏块与变更的采样器；`Bind()` 绑描述符集 + program |
| `include/Rendering/Data/DescriptorSetLayout.h` + `.cpp` | 布局：槽位 → 资源类型；`Validate(binding, kind)`；`MaxBinding()` |
| `include/Rendering/Data/DescriptorSet.h` + `.cpp` | 实际绑定集合：`SetBuffer` / `SetTexture`（塞入时按 `DescriptorSetLayout` 校验类型）、`Commit()`、`Bind()` |
| `include/Rendering/Resources/MaterialBuilder.h` + `.cpp` | 运行时构建器：`Package(const void*, size_t)` → `Build()` → `Material`（解析 package、编译程序、建默认实例） |
| `include/Rendering/Resources/MaterialParser.h` + `.cpp` | 包的反序列化（chunk 分派），只产出 `MaterialDefinition` |

数据流：

```
package bytes
   → MaterialParser            （chunk → MaterialDefinition）
   → MaterialBuilder::Build()  （定义 + 编译各变体 program → Material）
   → Material::CreateInstance()（→ MaterialInstance：一个 PerMaterial DescriptorSet + 值块）
   → MaterialInstance::SetParameter/SetTexture … Commit() → Bind()
```

`Material` 与 `MaterialInstance` 的关系（按你的第 2.2 条）：

- `Material`：模板。package 反序列化出的渲染状态、着色相关材质信息（shading model、features）、描述符布局、
  变体程序集合、默认实例。
- `MaterialInstance`：特例。持有一个 PerMaterial 描述符集；`SetParameter` 用 `UboBlockInfo`、
  `SetTexture` 用 `SamplerBlockInfo` 找到槽位并写入；draw 时不同实例只是描述符集里的内容不同。

## 4. 分阶段落地（每阶段可构建、可对照）

1. **编译器拆文件**（不改格式）：把 `Matc.cpp` 拆成 §1.1 的解析三件套 + `Includes`，`Matc.cpp` 只留 CLI。
   验证：同一批 `.shader` 生成的 package 与拆分前**逐字节一致**，`--dump-glsl` 输出一致（glslang 已在校验）。
2. **生成器与块信息**：引入 `ShaderGenerator` / `MaterialVariants` / `UibGenerator` / `SibGenerator`，
   把变体枚举与 prelude 拼接从 `MaterialBuilder` 里抽走；产出 `UboBlockInfo` / `SamplerBlockInfo`
   并在 `--dump-json` 里可见。验证：变体数、GLSL 逐字节不变。
3. **package v2 + 行字典**：实现 chunk 容器与行字典，写端切到 v2；读取端 `MaterialParser` 落地。
   验证：同一 shader 的 v1/v2 解出的 stage 源码逐字节相同，包体积对比。
4. **运行时类型**：`DescriptorSetLayout` / `DescriptorSet` / `MaterialDefinition` / `Material` /
   `MaterialInstance` / `MaterialBuilder`，先与原 `Shader` + `Data::Material` **并存**（开关切换），
   在一个 pass（Opaques）上跑通。验证：6.moon 截图像素对比 + `MOON_DEBUG_SAMPLERS`。
5. **切换与清理**：全部 pass 走新路径；删除 `Data::Material`/`Data::MaterialInstance` 的名字表、
   `MaterialSignatureSet`、`m_seenDefinitionRevision` 补丁；`.mat` 资产改为“给 MaterialInstance 预设值”。
6. **收尾**：状态收口成 `StateMask`；文档与 dump 工具同步。

## 5. 待你确认

1. **压缩**：先用行字典（零依赖）可以吗？还是这轮就要 LZ4（需要把单文件 LZ4 加进 `Extern`）。
2. **`.mat` 的角色**：这轮是否只做“shader → package → Material/MaterialInstance”，`.mat`（资产 XML）暂时原样保留、
   只把它的值写进实例？还是同时把 `.mat` 也改成构建期产物（上一轮 A/B 里的 B 方案）。
3. **PerView 组**：这轮明确不做（engine UBO / lights / shadow / IBL 继续走现有的绑定方式），我按这个理解推进。
