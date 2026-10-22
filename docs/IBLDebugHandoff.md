# IBL 调试交接（2026-10-21）

## 结论：IBL 预滤波的 LOD 算错了（已修）

球面“细密 hatch + 白色大块 + 大理石纹”来自 **specular prefilter 的 sample LOD**，不是
DFG LUT、不是天空盒方位、也不是曝光。

Filament `IBLPrefilterContext::SpecularFilter` 的 `omegaP` 用的是 **cubemap 的 base
尺寸**（它的源和输出都是 256：

```cpp
uint32_t dim = outReflectionsTexture->getWidth();          // 256，不随 mip 变
const float omegaP = 4*PI / (6*dim*dim);
mi->setParameter("lodOffset", options.lodOffset - log4(omegaP));
```

我们的旧代码把 `omegaP` 算成了 **当前 mip 的尺寸**：

```cpp
const float omegaP = 4*PI / (6*mipWidth*mipWidth);        // 旧：level i 再锐 i 级
```

而且我们的源 cubemap 是 1024、prefilter 输出是 256（Filament 是 256→256），源尺寸还差
`log2(1024/256) = 2` 级。于是第 i 级的 `sampleLod` 比 Filament **小 i 级**；又因为同一 LOD
在我们的 1024 源上对应更细的纹素（差 2 级），要和 Filament 的物理模糊对齐需要整体 **+2+i**：

| 级 | Filament p | Filament 采源 LOD | 旧 Moon 采源 LOD | 旧 Moon 偏差 |
|----|-----------|------------------|-----------------|-------------|
| 0  | 0.000     | 0（clamp）       | 0               | 0           |
| 1  | 0.134     | 0（clamp）       | 0（clamp）      | 0           |
| 2  | 0.293     | 1.75             | 0（clamp）      | -1.75       |
| 3  | 0.500     | 3.29             | 0.29            | -3.0        |
| 4  | 1.000     | 6.29             | 2.29            | -4.0（含 2 级源尺寸） |

第 4 级（roughness one）正是 Filament 无 SH 时拿来做 **diffuse** 的那一级；它保留了
studio 的窗帘/柔光箱结构，于是球面出现大理石纹。第 2~3 级是 roughness 0.4 的 specular
采样，源 LOD 接近 0（1024 原图），所以出现细密的重要性采样噪声/条纹和过度集中的白色高光。

## 修复

1. `SkyboxRenderPass::computeSkyTexture()`：
   - `omegaP` 改用 **源 cubemap 的 base 宽度**（1024），并且**所有 level 共用**；
   - 新增 `baseLod = log2(sourceWidth / 256) = 2`，给 mirror level（roughness 0、只有
     1 个 sample、没有核宽度）用，等价于 Filament 第 0 级对自己的 256 源做一次滤波拷贝。
2. `SkyboxPrefilter.shader`：加 `baseLod` 参数，`sampleLod = (roughness<=0.0001) ? baseLod : l + lodOffset`。
3. prefilter FBO 只 attach **一个** COLOR（当前 face），不再 attach 6 个 face。原来 6 个
   face 只在“一次画 6 个 face”时需要；逐 face 绘制时，某些驱动会把 framebuffer 尺寸取成
   最小 attachment，per-mip 切换 level 时会裁掉大 mip 的绘制/回读（本次 dump 就踩到了）。
4. `BrdfLut` 从 1024² 改为 128²（Filament 的 `dfg.bin` 是 128×128×3 half）；DFG 是平滑的
   两参数函数，1024² 没有多存信息，只是把一次性积分放大了 64 倍。

## 验证（已做）

