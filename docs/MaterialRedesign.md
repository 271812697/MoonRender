# Material / MaterialInstance 重做：对照 Filament 的设计与迁移计划

现状的问题不是"某处写错了"，而是**职责和数据的组织方式**跟 Filament 是反的：定义与实例的关系、
参数的存在形式、上传时机、绑定点的描述，四件事都拧着。下面先按 Filament 源码把它的模型讲清楚，
再逐项对照我们的实现，最后给目标设计和分步迁移。

## 1. Filament 的模型（源码依据）

### 1.1 Material：不可变的编译产物

`filament/src/details/Material.h`

- 持有 `MaterialDefinition const&`：matc/filamat 解析出的包——uniform interface block（每个参数的
  name / offset / size / type / precision）、sampler interface block（name → **descriptor binding**、
  sampler 类型）、render state（blending / culling / depth / …）、variant 列表。
- `mCachedPrograms`：每个 variant 一份 `HwProgram`，`getProgram(variant)` 取。
- `DescriptorSetLayout const& getDescriptorSetLayout(Variant)`：**布局属于材质**（每个 variant 一份）。
- `getDefaultInstance()`：材质自带一个默认实例；没有单独建实例的 draw 用它。
- 参数只提供"查表"能力（`getUniformSize()`、按名字查 offset），**自己不存值**。

### 1.2 MaterialInstance：可变的值 + 自己的描述符集

`filament/src/details/MaterialInstance.h`（成员区）与 `.cpp`

```cpp
FMaterial const* mMaterial;
backend::Handle<HwBufferObject> mUbHandle;      // 每实例一块 GPU UBO，大小 = 材质 uniform size
tsl::robin_map<descriptor_binding_t, TextureParameter> mTextureParameters;  // 按 binding 存
mutable DescriptorSet mDescriptorSet;           // 由材质的 layout 惰性创建
UniformBuffer mUniforms;                        // CPU 侧数据 + dirty
```

- `setParameter(name, value)`：查 offset → 写字节 → 标脏（`MaterialInstance.cpp` 的
  `setParameterImpl` 系列）。**没有按名字的属性表**，值就是一块 std140 内存。
- `commit(driver)`：`if (mUniforms.isDirty()) driver.updateBufferObject(mUbHandle, …)`；采样器有变化时
  `mDescriptorSet.setSampler(layout, …)` 再 `commit`。**只有脏了才上传**，且只上传一块。
- `use(driver, variant)`：绑 program，然后
  `mDescriptorSet.bind(driver, DescriptorSetBindingPoints::PER_MATERIAL)`。
- `fixMissingSamplers()`：布局要求的采样器槽**必须**有东西（Vulkan/Metal 的硬约束），缺的用引擎占位
  纹理补上并每实例警告一次。GL 宽松，但接口按严格后端设计。
- 渲染状态（culling / depth func / write / stencil / polygon offset / scissor / transparency mode /
  mask threshold / specular AA）以位域形式存在实例上，另有 sorting key。

### 1.3 描述符集与布局

- `filament/src/ds/DescriptorSetLayout.h`：按 binding 索引的 `DescriptorType` 表、`mSamplers[]` /
  `mUniformBuffers[]` 位掩码、`getMaxDescriptorBinding()`、`isSampler(binding)`。
- `filament/src/ds/DescriptorSet.h`：`setBuffer(layout, …)` / `setSampler(layout, …)` / `commit(layout, driver)`
  / `bind(driver, setPoint)`。
- 作用域是显式的：`PER_VIEW`（view UBO、灯光、阴影、IBL……由渲染器统一提供）和 `PER_MATERIAL`
  （实例 UBO + 采样器）。材质只声明自己那一组。
- 关键点：**draw 时不"按名字 SetUniform"**。所有绑定都是"一个 layout + 一个 set + 一次 bind"。

## 2. 我们现在的实现（对照）

