# CAD 材质库调研与落地

目标：给 CAD 体一个"选外观"的入口。商业化 CAD 里这套东西普遍叫 **appearance（外观）**，和
**physical material（物理材质：密度、屈服强度…）** 分开；我们只做外观这一半。

## 1. 商业 CAD 的appearance 体系（调研摘要）

| 产品 | 概念 | 分类骨架 |
|---|---|---|
| SolidWorks | RealView 外观（appearance），按文件夹组织 | Metal / Plastic / Glass / Rubber / Paint / Fabric / Stone / Wood / Ceramic / Water / Emissive / Misc |
| Inventor / Fusion 360 | Appearance（视觉）与 Physical Material（工程）分离 | Metal / Plastic / Glass / Paint / Wood / Stone / Fabric / Rubber / Ceramic / Concrete / Water / Emissive |
| KeyShot | Material 库，按大类 + 预设 | Metal / Plastic / Glass / Wood / Stone / Fabric / Leather / Gemstone / Liquid / Paint / Rubber / Emissive |
| Creo / NX | Appearance Definition / 材料库（视觉 + 物理） | 同类骨架，条目更偏工程牌号 |

参考：[Inventor「关于外观」](https://help.autodesk.com/view/INVNTOR/2026/CHT/?guid=GUID-53D2D220-7053-4851-AAA4-2A2AEE489E1E)、
[Fusion 360「材质和外观」](https://help.autodesk.com/view/fusion360/PTB/?guid=RND-MATS-APPEARANCES)、
[SolidWorks 外观文件夹](https://help.solidworks.com/2010/german/SolidWorks/pworks/LegacyHelp/PhotoWorks/Appearances/Appearance_Folders.htm)、
[KeyShot 手册（材料库章节）](https://media.keyshot.com/manual/KeyShot7_manual_en.pdf)。

三个共同点，直接决定我们的设计：

1. **分类是文件夹**，不是平铺列表；用户按"金属 → 钢 → 拉丝"找。
2. **同一个材质有多个"状态"变体**：抛光/拉丝/喷砂，而不是一个"钢"。
3. **外观 = 颜色 + 反射行为 + 少量贴图**；绝大多数 CAD 场景只用前两项（贴图是渲染向的加分项）。

## 2. 我们这套 CAD 面的能力边界（先钉死）

CAD 面走 `GeomertySurface.shader`，能表达：

`u_Albedo`（sRGB）、`u_Metallic`、`u_Roughness`、`u_Reflectance`、`u_ClearCoat(+Roughness)`、
`u_SpecularFactor`、`u_SpecularColorFactor`、`u_RefractionIndex`、`u_Transmission`、
`u_Absorption`、`u_Thickness`、`u_Emissive(Color/Intensity)`、`u_AmbientOcclusionMap`。

**不能表达**（B-rep 网格的硬约束，不是参数没开）：

- 法线贴图 / 视差：`VertexBVH` 没有切线坐标系，STEP 导入的 UV 全是 0。
- 木纹、拉丝、皮革这类**纹理驱动**的外观：同上，需要三平面（triplanar）映射才能做。
- 各向异性（拉丝高光的方向性）：Filament 标准模型里没接。
- 每面独立材质：一个 B-rep 体当前只有一份面材质 + 每 solid 的域颜色。

还有一个必须记住的行为：**域颜色会乘进基色**（`linearAlbedo *= domainColor`）。所以给彩色实体
套"黄铜"这类材质，结果是被实体颜色染过。用材质库时的约定：

- 想看到材质本色 → 把实体颜色设为白色（`u_Albedo` 白色 / 属性面板 Base Color 拉白）；
- 想保留实体配色 → 用中性材质（灰/白），不要再叠彩色外观。

## 3. 已落地的第一批（30 个，7 类）

目录：`Resource/Moon/Data/Engine/Materials/CAD/<分类>/<名称>.mat`，
资源键 `:Materials/CAD/<分类>/<名称>.mat`。

| 分类 | 条目 | 取值要点（sRGB 基色 / metallic / roughness） |
|---|---|---|
| Metal | SteelPolished, SteelBrushed, StainlessSteel, AluminumMatte, AluminumAnodized, CastIron, Brass, Copper, Chrome, Titanium | 基色 = 该金属的反射色；metallic 1；roughness 0.05–0.62 |
| Plastic | ABSGray, ABSBlack, PCWhite, NylonBlack, POMNatural, AcrylicClear | metallic 0；roughness 0.06–0.55；AcrylicClear 带 transmission 0.92 |
| Paint | PaintWhiteGloss, PaintAnthracite, PaintRedGloss, PowderCoatBlack, PaintMetallic | 电介质 + clear coat；metallic 款用 0.55 金属度 + clearCoat 1 |
| Rubber | RubberBlack, RubberGray | roughness 0.75–0.82，reflectance 0.35–0.4 |
| Mineral | CeramicWhite, MarbleWhite, Concrete | 陶瓷带 clearCoat；大理石/混凝土是"无纹理近似" |
| Wood | WoodOak | 纯色近似（真木纹需要 triplanar，见 §5） |
| Glass | GlassClear, GlassTinted | transmission 0.85–0.9 + ior 1.52；**当前是不透明近似**（见 §5） |
| Emit | EmissiveWarm | 基色白 + emissive `#FFE2B0` × 3 |

所有资产都只写与 shader 默认值**不同**的 uniform，其余沿用 `GeomertySurface` 的参数默认值，
这样以后调默认值能一次性影响整库。

## 4. 一个要跟你确认的取舍：金属在视口里"发黑"

物理正确的金属（metallic=1）没有漫反射，只反射环境。我们视口的环境光强度是 1.0（预览才是 2.2），
所以纯金属在视口里偏暗（缩略图里能直接看到：`SteelBrushed` 的圆柱比灰色塑料暗一截）。
商业 CAD 查看器通常两种做法：

- **物理派**：就是 metallic=1，靠环境亮度和反射撑起来（要求环境够亮）。
- **视图友好派**：金属也用"灰色电介质 + 高 reflectance"假装，保证任何角度都看得清轮廓。

建议两套都给：`Metal/`（物理）和 `Metal/Viewer*`（视图友好），让用户按用途选。这条等你定。

## 5. 与现有代码的两处配套改动（已改，未提交）

1. `ContentBrowserPanel.cpp` / `MaterialThumbnailRenderer.cpp`：材质资源键从"文件名单段"
   改成**相对 Materials 根目录的路径**（和 shader 分支一致）。原来的写法把子目录里的材质
   解析到根目录，等于打不开也存不进去。
2. 新增资产后必须重跑 `cmake -S . -B Build`：资源复制用的是 `file(GLOB_RECURSE ...)`，
   配置期才展开，否则 `Build/bin/Release/Moon/Data/Engine/Materials/CAD/` 下不会有新文件。

## 6. 后续（按价值排序）

1. **triplanar 贴图**：给 `GeomertySurface` 加一个"世界/物体三平面"采样模式，木纹、拉丝、
   混凝土才有意义；顺带解决 UV=0 的 STEP 面。
2. **每面材质**：B-rep 面按 domain/face 索引取材质（Filament 的 per-primitive material），
   商业 CAD 的"选面赋材质"就靠这个。
3. **透明材质的正规路径**：玻璃现在只能走 `Transparents` pass（深度剥离），需要把
   `CADFaceTransparent` 这条链路和材质库接起来（材质资产里选"透明"就自动换 pass）。
4. **法线/各向异性**：需要切线坐标系或屏幕空间导数 TBN，属于渲染向，优先级低于上面三条。
5. **材质库的 UI**：内容浏览器已经是文件夹树，够用；材质编辑器可以把 `group :` 当页签，
   现在参数按 group 分组显示，已经能读。

## 7. 验证情况

- Release 构建通过（资源重新配置 + 复制）。
- 取 `Metal/SteelBrushed.mat`、`Paint/PaintRedGloss.mat` 放到材质根目录跑了一次缩略图，
  两张都能正常渲染（说明资产 XML、shader 绑定、CAD 预览网格都对），验证后已删除这两个探针文件。
- 子目录材质的**列目录/打开**路径只在代码层改过，需要你在 UI 里点一下
  `Materials/CAD/Metal` 确认列表、缩略图、双击进材质编辑器都正常。