- 从 `SkyboxRenderPass` 临时 dump 了 prefilter 5 级 × 6 face 的原始 float；修复后每级都是
  完整 face，能量随 level 收敛（+Z face 的 max）：

  | level | 0 | 1 | 2 | 3 | 4 |
  |-------|---|---|---|---|---|
  | max   | 488.7 | 304.8 | 38.7 | 6.37 | 1.81 |
  | mean  | 1.44 | 1.45 | 1.32 | 1.10 | 1.01 |

  level 4 已经是平滑的 studio 渐变，没有窗帘/灯的结构；level 0 仍是清晰的 studio。
- DFG LUT dump：`NoV=0.5, p=0.5` 读到 `(R,G) = (0.832, 0.0215)`，与 Filament `DFV` 参考
  积分 `(A,B) = (0.8339, 0.0216)` 一致。Filament 的 LUT 存 `(B, A+B)`、shader 用
  `mix(x,y,f0)`；Moon 存 `(A,B)`、shader 用 `f0*A+B`，两者等价，**不用改公式**。
- `cmake --build Build --target Moon --config Release -- /nologo /m:1` 通过。

## 第二轮：预览 IBL 强度（已修）

结构对上之后，两颗球的**形状/高光位置**基本一致，但 Moon 整颗球更亮。把两张截图反推过
ACES 之后（engine 的 `Tonemapping.shader` 是 Filament ACES，输入先乘 `_Exposure`）：

| | 截图 sRGB（球均值） | 反推 pre-ACES 场景值 |
|---|---|---|
| Qt-Filament | 146 | 0.315 |
| Moon（intensity 4.0） | 189 | 0.571 |

Filament：`IndirectLight` 20000 lux × 默认相机曝光（1/125s、f/16、ISO100 = 2.604e-5）
= `iblLuminance 0.521`。Moon：引擎 brightness `0.2`（`EngineBufferRenderFeature`）×
bake `4.0` × 后处理曝光 `1.2` = **0.96**，正好是 1.84 倍；差值 `0.571-0.315=0.256`
与 IBL 部分的差值一致（太阳项在两边相同，相减抵消）。

修正：`PreviewView::SetEnvironmentIntensity(4.0)` → **`2.2`**（`0.521/(0.2*1.2)=2.17`）。
旧的 4.0 是拿“过锐的坏球”标出来的；mip 修好后它就偏亮了。

太阳同理：Filament 40000 lux × 2.604e-5 = 1.042；Moon 的 tonemapper 之后又乘 1.2，所以
`CDirectionalLight` 的强度应是 1.042/1.2 = **0.87**（原来 1.0，偏亮 17%，也让太阳/IBL
比例从 Filament 的 2:1 变成 2.3:1）。修正后 0.87×1.2=1.044，IBL 0.2×2.2×1.2=0.528，
比例回到 1.98:1。

## 第三轮观测：亮度已对，剩下的是“图案/对比”，先排除机位

2026-10-21 09:5x 的截图（intensity 2.2 / sun 0.87）实测：

| | 球均值（display-linear） | 中位数 | std / CV | 最亮 0.5% 质心 (x,y) |
|---|---|---|---|---|
| Qt-Filament | 0.295 | 0.307 | 0.092 / 0.35 | (-0.45, **+0.65**) |
| Moon 2.2 | 0.269 | 0.248 | 0.167 / 0.61 | (-0.27, **-0.29**) |

- 均值差 9%，2.2 基本对（要抠可到 2.4）。
- **对比度仍是 1.8 倍**，但两张截图的亮区质心 **x 几乎一样（-0.08 vs -0.09），y 上下相反**
  （Moon 在中心下方、Filament 在上方）。IBL 强度错误不会把图案上下翻转，这更像**相机俯仰/
  环绕不同**（两个预览的相机都可独立 orbit），或者环境 cube 的竖直朝向不同。
- 因此下一步先做**受控 A/B**：两边都点 **Reset View**（Filament `orbitHomePosition` 也是
  1.50, 0.82, 3.65），确认 IBL Rotation 0、Qt 侧无 bloom/额外后处理，再截图比。