| 项 | 我们（文件:行） | Filament | 后果 |
|---|---|---|---|
| 定义与实例的关系 | `Material : public MaterialInstance`（`Material.h:22`），`m_definition == nullptr` 表示"我就是定义" | `Material` 组合一个 default instance，二者无继承 | `FindProperty` / `ShaderOf` / `FeaturesOf` 三个虚函数做回落；`Material` 自己也带参数切片和属性表，"默认实例"这个概念藏在继承里，`Core::Resources::Material` 又派生自它，链路越来越绕 |
| 参数的存法 | `std::map<std::string, MaterialProperty>` + `std::variant`（`MaterialInstance.h:67,124`） | 一块 std140 内存 + 按名字查 offset | 每次 `SetProperty` 是 map 查找 + variant 赋值 + 全局版本号自增；值没有"位置"概念，只能整块重写 |
| 上传时机 | 三个 hash 组成 `MaterialSignatureSet`（`Material.cpp:1359`），渲染器比较后决定是否 `UploadProperties`（`ABaseRenderer.cpp:424-446`） | 实例自己 dirty → `commit()` 只传脏块 | 定义（.mat）改动不会被实例发现，因为"资产 revision 变了"的检查写在 `UploadProperties` 里，而它要签名先变才会被调用（我上一轮把 `m_definition->Revision()` 混进签名，那是个补丁，不是模型） |
| 采样器 | 每次上传遍历 `ShaderOf()->GetParameters()`、按名找属性、按 `parameter.binding` 绑；fallback 由渲染器作为参数传进来 | 实例按 binding 存 `(Texture*, SamplerParams)`，缺失的在 `commit` 里补占位 | `_BRDFLut` / `_IrradianceCube` / `_PrefilterCube` 必须"每 draw 再绑一次"（`BindGlobalTexture`，`ABaseRenderer.cpp:455-461`），否则首帧绑到的空纹理永远留着——典型症状 |
| 绑定点的描述 | 散落常量：`kMaterialParameterBinding`、engine UBO、lights SSBO、object UBO 各自一个（`Settings/EBindingPoint.h`），GLSL 里 `layout(binding=N)` 靠人工对齐 | `DescriptorSetLayout` 由包反射生成，绑定点是 layout 的一部分 | 加一个块/换一个块要同时改 GLSL 与 C++ 常量；没有"这个材质需要哪些绑定"的单一事实来源 |
| 渲染状态 | 19 个 `std::optional<>` + 每个 Get 外联回落（`MaterialInstance.h:252-270`，`GetDrawOrder` 等在 .cpp） | 位域 + 一份 `RasterState`/`StencilState` | 样板代码多，`GenerateStateMask()` 之外还有一堆 `GetX()`；新增一个状态要动 4 个地方 |
| Features | 放在实例上（`MaterialInstance.h:237`），`Bind()` 里还兼做 variant 选择 | 放材质上；实例只覆盖值 | `Bind()` 同时干四件事（选 program、绑 program、算签名、绑参数块），调用方很难用对 |
| 每帧临时参数 | `singleUse` 标记 + 上传后回写默认值（`Material.cpp:791-813`） | per-view 描述符集 | "一帧内有效"的值靠回写兜住，漏一处就粘住 |

## 3. 目标设计（面向我们的 GL 后端，但按严格后端的形状设计）

### 3.1 三层，职责单一

```
Material           不可变：包 + layout + 默认状态 + 每个 variant 的 program + default instance
MaterialInstance   可变：一块 std140 值内存（+ dirty 区间）、采样器槽、状态覆盖
BindingLayout/Set  绑定契约：材质声明 PER_MATERIAL 组，渲染器声明 PER_VIEW 组
```

**BindingLayout / BindingSet**（GL 下是平坦 binding point，Vulkan 下是真描述符集）：

```cpp
enum class BindingKind { UniformBuffer, StorageBuffer, Texture };
struct BindingEntry { BindingKind kind; uint32_t binding; ShaderStage stages; };
class BindingLayout { std::vector<BindingEntry> m_entries; uint32_t MaxBinding() const; };
class BindingSet {                     // 一个 layout 的一组实际绑定
    void SetBuffer(uint32_t binding, HAL::Buffer* b, uint32_t offset, uint32_t size);
    void SetTexture(uint32_t binding, HAL::Texture* t);
    void Commit();                     // GL: glBindBufferRange / glBindTextureUnit
};
```

- `ShaderPackage` 已经反射出了 `Parameter::offset/stride/binding/fallback`——材质构建时把它整理成
  `UniformLayout`（offset/size/type）+ `SamplerLayout`（unit/fallback）+ `BindingLayout`，成为唯一事实来源。
- **PER_VIEW 组**：engine UBO、lights SSBO、shadow map + lightSpaceMatrix、`_BRDFLut`、`_IrradianceCube`、
  `_PrefilterCube`。一次性提交，不再每 draw 重绑。

### 3.2 MaterialInstance 的数据与接口