- 若 reset 后亮斑位置仍上下相反，再查环境 cube 的 v 方向（本文件上面的“环境 cube 方位”
  已用 SkyID 验证过；但那次验证的是公式，不是实际纹理上传的翻转）。
- 若位置对上、对比仍大，再逐级比对 prefilter：我们 level 2 的全局 max 51.9、level 4 max
  1.81（`Build/_ibl_dump_single2/`）；需要 Filament 侧 `mReflectionCubemap` 的同样统计。

## 回归修复：FilamentPBR 的 diffuse IBL 改回 irradiance cube（2026-10-21）

现象：Metallic 0 / Roughness 0 时球是**黑底 + 白高光**，diffuse 完全没出来；同一面板
Base Color #b8b8b8、Albedo Map None。sampler2D 的空纹理回退是**白色**
（`ABaseRenderer.cpp` 的 `m_emptyTexture2D` 上传 `kWhitePixel`），所以不是贴图回退变黑。

diffuse 这一路之前改成采样 prefilter 的 roughness-one mip（`diffuseIrradiance()`），
在预览里结果是 0/黑；高光（mip 0）正常，所以只剩黑底 + 白高光。已改回：
`Fd = pixel.diffuseColor * texture(irradianceCube, n).rgb * ...`，即引擎一直可用的
余弦卷积 irradiance cube（Lambert kernel 已含 1/π，和 Standard 路径一致）。
`diffuseIrradiance()` 保留未用，后续若要回到 Filament 的 iblRoughnessOneLevel 再查
preview 自己那份 prefilter mip 是否真的生成了。

## 第四轮：对比的根本前提错了——Qt 侧预览的不是 FilamentPBR

2026-10-21 两张截图并排看：

- Moon 左侧列表选中的是 **"Filamen..."**（`FilamentPBR.shader`），Metallic **1**、Roughness 0；
- Qt-Filament 右侧标题写的是 **"Previewing: Assimp Default PBR"**，左侧列表高亮的是第一项
  **"Atmosph..."**（Atmosphere/Assimp 内置材质），Metallic **0**、Roughness 0，而且面板上
  标着 **read-only**。

也就是说两边一直不是同一个材质：一个是金属镜面（metallic 1），一个是白色介质（metallic 0）。
底色、反射、对比度都不可能一样。之前 2.2/0.87 的亮度标定也是拿 **Atmosphere 球**测的
（均值 0.295），换成 FilamentPBR 后需要重新对一次，不能直接沿用。

**正确的 A/B**：在 Qt 左侧列表点 **"Filamen..."**（Default 和 Skysphere 之间那一项），
确认标题变成 FilamentPBR，Metallic/Roughness 与 Moon 设成一样，两边都 Reset View，再截图。

## 环境 cube 朝向：最终结论（2026-10-21）

用 `cmgen` 抽一个**已知上红下蓝**的 HDR（`Build/_skytest/UpDown.hdr`）：

- `+Z` face 的 PNG **row 0 = 红（up）**、最后一行 = 蓝（down）。标准 GL `+Z` 的 row 0 是
  **top = -Y（down）**，所以 `cmgen --extract` 导出的 PNG 是**自下而上**（row 0 = face 的
  v=0）。之前拿我们的 dump（glReadPixels，row 0 也是 v=0）先翻转再比，才出现每面 `flipV`；
  **按同样行序直接比 `a` vs cmgen PNG 是 identity**（相关 0.97–0.999）。
- 原始 `Studio.hdr`（`read_hdr` 的行序用自生成的 SkyID 验证过：row 0 = up）与我们的
  `sky_mip0` 用**着色器同一套映射**对比，3000 方向 corr = **0.9999**。

所以 **源 cube / prefilter cube / 反射采样的上下方向都是对的**，没有翻转。`+Y` face 中心
和 equirect 极点数值不同是极点处 implicit-LOD 滤波造成的，不是朝向。

## 剩下的“对比不对”：优先怀疑参考侧环境