```cpp
class MaterialInstance {
    Material* m_definition;
    std::vector<uint8_t> m_block;      // 大小 = definition->GetUniformLayout().Size()
    uint32_t m_dirtyBegin, m_dirtyEnd; // 脏区间（写参数时扩展，Commit 后清零）
    std::unordered_map<uint32_t /*unit*/, TextureBinding> m_textures;
    uint32_t m_textureVersion;

    template<typename T> void Set(const char* name, T const& v);  // 查 offset → 写 → 标脏
    void Commit();                                // 脏才 upload；采样器变了才重绑
    void Bind(HAL::ShaderProgram& program, const BindingSet& viewBindings);
};
```

- `SetParameter` 是 O(1) 的 offset 写；不再有 `std::map<std::string, variant>`。
- `Commit()` 是唯一的上传点；`Bind()` 只做 program + 参数块 + 采样器 + view 组。
- 采样器槽缺失时在 `Commit()` 里补占位并每实例警告一次（照 Filament 的 `fixMissingSamplers`）。
- `singleUse` 概念删除：它属于 per-view 组（阴影图、灯光矩阵由渲染器每帧写进 view 组）。
- 状态覆盖保留 optional 语义，但**只保留一份 `StateMask`**，`GetX()` 由 mask 派生。

### 3.3 Material 与默认实例

```cpp
class Material {
    Shader* m_shader;
    UniformLayout m_uniforms;  SamplerLayout m_samplers;  BindingLayout m_layout;
    std::unique_ptr<MaterialInstance> m_defaultInstance;   // 不是继承
    std::unordered_map<Variant, HAL::ShaderProgram> m_programs;
};
```

- 去掉 `Material : MaterialInstance`、去掉三个回落虚函数；`Material::SetProperty` 转给默认实例。
- `Core::Resources::Material`（.mat 资产）继续派生自 `Material`，但只负责"序列化 + 默认值"，不再碰实例。

## 4. 迁移计划（每步都可编译、可对照截图）

1. **layout 先行**（不动 API）：把 `ShaderPackage` 的反射整理成 `UniformLayout` / `SamplerLayout` /
   `BindingLayout`，`SetProperty` 改为按 offset 写进 `m_block`，生成 `UploadParameterBlock` 的等价物；
   旧路径保留一个开关。验证：同一份 6.moon 与 Gaussian 球的截图逐字节对比。
2. **上传时机**：引入 dirty 区间 + `Commit()`；`Bind()` 不再返回签名，渲染器删掉 `MaterialSignatureSet`
   比较；`m_seenDefinitionRevision` / `Revision()` 这套补丁随之删除。验证：设计器改贴图、滑条实时生效。
3. **采样器归位**：unit → `(Texture*, sampler)`；fallback 在 `Commit()` 内补；`BindGlobalTexture` 并入
   PER_VIEW 组。验证：首帧 IBL、flat normal、无贴图材质。
4. **BindingSet**：把 engine UBO / lights SSBO / object UBO / material block / 采样器的绑定点收进
   `BindingLayout`，GL 侧实现 `BindingSet::Commit()`。验证：全部 pass 的 shader 仍能拿到数据。
5. **定义/实例拆分**：`Material` 改组合 default instance，删三个虚函数与 `m_definition == nullptr` 特判；
   `Core::Resources::Material` 跟随调整。验证：全量构建 + 视口、预览、拾取、材质面板。
6. **状态收口**：19 个 optional → 一份 `StateMask` + 覆盖语义；删掉样板 `GetX()`。

## 5. 需要你拍板的点

1. **每实例一块 UBO，还是池化切片？** 现在是 `UniformBufferPool` 切一段 + `glBindBufferRange`；Filament
   是每实例一块 `HwBufferObject`。前者省驱动对象、后者更简单（`updateBufferObject` 不必带 offset）。
2. **`Data::Material` 与 `Core::Resources::Material` 的边界**：资产是否继续派生自运行时定义，还是反过来
   持有它（我倾向后者，和 `.mat` 只存"值 + 状态"的分工一致）。
3. **是否一步到位引入 `BindingSet`**，还是先做 1、2 两步（layout + dirty/commit），把描述符集留到接
   Vulkan 或需要 MRT 时再做。
4. **`singleUse` 参数的去向**：全部并入 PER_VIEW 组（我倾向），还是保留"每 draw 临时值"的能力。

## 6. 我需要先补的验证手段

- 一个离屏用例：同一材质两种参数值 → 读回帧 → 比较像素，用来锁住 1、2 步的重构（现在只能靠肉眼截图，
  而 `MOON_SHOT` 的相机取景有竞态，不适合做逐像素对比）。
- 一个 shader/layout 一致性检查：`ShaderPackage` 的 offset/binding 与实际 GLSL `layout(...)` 编译结果对账
  （Build 期 glslang 已经跑过，可以顺带把反射结果 dump 出来比对）。