Qt‑Filament 的 `MaterialPreview::createEnvironment()` 在找不到 `Studio.hdr` 时会退到
`MakeFallbackEnvironment()`：程序化天空 + **三个** softbox（key/fill/rim，方向
`(0.35,0.55,0.76) / (-0.75,0.15,0.25) / (0.15,0.35,-0.9)`）。参考球看起来正是
**平滑灰底 + 三个干净高光**，而我们的球反射的是真实 Studio.hdr（窗帘、多个灯、结构多），
和“高光数量/对比度”的观感差异吻合。

先看 Qt‑Filament 日志有没有：

- `[Env] found .../Studio.hdr` → 同一张 HDR，继续查相机；
- `[Env] no environment file found` / `使用程序化环境贴图` → 参考用的是 fallback，两张图
  环境不同，这个对比不成立。

## 调试时踩过的坑（重要）

- **不要用 FBO 读回 6-face cubemap 的单个 mip 来直接判断 prefilter 内容**：当其余 5 个
  attachment 还停在上一个（更小的）mip，或 depth attachment 还是上一级尺寸时，驱动会把
  framebuffer / 回读区域裁到最小 attachment。本次一开始看到“level 0~3 全黑、只有 16×16
  有 alpha”就是这个原因，实际绘制是好的。读回前要先 detach depth / 把所有 attachment
  摆到同一尺寸。
- `glFramebufferTextureLayer` 的 level/layer 顺序不要写反（level 在前）。
- 旧代码的 6 face attachment 在“setup 每次重跑”时其实不会裁绘制（当前 mip 是 6 个里最小的），
  但仍然脆；现在改成单 attachment 了。

## 最终结论（2026-10-21 下午，球已恢复浅灰）

一连串问题叠加，单看每一个都会误判：

1. **ShaderPack 没重打包**：只改 `.glsl` include 时 `ShaderPack` 的 custom command 没有
   重新生成 `shader.cpp`，`MoonShader` 照样重编但编的是旧包，所以好几次“修了没变化”。
   已给 `Tools/ShaderPack/CMakeLists.txt` 的 `DEPENDS` 补上 `${SHADER_HEADER_PATHS}`；
   排查时以构建输出里的 `[ShaderPack] packed N shader sources` 为准。
2. **预览的 `_BRDFLut` 实际读到白色空纹理**：DFG = (1,1) → `E ≈ 1`，diffuse 被 `(1-E)`
   乘成 0，同时 specular 变成未衰减的环境反射。改成 Karis 解析近似（不依赖纹理）。
   TODO：查预览为什么没绑上 LUT。
3. **未赋值的贴图采样到黑色空纹理**：`u_AlbedoMap`/`u_AmbientOcclusionMap` 是 None 时
   仍然乘以 0，整个输入变黑。加了 `u_HasAlbedoMap` / `u_HasMetallicMap` /
   `u_HasRoughnessMap` / `u_HasAOMap` 开关，未赋值时用 1.0。
   TODO：材质面板/加载器在真的赋了贴图时要把这些开关置 true，否则带贴图的 FilamentPBR
   会忽略贴图。
4. **diffuse 尺度差一个 π**：同一 studio 下 irradiance cube 实测 ~0.7，Filament 的
   roughness-one mip ~2.2——irradiance 烘焙多乘了 1/π。预览的 prefilter 高 mip 又是黑的，
   所以 diffuse 用 `irradiance × π` 补回 Filament 的尺度。
   TODO：查预览的 prefilter mip 4 为什么是黑的（场景 viewport 的 dump 是正常的）。

验证：`Moon.exe` 13:39，metallic 0 / roughness 0 → 浅灰介质球 + 正常反射。

## 运行状态

- 最新构建：`Build/bin/Release/Moon.exe`（2026-10-21 13:39）。
- 验证用的 dump 在 `Build/_ibl_dump_single2/`（临时文件，未入库）。
