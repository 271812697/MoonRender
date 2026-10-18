# 鐫€鑹插櫒绂荤嚎缂栬瘧锛堝鏍?Filament matc锛?
> 鐘舵€侊細**瑙勮寖鑽夋**锛屽緟 review銆傛湰鏂囧彧瀹氫箟鐩爣褰㈡€佷笌璇硶锛屼笉鏀圭幇鏈変唬鐮併€?> 璁捐鍘熷垯锛?*鐓ф妱 Filament 鐨勬垚鐔熻璁?*锛屼笉鑷繁鍙戞槑銆傛瘡涓€鑺傞兘鏍囧嚭瀵瑰簲鐨?Filament 瀹炵幇锛?> 渚夸簬閫愭潯瀵圭収銆傚敮涓€鐨勫亸绂绘槸"shader 鐢辨垜浠墜鍐欏叆鍙ｅ嚱鏁?+ matc 鍐呯疆 prelude"锛孎ilament 鏄?> 鍚屾牱鐨勬ā鍨嬶紙瀹冧篃鏄叆鍙ｅ嚱鏁?+ 鐢熸垚楠ㄦ灦锛夛紝鍙槸瀹冪殑楠ㄦ灦鏇村帤銆?
鐜版湁杩涘害锛?
| 浠?| 鐘舵€?|
|---|---|
| `Tools/ShaderPack`锛坆lob 鈫?`shader.h` / `shader.cpp`锛屽鏍?`resgen`锛?| 鉁?宸插畬鎴?|
| `ShaderArchive`锛堣繍琛屾椂鎸夊悕瀛楀彇婧愮爜锛屽綊妗ｄ紭鍏?纾佺洏鍏滃簳锛?| 鉁?宸插畬鎴?|
| `#include` 灞曞紑 + `#line` 琛屽彿鏄犲皠锛堝鏍?`Includes.cpp`锛?| 鉁?宸插畬鎴?|
| `MoonMatc`锛堝鏍?`matc`锛夈€佹柊 `.ovfx` 璇硶銆乧hunk 鍖呫€佽繍琛屾椂 chunk reader | 猬?鏈枃鍐呭 |

---

## 1. 娴佹按绾匡紙涓夌骇锛屼竴涓€瀵瑰簲锛?
```
X.ovfx 鈹€鈹€[MoonMatc]鈹€鈹€鈻?X.ovshader 鈹€鈹€[ShaderPack]鈹€鈹€鈻?shader.h + shader.cpp 鈹€鈹€鈻?閾炬帴
        閫愭枃浠?           姣忎釜 .ovfx 涓€涓寘        澶氬寘鍚堝苟锛堝凡鏈夛級          杩?exe
                            鈹?                            鈹斺攢 杩愯鏃讹細璇?chunk 鈫?glShaderSource 鈫?glCompileShader
```

| Filament | 鎴戜滑 | 璇存槑 |
|---|---|---|
| `tools/matc` + `libs/filamat` | `Tools/MoonMatc` | 瑙ｆ瀽 + 鍙樹綋 + 鏍￠獙 + 鍙嶅皠 + 鎵撳寘 |
| `.filamat` | `.ovshader` | chunk 瀹瑰櫒 |
| `tools/resgen` | `Tools/ShaderPack` | 宸插疄鐜帮紝鏀剁獎涓?鍙悎骞? |
| `filament/src/MaterialParser.cpp` + `filaflat/DictionaryReader` | `ShaderManager` 鍐呯殑 chunk reader | 杩愯鏃跺彧璇汇€佷笉瑙ｆ瀽婧愮爜 |

**鏀剁泭**锛歚.ovfxh` 涓嶅啀鍙戝竷锛坧relude 缂栬繘 matc銆乮nclude 鏋勫缓鏈熷睍寮€锛夛紱`shader.cpp` 閲屼笉鍐嶆湁
鏄庢枃婧愮爜锛屽彧鏈夌紪璇戝寘锛涜繍琛屾椂涓嶅啀鍋?`2^n` 鍙樹綋鍏ㄧ粍鍚堛€佷笉鍐嶆嫾瀹忋€佷笉鍐嶆壂鏂囨湰銆?
---

## 2. 鏂?`.ovfx` 璇硶

鐓?`lit.mat`锛坄android/samples/sample-lit-cube/.../lit.mat`锛夌殑鍥涗釜閮ㄥ垎銆傛枃浠跺悕浠嶄负 `.ovfx`锛?閲嶆瀯瀹屾垚鍚庡彲鏁翠綋鏀瑰悕 `.mat`銆?
### 2.1 涓変釜鍧?
```glsl
material {
    name        : Standard,
    shadingModel: lit,          // lit | unlit | subsurface | cloth ...
    blending    : opaque,       // opaque | transparent | add | mask ...
    culling     : back,         // back | front | none
    depthWrite  : true,
    depthCulling: true,
    doubleSided : false,

    requires    : [ tangents ], // 椤剁偣灞炴€ч渶姹?
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
        // 鍙€夈€備笉鍋氬彉鎹紝鍙厑璁告敼灞炴€?    }
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

- 鏀寔"鍙湁 fragment 鍧?锛坲nlit 鏉愯川銆佸悗澶勭悊鍏ㄥ睆 pass 閮戒笉闇€瑕?vertex 鍧楋級銆?- `#include ":Shaders/Common/....ovfxh"` 浠嶅厑璁革紝浣?**prelude 涓嶅啀闇€瑕佹墜鍐?include**
  锛堣 搂3锛夈€?
### 2.2 `MaterialInputs`锛堢収鎶?`shaders/src/surface_material_inputs.fs`锛?
鎸?`shadingModel` 瑁佸壀瀛楁鈥斺€擣ilament 灏辨槸杩欎箞鍋氱殑锛坄#if !defined(SHADING_MODEL_UNLIT)` 绛夛級锛?
| 瀛楁 | 鍑虹幇鏉′欢 | 鎴戜滑鐨?`Standard.ovfx` 浠婂ぉ鎬庝箞鍐欑殑 |
|---|---|---|
| `vec4 baseColor` | 鎬绘槸 | `u_Albedo * u_AlbedoMap` |
| `float roughness` | lit 涓旈潪 specular-glossiness | `u_Roughness * u_RoughnessMap` |
| `float metallic` / `reflectance` | 鍚屼笂 | `u_Metallic * u_MetallicMap` |
| `float ambientOcclusion` | lit | `occlusionTex` |
| `vec4 emissive` | 鎬绘槸 | `u_EmissiveColor * u_EmissiveIntensity` |
| `float clearCoat` / `clearCoatRoughness` | lit | 鐜板湪娌℃湁 |
| `float anisotropy` / `vec3 anisotropyDirection` | lit | 鐜板湪娌℃湁 |
| `float thickness` | subsurface / refraction | 鎴戜滑鐨?`u_Transmission`/`u_RefractionIndex` |
| `vec3 normal` | 闇€娉曠嚎璐村浘鏃?| `u_NormalMap` 鐨勭粨鏋?|

> 鍙妱鎴戜滑鐪熺殑浼氬疄鐜扮殑瀛楁锛沗sheen`/`subsurfaceColor`/`specularColor` 绛夊厛涓嶆斁杩涜〃閲岋紝
> 闇€瑕佹椂鍐嶆寜 Filament 鐨勫畾涔夎ˉ鈥斺€旇〃缁撴瀯宸茬粡鍜屽畠涓€鑷达紝鍔犲瓧娈典笉褰卞搷鍏跺畠閮ㄥ垎銆?
### 2.3 `MaterialVertexInputs` / `MaterialVertexOutputs`

鐓?Filament 鐨?`surface_vertex_inputs.vs` / `..._outputs`锛氫綅缃?娉曠嚎/鍒囩嚎/UV 鐢?prelude 鎻愪緵锛?鍏ュ彛鍑芥暟鍙兘鏀瑰睘鎬э紝涓嶈兘鏀瑰彉鎹€傛垜浠洰鍓嶇殑 `VERTEX_POS_NORMAL` feature 灏辨槸瀹冪殑闆忓舰銆?
### 2.4 鏈疆琛ヤ笂鐨勫洓涓娉曪紙杩佺Щ 44 涓?shader 鏃堕€愪釜閫煎嚭鏉ョ殑锛?
| 璇硶 | 鐢ㄩ€?| 渚嬪瓙 |
|---|---|---|
| `vertex { void main() {} }` / `fragment { void main() {} }` | **鏁存鑷**锛坮aw stage锛夛細鍧楅噷瀹氫箟浜?`main()`锛孧oonMatc 灏卞彧缁?prelude + 鍙傛暟锛屽睘鎬?varying/`FRAGMENT_COLOR`/楠ㄦ灦 main 鍏ㄩ兘涓嶇敓鎴愶紝鍧楄嚜宸卞０鏄庛€佽嚜宸卞啓 `gl_Position` | `GizmoLine` `SkyboxConvert` `Point` `Standard` |
| `geometry { ... }` | GLSL 绗笁闃舵锛?*鎴戜滑鐨勬墿灞?*锛團ilament 娌℃湁锛夈€傛€绘槸鏁存鑷锛屽潡閲屽畾涔?`layout(triangles) in` 绛?| `GizmoLine` `GeomertySurface` `SectionFace` `GizmoCell` `SectionContour` |
| `{ type : float3, name : samples, count : 64 }` | 鏁扮粍 uniform锛圫SAO 鐨?64 閲囨牱鏍革級 | `ssao` |
| `{ type : float, name : u_GridSize, default : 0.01 }` | uniform 鐨?GLSL 鍒濆鍖栧€硷細鑰佹枃浠?`uniform float x = 0.01;` 鐨勪笅鍙戦粯璁ゅ€煎繀椤讳繚鐣欙紝鍚﹀垯娌′汉璁剧殑鍙傛暟浼氶潤榛樺彉 0 | `Grid` `SectionCap` `Billboard` `Standard` |

鍙﹀涓や欢閰嶅鐨勪簨锛?
- **`#include` 鏀规垚鏋勫缓鏈熷睍寮€**銆傝€佽矾寰勭敱杩愯鏃?`ShaderLoader::AssembleSource` 灞曞紑锛堝鏍?  Filament 鐨?`Includes.cpp`锛夛紱鎴愪簡 package 涔嬪悗 GLSL 鐩存帴杩涢┍鍔紝娌℃湁杩愯鏃堕偅涓€瓒熶簡锛屾墍浠?  `MoonMatc` 鑷繁閫掑綊灞曞紑锛坄-I` 鏄?shader 鏍癸紝`:Shaders/...` 鍓嶇紑鏄犲皠鍒板畠锛夛紝姣忎釜鏂囦欢姣忎釜闃舵
  鍙睍寮€涓€娆★紙澶存枃浠舵病鏈?include guard锛宍PBR.ovfxh` 浼氫簩娆″鍒?`Utils.ovfxh`锛夛紝
  `Common/Buffers/EngineUBO.ovfxh` 鍜?`Common/Utils.ovfxh` 杩欎袱浠?prelude 鑷甫鐨勭洿鎺ヤ涪鎺夈€?  灞曞紑澶勬彃 `#line` 鍥炴寚锛屾姤閿欎粛鐒舵寚鍒?`.ovfxh` 鑷繁鐨勮銆?- **姣忎釜闃舵甯?`#define MOON_STAGE_VERTEX|FRAGMENT|GEOMETRY`**銆俙Common/Utils.ovfxh` 閲岀殑
  `ComputeTBN()` 鐢?`dFdxFine`锛屽彧鏈夌墖鍏冮樁娈佃兘鐢紱浠ュ墠瀹冭鎷艰繘鎵€鏈夐樁娈碉紝NVIDIA 椹卞姩瀹瑰繊銆?  glslang 涓嶅蹇嶃€傜幇鍦ㄧ敤 `#if !defined(MOON_STAGE_VERTEX) && !defined(MOON_STAGE_GEOMETRY)`
  鎶婂畠鎸″湪闈炵墖鍏冮樁娈靛锛堣€佽矾寰勪笉瀹氫箟杩欎簺瀹忥紝琛屼负涓嶅彉锛夛紝椤剁偣/鍑犱綍闃舵鎵嶇粓浜庤兘绂荤嚎鏍￠獙銆?
---

## 3. prelude锛氭垜浠殑"鐢熸垚楠ㄦ灦"

Filament 鐨勯鏋跺湪 `filament/src/materials/`锛坄surface_shading_model_standard.fs`銆?`surface_lighting.fs`銆乣surface_shadowing.fs` 鈥︼級锛岀敱 matc 鎷艰繘鍘汇€傛垜浠殑楠ㄦ灦灏辨槸鐜版湁鐨?`Shaders/Common/*.ovfxh` + `Standard.ovfx` 閲岄偅娈靛厜鐓э紝鏁寸悊鎴?matc 鍐呯疆鏂囦欢锛?
| prelude | 鏉ユ簮 | 鍐呭 |
|---|---|---|
| `prelude/buffers.ovfxh` | `Common/Buffers/EngineUBO.ovfxh`銆乣LightsSSBO.ovfxh`銆乣ReflectionUBO.ovfxh` | 甯?鐏?鍙嶅皠 UBO |
| `prelude/common.ovfxh` | `Common/Constants.ovfxh`銆乣Utils.ovfxh`銆乣Physics.ovfxh` | 甯搁噺涓庡伐鍏峰嚱鏁?|
| `prelude/material.ovfxh` | 鏂板啓锛堢収 `surface_material_inputs.fs`锛?| `MaterialInputs` / `prepareMaterial()` / `materialParams` |
| `prelude/surface.ovfxh` | `Standard.ovfx` 鐨勫厜鐓ф + `Lighting/PBR.ovfxh`銆乣IBL.ovfxh`銆乣Shadow.ovfxh` | 鐢?`shadingModel` 閫変腑鐨勭潃鑹叉ā鍨?|
| `prelude/variants.ovfxh` | 鏂板啓锛堢収 `Variant.h`锛?| 鍙樹綋浣嶅搴旂殑 `#define` |
| `prelude/output.ovfxh` | `Standard.ovfx` 鏈熬 | 鑹茶皟鏄犲皠/alpha 绛夋敹灏?|

matc 鎷兼帴椤哄簭锛堢収 Filament锛夛細`#version` 鈫?椤剁偣/鐗囧厓灞炴€?鈫?鍙樹綋浣?`#define` 鈫?`buffers` 鈫?`common` 鈫?`material` 鈫?`surface` 鈫?鐢ㄦ埛鍏ュ彛鍑芥暟锛坄main` 涔嬪墠锛夆啋 鐢熸垚鐨?`main()`銆?
---

## 4. 鍙樹綋妯″瀷锛堢収 `Variant.h` + `MaterialVariants.cpp`锛?
Filament锛? 浣嶆帺鐮?+ 鐧藉悕鍗?+ **鍥哄畾鏋氫妇**锛坄determineSurfaceVariants(filter, isLit, ...)`锛夛紝
**浠庝笉鍋?2^n 鍏ㄧ粍鍚?*銆傛垜浠幇鍦?`Standard.ovfx` 鐨?64 涓彉浣撳氨鏄繖涔堟潵鐨勶紙`AssembleShader` 鐨?骞傞泦寰幆锛夛紝杩欐槸鏈€璇ユ浛鎹㈢殑涓€澶勩€?
鎴戜滑鐨勫彉浣撲綅锛堝垵鐗堬紝澶熺敤鍗冲彲锛屽悗缁寜闇€鍔狅級锛?
| 浣?| 鍚?| 鍚箟 |
|---|---|---|
| `0x01` | `SKN` | 钂欑毊/鍙樺舰锛堟殏鐣欑┖锛?|
| `0x02` | `SRE` | 鎺ユ敹闃村奖 |
| `0x04` | `DIR` | 鏈夋柟鍚戝厜 |
| `0x08` | `DYN` | 鏈夌偣鍏?鑱氬厜 |
| `0x10` | `DEP` | 鍙啓娣卞害锛堥槾褰?鎷惧彇锛?|
| `0x20` | `NRM` | 鏈夋硶绾胯创鍥?|
| `0x40` | `MSK` | alpha mask/dithering |
| `0x80` | `TRS` | 閫忓皠/鎶樺皠 |

`isValidStandardVariant()` / `isValidDepthVariant()` 鐓?Filament 鍐欑櫧鍚嶅崟锛沗-V` 杩囨护瀵圭収
`variantFilter`銆俙Standard` 浠?64 涓帇鍒?~6 涓€?
---

## 5. chunk 鏍煎紡锛堢収 `MaterialChunkType.h`锛?
8 瀛楄妭 ASCII tag + 鏁版嵁锛屽悓 `filabridge/include/filament/MaterialChunkType.h` 鐨勫啓娉曪細

| tag | 鍐呭 |
|---|---|
| `SHDR_GLSL` | 姣?(pass, variant, stage) 鐨勬渶缁?GLSL |
| `SHDR_UIB` | uniform 鍧楀竷灞€锛堝亸绉?澶у皬/绫诲瀷锛?|
| `SHDR_SIB` | sampler 缁戝畾 |
| `SHDR_ATTR` | 椤剁偣灞炴€?|
| `SHDR_BIND` | binding 涓嬫爣 |
| `SHDR_VARI` | 鍙樹綋娓呭崟锛堜綅鍊?鈫?stage 鍒楄〃锛?|
| `SHDR_NAME` / `SHDR_UUID` / `SHDR_VERS` | 鍚嶅瓧 / 鍐呭 hash / 鐗堟湰 |

`SHDR_GLSL` 鍐呴儴鍒嗙粍鎴愪竴鍧?瀛楀吀"锛坆lob 鏁扮粍 + (pass, variant, stage) 鈫?blob 涓嬫爣 鐨勭储寮曪級锛?瀵瑰簲 Filament 鐨?`DictionaryChunk` + `MAT_GLSL`銆?
**瀹為檯瀹炵幇鐨?tag**锛? 瀛楄妭锛屾敞鎰忎笉鏄笂闈㈣〃鏍奸噷甯︿笅鍒掔嚎鐨勬嫾娉曪級锛?
| tag | 鍐呭 |
|---|---|
| `SHDRSTAG` | **姣忎釜闃舵涓€浠?*鐨勫叏鏂囷紝鍙樹綋 `#define` 鐨勪綅缃暀鎴愬崰浣嶇 `//@MOON_VARIANT_DEFINES@` |
| `SHDRGLSL` | 姣?(pass, variant, stage) 鍙瓨**鑷繁閭ｅ嚑琛?`#define`** |
| `SHDRVARI` | 鍙樹綋娓呭崟锛坧ass + feature 闆嗗悎锛?|
| `SHDRPROP` | **鍙嶅皠**锛氭瘡涓弬鏁扮殑 name銆丏SL 绫诲瀷銆丟LSL 绫诲瀷銆佹暟缁勯暱搴︺€佹槸鍚?sampler銆佸０鏄庡鍒濆€?|
| `SHDRATTR` | **鍙嶅皠**锛氶《鐐瑰睘鎬э紙location銆丟LSL 绫诲瀷銆佸悕瀛楋級锛屼粠鐢熸垚鐨勯《鐐归樁娈垫枃鏈噷璇诲洖鏉?|
| `SHDRPARM` | 鑰佺殑 name/type 琛紝淇濈暀鍏煎锛坄SHDRPROP` 鍦ㄥ畠涔嬪悗鍐欏嚭骞惰鐩栧悓鍚嶈褰曪級 |
| `SHDRSTAT` | `material { }` 閲岀殑鐘舵€佸瓧娈碉紙blending/culling/depthWrite 鈥︼級锛岀洰鍓嶅彧璁板綍銆佽繍琛屾椂涓嶈 |
| `SHDRNAME` | 鏉愯川鍚?|

`SHDRPROP` / `SHDRATTR` 瀵瑰簲 Filament 鐨?`MaterialUib` / `MaterialSib` / `MaterialAttributeInfo`锛?浣嗙矑搴︽寜鎴戜滑鐨勫疄闄呮儏鍐垫敹绐勶細Filament 鐨?UIB 瑕佸啓鍧楀唴 **鍋忕Щ涓庡ぇ灏?*锛堝畠鎶婂弬鏁版墦杩涗竴涓?UBO锛夛紝
SIB 瑕佸啓 **binding 涓嬫爣**锛圴ulkan 鐨?descriptor set锛夈€傛垜浠敤瑁?uniform銆佹寜鍚嶅瓧缁戠汗鐞嗭紝娌℃湁甯冨眬銆?娌℃湁 descriptor set锛屾墍浠ヤ竴鍧楀甫 flag 鐨勫垪琛ㄥ氨澶熲€斺€斾絾闈㈡澘瑕佺殑涓滆タ锛堢被鍨嬨€佹槸鍚﹁创鍥俱€佹暟缁勯暱搴︺€?榛樿鍊硷級閮藉湪閲岄潰锛屼笉闇€瑕佽繍琛屾椂鍐嶉棶椹卞姩瑕?`glGetActiveUniform`銆?
灞炴€у垪琛ㄦ槸**鎸夋潗璐?*鑰屼笉鏄寜鍙樹綋锛氭煇涓樁娈靛湪 feature 鍒嗘敮閲屽０鏄庣殑灞炴€э紝浼氭妸鏂囨湰閲屽嚭鐜拌繃鐨勫０鏄庨兘绠楄繘鏉?锛團ilament 鐨?attribute 鍒楄〃鍚屾牱鏉ヨ嚜 `material { requires : [鈥 }`锛屼篃鏄潗璐ㄧ骇鐨勶級銆?
闄ゆ涔嬪杩樻湁涓や欢浜嬮『鎵嬪仛浜嗭細`--dump-json` 鐜板湪杈撳嚭 `parameters`锛堝惈 `glslType`/`sampler`/`count`/
`default`锛夊拰 `attributes`锛涢《鐐瑰睘鎬х殑閲囬泦鏄粠**鐢熸垚鐨勯樁娈垫枃鏈?*閲屾鍒欒鍥炴潵鐨勶紝鎵€浠ユ暣娈佃嚜绠＄殑闃舵
锛坄GizmoLine` 鐨?`aPositionSize`/`aColor`銆佸疄渚嬪寲灞炴€э級鍜岄鏋剁敓鎴愮殑 `geo_Pos/geo_TexCoords/geo_Normal`
閮借兘琚褰曘€?
`SHDRSTAG` + `SHDRGLSL` 鐨勬媶鍒嗘槸**蹇呴』鐨?*锛屼笉鏄紭鍖栵細涓€涓潗璐ㄧ殑姣忎釜鍙樹綋闄や簡閭ｅ嚑琛?`#define`
浠ュ瀹屽叏鐩稿悓锛宍Standard` 鏈?192 涓彉浣擄紝涓嶆媶灏辨槸 192 浠藉悓鏍?64 KB 鐨勬簮鐮?鈥斺€?鎵撳寘鍑烘潵
20.5 MB锛坄shader.cpp` 99.8 MB锛夛紱鎷嗗畬 1.31 MB锛坄shader.cpp` 6.8 MB锛夈€傝繍琛屾椂
`ShaderPackage::Source()` 鎶婁袱鑰呮嫾璧锋潵锛坄Source` 鍥犳杩斿洖 `std::string` 鑰屼笉鏄?`string_view`锛夈€?娌℃湁 `SHDRSTAG` 鐨勬棫鍖呬粛鐒舵寜"blob 灏辨槸鏁存"璇伙紝鎵€浠ユ牸寮忔槸鍚戝悗鍏煎鐨勩€?
---

## 6. 杩愯鏃?
鐓?`filament/src/MaterialParser.cpp`锛歚Shader` 涓嶅啀鎸佹湁婧愮爜锛岃€屾槸鎸佹湁 chunk 瑙嗗浘銆?
```
ShaderManager[key]
   鈹斺攢 璇昏〃椤?{ name, data, size }        鈫?宸叉湁锛圫haderArchive锛?      鈹斺攢 Unflatten + DictionaryReader    鈫?鏂帮細鍒?chunk銆佸～ blob 瀛楀吀
         鈹溾攢 鍙樹綋娓呭崟锛堢敤浜?variantFilter 鏍￠獙锛?         鈹溾攢 鍙嶅皠锛圲IB/SIB/ATTR锛夆啋 鏉愯川鍙傛暟琛?鈫?闈㈡澘
         鈹斺攢 GetVariant(pass, variant) 鈫?glShaderSource + glCompileShader锛堜竴娆★級
```

鍒犻櫎锛歚ParseShader`锛堟壂 `#shader/#feature`锛夈€乣AssembleShader`锛?^n 寰幆 + 瀹忔敞鍏ワ級銆?**杩欎袱涓殏鏃朵笉鑳藉垹**锛歅athTrace 鐨?4 涓?shader 杩樻寜"璇绘枃浠舵枃鏈?+ 鎻?define"鐨勬棫鏂瑰紡缂栬瘧锛?`CreateFromSource`锛堝唴宓屽瓧绗︿覆锛夋洿鏄?5 涓粯鍒剁偣鍦ㄧ敤銆偮? 宸叉妸 PathTrace 瀹氫负姘镐箙渚嬪
锛堝悗缁噸鏋勮矾寰勮拷韪櫒鏃朵竴璧锋敼锛夛紝鎵€浠ヨ繖鏉?鍒犻櫎"瑕佺瓑閭ｄ箣鍚庛€?
---

## 7. 杩佺Щ璁″垝锛堝瀭鐩村垏鐗囦紭鍏堬級

1. **鍒囩墖**锛歚Unlit.ovfx` 璧伴€氬叏閾捐矾锛堟柊璇硶 鈫?MoonMatc 鈫?`.ovshader` 鈫?ShaderPack 鈫?   杩愯鏃惰 chunk 鈫?鐢婚潰姝ｅ父锛夈€傛闃舵鏂版棫涓ゅ骞跺瓨銆?2. 鍚庡鐞嗙被锛圔loom / FXAA / ToneMap / HZB / Blit 鈥︼級锛歚shadingModel : unlit` + 鍏ㄥ睆 pass锛?   浣撻噺灏忋€侀闄╀綆鈥斺€旂収 `filament/src/materials/` 閲屽悓鍚嶇殑 `.mat` 鎶勭粨鏋勩€?3. 宸ュ叿绫伙紙Grid / Line / Picking / Section / Skybox锛夛細澶у涔熸槸 unlit锛屽悓涓娿€?4. `Standard.ovfx` 鏈€鍚庡仛锛圥BR/IBL/闃村奖/鍙樹綋鏈€澶氾級銆?5. 鍏ㄩ儴杩佸畬鍚庡啀鍒犳棫瑙ｆ瀽璺緞锛屽苟鎶?`.ovfx` 鏀瑰悕涓?`.mat`銆?
瀹屾垚鎯呭喌锛?-4 宸插仛瀹岋紙`Engine/Shaders` 涓?44 / 48 涓?`material{}` 褰㈠紡锛屽墿 4 涓?PathTrace 瀹氫负
姘镐箙渚嬪锛岃 搂8锛夛紱绗?5 姝?*涓嶅仛**鈥斺€旀棫鐨勬枃浠剁増瑙ｆ瀽璺緞杩樿鐣欑粰 PathTrace锛宍.ovfx` 涔熷洜姝や笉鏀瑰悕
`.mat`銆?
> 涔嬫墍浠ュ仠鍦?涓ゆ潯璺苟瀛?鑰屼笉鏄交搴曞垏鎹細鍓╀笅閭?4 涓矾寰勮拷韪?shader 鐨勫姞杞芥柟寮忔槸
> "杩愯鏃跺線婧愮爜閲屾彃 define 鍐嶇紪"锛屽拰鏋勫缓鏈熷嚭鍖呯殑妯″瀷澶╃劧鍐茬獊锛涘喅瀹氱瓑**璺緞杩借釜鍣ㄦ暣浣撻噸鏋?*鏃?> 杩炲姞杞芥柟寮忎竴璧锋崲锛岀幇鍦ㄨ縼绉诲睘浜庣櫧鏀癸紙璇﹁ 搂8 "鍓╀綑"锛夈€?
---

## 8. 浜ゆ帴锛氬綋鍓嶈繘搴︺€佹崲绠楅厤鏂广€佸凡韪╃殑鍧?
### 杩涘害

| 缁?| 鐘舵€?|
|---|---|
| 鏋勫缓鏈?`Tools/MoonMatc`锛? matc锛?| 鉁?`material{}`/`vertex{}`/`geometry{}`/`fragment{}` 瑙ｆ瀽銆乣domain: surface\|postProcess`銆佸彉浣撳睍寮€銆乣uniform` 鐢熸垚锛圖SL 鍚?鈫?GLSL銆乣count` 鏁扮粍銆乣default` 鍒濆€硷級銆乣#include` 鏋勫缓鏈熷睍寮€銆佹暣娈佃嚜绠￠樁娈点€侀樁娈靛畯銆乣#line` 鍥炴寚銆乣.ovshader` chunk銆佸弽灏?JSON銆佹湭杩佺Щ鏂囦欢閫忎紶 |
| 鎵撳寘 `Tools/ShaderPack`锛? resgen锛?| 鉁?`name=path` 鍚堝苟鎴?`shader.h` / `shader.cpp`锛宍MoonShader` 鐢?`MoonRender` 閾炬帴 |
| 杩愯鏃?`ShaderArchive` / `ShaderPackage` / `ShaderLoader` | 鉁?褰掓。浼樺厛銆乣MOONSDR1` 鍒嗘祦銆佹寜鍙樹綋娓呭崟缂栬瘧锛堥《鐐?鐗囧厓/鍑犱綍涓夋锛屽嚑浣曞彲閫夛級 |
| **宸茶縼绉伙紙44 / 48锛?* | 鉁?`Unlit` `Standard` `GeomertySurface` `GeomertySurfacePick` `GeomertyLine` `GeomertyPoint` `GizmoCell` `Gizmo` `GizmoLine` `GizmoPoint` `GizmoTriangle` `Grid` `Line` `Point` `Billboard` `Atmosphere` `Skysphere` `SkyboxConvert` `SkyboxIrrandiance` `SkyboxPrefilter` `BrdfLut` `ClearBackground` `PickingFallback` `OutlineFallback` `ShadowFallback` `MirrorPlaneShadow` `SectionCap` `SectionContour` `SectionFace` `SectionParity` `gbuffer` `ssao` `ssaoblur`锛沗PostProcess/` 鍏ㄩ儴 12 涓?|

`Resource/Moon/Data/Engine/Shaders` 涓嬪叡 48 涓?`.ovfx`锛? 10 涓?`.ovfxh`锛屽叡 58 涓簮鎵撹繘褰掓。锛夈€?
### 鍓╀綑锛堥兘宸插畾妗堬紝涓嶆槸寰呭姙锛?
**1. D 缁?4 涓紙PathTrace锛夛細瀹氫负姘镐箙渚嬪锛屼笉杩併€?*

`PathTrace/Output` `PathTrace/PathTrace` `PathTrace/PathTraceLowRes` `PathTrace/ToneMap` 缁х画璧?`CreateFromSource` + `#shader` 璇硶銆傝繖涓嶆槸"娌℃潵寰楀強"锛屾槸**鏄庣‘鍐冲畾涓嶈縼**锛?026-10锛屽悗缁矾寰勮拷韪櫒
瑕佹暣浣撻噸鏋勶紝灞婃椂杩炲姞杞芥柟寮忎竴璧锋敼锛岀幇鍦ㄥ姩灞炰簬鐧芥敼锛夈€傜悊鐢卞拰鏁板瓧鐣欏湪杩欓噷锛屽厤寰楀悗鏉ヤ汉閲嶈蛋涓€閬嶏細

- `PathTraceRenderPass::UpdateShaders()`锛坄PathTraceRenderPass.cpp:590-742`锛夊厛
  `loadShaderSource()` 鎶婃枃浠惰鎴愬瓧绗︿覆锛屽啀鎸夋覆鏌撻€夐」**寰€婧愮爜閲屾彃鍏?`#define`**锛岀劧鍚庢寜 define 涓?  缂撳瓨 `CreateFromSource` 鐨勭粨鏋溿€傛枃浠朵竴鏃︽敼鎴?`material{}`锛岃繖娈?鎵?`#shader fragment` + 鎻?  define"灏卞け鏁堚€斺€旇縼瀹冪瓑浜庢敼杩欐潯娓叉煋閫氳矾鐨勫姞杞芥柟寮忥紱
- 12 涓?`OPT_*` 甯冨皵锛坄OPT_ENVMAP` `OPT_LIGHTS` `OPT_RR` `OPT_MEDIUM` `OPT_VOL_MIS`
  `OPT_ALPHA_TEST` `OPT_BACKGROUND` `OPT_TRANSPARENT_BACKGROUND` `OPT_HIDE_EMITTERS`
  `OPT_UNIFORM_LIGHT` `OPT_OPENGL_NORMALMAP` `OPT_ROUGHNESS_MOLLIFICATION`锛夊綋 features 鐢紝
  骞傞泦鏄?2^12 = 4096 涓彉浣?脳 姣忎唤 53 KB 脳 2 闃舵锛涜€屼笖 `PropertyWidget` 鍏佽杩愯鏃朵换鎰?  `AddFeature`锛屾墍浠?鍙紪鐪熶細鐢ㄧ殑缁勫悎"涔熶笉灏侀棴锛?- 瑙勬ā锛歚PathTrace` / `PathTraceLowRes` 鍚?1926 / 1905 琛屻€佸悇 28 澶?`OPT_*` 瀹堝崼锛? 涓?`#else`锛夛紝
  `ToneMap` 2 澶勶紝`Output` 0 澶勩€傝鎶婂畠浠縼鎴?1 涓彉浣?+ uniform 甯冨皵"锛屽厛寰楀仛瀹岃繖浠朵簨鍐嶈窇
  benchmark鈥斺€旂暀缁欒矾寰勮拷韪櫒閲嶆瀯锛?- 闄勫甫濂藉锛歚OPT_RR_DEPTH` 閭ｄ釜甯﹀弬鏁扮殑 define 鍏跺疄娌¤ shader 鐢ㄥ埌锛坄PathTrace.ovfx` 閲屽彧鏈?  `OPT_RR` 鍜屼竴涓?`RR_DEPTH` uniform锛夛紝鎵€浠ュ皢鏉ュ彧闇€瑕佸鐞?12 涓函甯冨皵銆?
鍥犱负杩欐潯鍐冲畾锛?*`ShaderLoader` 鐨勬枃浠剁増鏃цВ鏋愯矾寰勶紙`CompileShaderFromFile` 鈫?`ParseShader` 鈫?`AssembleShader`锛変繚鐣?*锛屄? 鐨勭 5 姝ワ紙鍒犳棫璺緞锛夌浉搴旀悂缃€?
**閮ㄧ讲褰㈡€佷篃璺熺潃鍙樹簡**锛氬紩鎿庣潃鑹插櫒鐜板湪鍙湪 exe 閲岋紙`Moon/CMakeLists.txt` 鐨勮祫婧愭嫹璐濇楠ゆ妸
`Data/Engine/Shaders/` 鎺掗櫎鎺変簡锛夛紝杩愯鐩綍閲屽彧鍓?`Data/Engine/Shaders/PathTrace/*.ovfx`
锛? 涓紝涓婇潰鐨勬案涔呬緥澶栵級杩樿浠庣鐩樿銆傛敼閭ｆ鎷疯礉瑙勫垯鏃跺埆鍐嶄竴鍒€鍒囥€?
**2. `Resource/Moon/Data/Editor/Shaders/` 涓嬮偅 5 涓€佸壇鏈細宸插垹銆?*

`Grid` `Gizmo` `Billboard` `PickingFallback` `OutlineFallback` 鍘熸潵鐢?`EditorResources` 鏋勯€犳椂
鍔犺浇锛坄m_shaders`锛夛紝浣?`EditorResources::GetShader()` **鍏ㄥ伐绋?0 涓皟鐢ㄧ偣**鈥斺€斿畠浠彧琚姞杞姐€?浠庢潵娌¤鐢ㄦ潵鐢昏繃浠讳綍涓滆タ锛坄GridRenderPass` 鐢ㄧ殑鏄?`ShaderManager[":Shaders/Grid.ovfx"]`锛屼篃灏辨槸
`Engine/Shaders` 閭ｄ唤锛沗DebugSceneRenderer` 鍙槸 include 浜嗗ご鏂囦欢锛夈€傚凡杩炲悓 `m_shaders` 鎴愬憳銆?`GetShader()`銆乣CreateShader()` helper銆佷袱涓棤鐢ㄧ殑 `#include`銆佺洰褰曟湰韬拰 CMake 閲岀殑鎷疯礉/IDE 鎸傝浇
涓€璧峰垹鎺夈€傞『甯︾殑濂藉锛氬惎鍔ㄥ皯缂?5 涓?shader锛岃繍琛岀洰褰曚笉鐢ㄥ啀甯﹀畠浠€?
### 浠ｄ环锛堟湰杞紩鍏ワ紝蹇呴』浜や唬锛?
| 椤?| 杩佺Щ鍓?| 鎷嗗垎鍓?| 鐜板湪 |
|---|---|---|---|
| 褰掓。瀛楄妭锛堟墦杩?exe锛?| 683 KB | 20.5 MB | **1.31 MB** |
| `Build/generated/shaders/shader.cpp` | 0.7 MB | 99.8 MB | **6.8 MB** |
| `Moon.exe` | 鈥?| 30.1 MB | **11.9 MB** |
| 鍚姩鍒伴甯э紙寮€ 1.moon锛?| ~10 s | ~25 s | **~26 s** |

浣撶Н閭ｄ竴鍒楀凡缁忚В鍐筹紙`SHDRSTAG` 鍏变韩闃舵鏂囨湰锛岃 搂5锛夛細鍓╀綑 1.31 MB 瀵规瘮鍘熸潵鐨?683 KB锛?鍩烘湰灏辨槸鍙樹綋鏁伴噺鍜?`Standard`/`GeomertySurface`/`GizmoCell` 鐨勭湡瀹炲閲忋€?
**鍓╀笅鐨勬槸鍚姩閭?~15 s锛屽畠娌¤瑙ｅ喅**锛氬彉浣撴暟娌″彉锛岄┍鍔ㄥ氨瑕佺紪鍚屾牱澶氱殑绋嬪簭
锛坄Standard` 192 鍙樹綋 脳 2~3 闃舵 + `GizmoCell` 64 脳 3 + `GeomertySurface` 64 脳 3 鈥︼級銆?鑰佽矾寰勫叾瀹炰篃缂栬繖涔堝锛屾墍浠ヤ弗鏍艰杩欎笉鏄縼绉诲紩鍏ョ殑锛屼絾鐜板湪鐨勬暟瀛椾笉濂界湅銆傜湡姝ｇ殑瑙ｆ硶鏄?**鎸夐渶缂栬瘧鍙樹綋**锛堥娆?`GetVariant` 鏃舵墠缂栵紝鑰屼笉鏄?`Shader` 鏋勯€犳椂鍏ㄧ紪锛夆€斺€旈偅鏄繍琛屾椂琛屼负鏀瑰姩
锛堥甯т細鍗′竴涓嬶級锛岄渶瑕佸崟鐙喅绛栵紝鏈疆娌″姩銆?
### 鎹㈢畻閰嶆柟锛堝凡鐢?44 涓枃浠堕獙璇侊級

```diff
- #feature X             鈫? material { features : [ X ], ... }
- #pass P                鈫? material { passes : [ P ], ... }
- #shader vertex 鏁存     鈫? 鍒犳帀锛坧ostProcess 鐢熸垚鍏ㄥ睆 quad锛泂urface 鐢熸垚 MVP + materialVertex锛?- #shader fragment
- #shader geomerty 鏁存   鈫? geometry { 鍘熸牱 }锛堟暣娈佃嚜绠★紝MoonMatc 鍙粰 prelude + 鍙傛暟锛?- uniform <type> <name>;  鈫? material { parameters : [ { type : <DSL 鍚?, name : <鍚屽悕> } ] }
- uniform <t> <n> = V;    鈫? { type : <DSL 鍚?, name : <n>, default : V }   鈫?鍒濆€煎繀椤荤暀
- <t> <name>[N];          鈫? { type : <DSL 鍚?, name : <name>, count : N }
- void main() { ... }     鈫? fragment { void material(inout MaterialInputs m) { ... } }锛堟垨鏁存鑷锛?- FRAGMENT_COLOR = X;     鈫? m.baseColor = X;锛堟暣娈佃嚜绠℃椂淇濇寔鍘熸牱锛?```

- 鍙傛暟绫诲瀷鍐?**DSL 鍚?*锛歚float/float2/float3/float4/bool/int/mat4/sampler2D/samplerCube`
  锛堝伐鍏疯浆 GLSL锛夛紱琛ㄩ噷娌℃湁鐨勫悕瀛楋紙`samplerBuffer`/`usamplerBuffer`锛夊師鏍烽€忎紶缁?GLSL锛?- `TexCoords`锛坧ostProcess锛夋垨 `fs_in.TexCoords` / `fs_in.FragPos` / `fs_in.Normal`锛坰urface锛?  鐢?skeleton 鎻愪緵锛宍FRAGMENT_COLOR` 涔熶笉鐢ㄥ０鏄庯紱**鏁存鑷鏃惰繖涓夋牱閮藉緱鑷繁澹版槑**锛?- helper 鍑芥暟銆佸父閲忔暟缁勩€乣#define`銆乣#if defined(FEATURE)` 閮藉彲浠ョ暀鍦ㄥ潡鍐咃紱
- 鐪熺殑鑳界敤 skeleton 灏卞埆鐢ㄦ暣娈佃嚜绠★細`materialVertex` 鑳芥敼灞炴€э紝鏀逛笉浜?`gl_Position`銆?
### 楠岃瘉

```powershell
cmake --build Build --target Moon --config Release -- /nologo /m:1
```

**鏋勫缓鏈熷氨浼氭牎楠?GLSL**锛堢収 filament 鐨?matc锛氱紪璇戜笉杩囧氨鏋勫缓澶辫触锛屼笉璁╁畠娲诲埌杩愯鏃讹級銆?`Tools/ShaderPack/CMakeLists.txt` 鐢?`find_program(GLSLANG_VALIDATOR 鈥?` 鎵惧埌
`glslangValidator` 鍚庯紝姣忎釜 `MoonMatc` 瑙勫垯鍙樻垚涓夋锛?
1. 娓呮帀璇?shader 鐨?dump 鐩綍锛堥伩鍏嶆嬁涓婁竴杞殑娈嬬暀闃舵鍘绘牎楠岋級锛?2. `MoonMatc --dump-glsl <dir>` 鈥斺€?姣忎釜 (pass, 鍙樹綋, 闃舵) 涓€涓?`.glsl`锛?3. `ValidateGlslStages.cmake` 瀵规瘡涓枃浠惰窇 `glslangValidator -S vert|frag|geom`锛屾湁澶辫触灏?   `FATAL_ERROR`锛屽苟鎶?glslang 鐨勮緭鍑哄師鏍锋墦鍑烘潵銆?
鎵句笉鍒?`glslangValidator` 鏃跺彧鎵撲竴琛?`STATUS`锛堚€渨ill NOT be validated at build time鈥濓級锛?鍏跺畠琛屼负涓嶅彉鈥斺€旀墍浠ヨ繖鏄竴鏉″彲閫変緷璧栵紝涓嶅紩鍏?glslang 搴擄紙filament 鏄嚜宸遍摼鎺?glslang + SPIRV-Tools锛夈€?
鍓嶇疆鐨勪竴浠朵簨锛歚MoonMatc` 鐜板湪鍦ㄦ瘡涓樁娈靛紑澶达紙`#version` 涔嬪悗锛夎緭鍑?`#extension GL_GOOGLE_cpp_style_line_directive : enable`锛堢収 filament 鐨?`CodeGenerator.cpp:125-127`锛夈€傛病鏈夊畠 glslang 鐩存帴鎷掔粷甯︽枃浠跺悕鐨?`#line`锛宒ump 鍑烘潵鐨勪笢瑗垮氨鍠備笉杩?validator锛涢┍鍔ㄤ晶鏈潵灏辨帴鍙椼€?
瀹炴祴锛堟晠鎰忓湪 `ssaoblur.ovfx:27` 鍐欏潖涓€琛岋級锛?
```
CUSTOMBUILD : error : ssaoblur.ovfx:27: '' :  syntax error, unexpected IDENTIFIER ...
ssaoblur.ovfx: 1 stage(s) do not compile (glslangValidator). The dump is under ...
```

琛屽彿鎸囧洖 `.ovfx` 鑷繁锛屾瀯寤?exit 1銆?*浠ｄ环**锛氬彧鏈夋敼鍔ㄧ殑 shader 浼氶噸鏂版牎楠岋紝浣嗗崟涓ぇ shader 涓嶄究瀹?鈥斺€擿Standard`锛?92 鍙樹綋銆?80 涓樁娈碉級閲嶆柊鏍￠獙 + 閲嶆柊鎵撳寘 + 閾炬帴绾?**50 s**锛涘叏閲忔竻鐞嗗悗閲嶅缓浼氭牎楠?鍏ㄩ儴 1002 涓樁娈点€傚珜鎱㈢殑璇濇妸 `GLSLANG_VALIDATOR` 缂撳瓨鍙橀噺缃┖鍗冲彲鍏虫帀銆?
鍐?*甯︿竴涓伐绋嬪惎鍔?*锛岃瑙嗗彛鐪熺殑鐢诲抚锛堜笉寮€宸ョ▼鍋滃湪棣栭〉鏃讹紝涓€鍗?shader 鏍规湰涓嶄細琚姞杞斤級锛?
```powershell
Start-Process Build\bin\Release\Moon.exe -ArgumentList '"D:\Project\UseQt\SketchExample\1.moon"' `
    -WorkingDirectory Build\bin\Release -RedirectStandardOutput Build\run.txt
```
姝ｅ父鏃ュ織鍙湁 `[Shader] sources are read from the embedded archive (58 files packed)`锛?涓嶅簲鏈?`[Shader Package]`锛堝彉浣撶己婧愶級/ `[Shader Compile]`锛堢紪璇戝け璐ワ級/ `[Shader Linking]`銆?`Moon.exe <宸ョ▼璺緞>` 鏄繖涓€杞姞鐨勫皬鍔熻兘锛坄Moon/main.cpp`锛?00 ms 鍚?`onReadFile`锛夛紝灏辨槸涓轰簡璁?杩欎欢浜嬭兘鑴氭湰鍖栤€斺€斾互鍓嶅彧鑳芥墜鐐归椤点€?
鎯冲崟鐙獙璇佹煇涓?shader锛堟瀯寤烘湡娌″紑鏍￠獙銆佹垨鎯虫墜宸ョ湅 dump锛夛細

```powershell
Build\bin\Release\MoonMatc.exe -i <X.ovfx> -o Build\verify\X.ovshader -I Resource\Moon\Data\Engine\Shaders `
    --dump-glsl Build\verify --dump-json Build\verify\X.json
```
dump 鍑烘潵鐨?`.glsl` **鐩存帴**灏辫兘鍠?validator锛堥樁娈靛紑澶村凡缁忔湁閭ｈ
`#extension GL_GOOGLE_cpp_style_line_directive : enable`锛夛紝涓嶇敤鍐嶆墜宸ユ彃锛?`glslangValidator <stage>.glsl -S vert|frag|geom`銆?涓変釜闃舵閮藉繀椤昏繃鈥斺€擿ComputeTBN` 鐨?`dFdxFine` 宸茬粡琚?`MOON_STAGE_*` 鎸℃帀锛岄《鐐?鍑犱綍闃舵涓嶅啀鏄?"娉ㄥ畾澶辫触"锛岃繖閲屾姤閿欏氨鏄?shader 鐪熸湁闂銆?
**闃舵鍚嶈浠庢枃浠跺悕鍚庣紑鍒ゆ柇**锛坄_vertex` / `_fragment` / `_geometry`锛夛紝涓嶈兘鐢?`-match 'vertex'` 杩欑澶у皬鍐欎笉鏁忔劅鐨勫寘鍚垽鏂細`_VERTEX_POS_NORMAL_fragment.glsl` 浼氬懡涓?`vertex`锛屼簬鏄妸鐗囧厓褰撻《鐐圭紪锛屾姤涓€鍫嗗亣澶辫触锛堟湰杞俯杩囷級銆?
骞插噣鐘舵€佷笅璺戜竴閬嶅叏閲忥紙44 涓潗璐ㄣ€?002 涓樁娈碉級锛?```
materials: 44 ; stages validated: 1002 ; failures: 0
```

### 宸茶俯鐨勫潙

1. chunk tag **蹇呴』 8 瀛楄妭**锛? 瀛楃浼氳鎴柇銆佽繍琛屾椂姘歌繙鍖归厤涓嶄笂锛夛紱
2. `#line` **涓嶈兘鎻掑湪 `#version` 涔嬪墠**锛圙LSL 瑕佹眰瀹冩槸绗竴鏉¤鍙ワ級锛?3. 鍙傛暟蹇呴』鐢熸垚 `uniform`锛岀被鍨嬭鍋?`float4 鈫?vec4` 鏄犲皠锛?4. blob 閿槸 `pass/feat1|feat2/stage`锛堝垎闅旂鍦?feature **涔嬮棿**锛屾棤灏惧垎闅旂锛夛紱
5. 鑰佺殑 geometry-stage 璺宠繃 hack 渚濊禆"`#version` 鍚庣涓€琛屾槸 `#if defined`"锛屾彃 `#line` 浼氱牬鍧忓畠锛堝凡鍦ㄦ壂鎻忓墠璺宠繃 `#line` / `//`锛夈€?6. 鍏ュ彛鐐瑰弬鏁板彨 `m`锛坄void material(inout MaterialInputs m)`锛夛紝shader 閲屽師鏉ョ殑灞€閮ㄥ彉閲?`m`
   **蹇呴』鏀瑰悕**锛歚BloomDownsampling` 鐨勯噰鏍锋湰鏉ュ氨鍙?`m`锛屾敼鍚嶆垚 `mm`锛堝眬閮ㄥ彉閲忎細閬斀鍚屽悕鐨勫舰鍙傦紝
   灏卞湴鏀瑰畬涔熻兘缂栵紝浣嗚璧锋潵鏄伨闅撅級锛?7. `uniform float u_X = 0.5;` 杩欑**澹版槑澶勯粯璁ゅ€?*蹇呴』缈昏瘧鎴?`default :`锛屼笉鑳界洿鎺ュ垹锛?   `Grid` 鐨?`u_GridSize`/`u_GridOpacity`/`u_FadeDistance`銆乣SectionCap` 鐨?   `u_Albedo`/`u_environmentIntensity`銆乣Billboard` 鐨?4 涓€乣Standard`/`GeomertySurface`/
   `SectionFace` 鐨勪竴澶хエ 鈥斺€?鍏ㄩ兘娌℃湁浠讳綍 C++ 鎴?`.ovmat` 璁捐繃锛屽垹鎺夊氨闈欓粯鍙?0锛堢綉鏍?spacing
   鍙?0 鈫?`fwidth(coord/0)`锛岀洿鎺ュ潖锛夈€傚彧鏈夌‘璁ゆ瘡甯?`SetProperty` 鐨勶紙`ssaoblur` 鐨?   `u_BlurDepthTolerance`锛夋墠鍙互鍒狅紱
8. **geometry 鍧楅粯璁?姣忎釜鍙樹綋閮芥湁"**锛歁oonMatc 缁欏０鏄庝簡 geometry 鐨勬潗璐ㄥ湪**鎵€鏈?*鍙樹綋閲屽啓鍑犱綍
   闃舵锛岃€岃€佽矾寰勬湁涓?hack 鈥斺€?stage 浣撲互 `#if defined(WITH_EDGE)` 寮€澶存椂锛宖eature 娌″紑鐨勫彉浣?   鏁存璺宠繃銆傜幇鍦ㄧ殑瑙勫垯锛坄WantsGeometryStage`锛夛細**鍙湁褰撴暣涓?geometry 鍧楃殑绗竴涓潪绌鸿鏄?   鍗?feature 鐨勫畧鍗€佷笖閰嶅鐨?`#endif` 灏辨槸鍧楀熬**鏃讹紝鎵嶆寜鍙樹綋瑁佹帀锛涘叾浣欐儏鍐典竴寰嬬収鍐欙紙鎷夸笉鍑嗗氨
   淇濆畧锛夈€俙Standard` 姝ｅソ鏄繖绉?鏁村潡琚寘浣?鐨勫舰鐘讹紝192 鍙樹綋閲?96 涓笉甯﹀嚑浣曢樁娈碉紱
   `GizmoCell`/`GeomertySurface`/`SectionFace` 鐨勫畧鍗湪 varying 澹版槑涔嬪悗锛岃涓嶄簡锛屼粛鐒舵瘡涓彉浣?   涓€浠斤紙瀹冧滑浣撳唴閭ｄ釜 `#else` 鐩撮€氬垎鏀氨鏄负姝や繚鐣欑殑锛夈€傜湡瑕佸交搴曠渷锛屽緱璁?DSL 鏄惧紡澹版槑
   "鏈潗璐ㄥ嚑浣曢樁娈甸渶瑕佸摢涓?feature"锛岄偅鏄竴涓柊瀛楁锛岃繕娌″姞锛?9. `mix(vec3, vec3, bool)` **涓嶆槸 GLSL 4.5 鐨勯噸杞?*锛堥€夋嫨瀛愬繀椤绘槸 `bvec3`锛夛細鑰佷唬鐮佽兘缂栫函绮规槸
   NVIDIA 椹卞姩鏀惧浜嗐€俙Standard`/`GeomertySurface`/`SectionFace` 鐨?   `mix(pbr, ..., u_BuiltInToneMapping)` 鍥犳鍦ㄨ縼绉绘椂鍐欐垚 `bvec3(u_BuiltInToneMapping)`
   鈥斺€旈€愬垎閲忛€夋嫨锛岀粨鏋滀笌椹卞姩鍘熸潵鐨勮涓轰竴鑷达紝浣嗚繖鏄湰杞敮涓€鍔ㄤ簡琛ㄨ揪寮忕殑鍦版柟锛?10. 鏁存鑷鐨勯樁娈?*涓嶅悆 skeleton 鐨勪换浣曚笢瑗?*锛氬睘鎬с€乿arying銆乣FRAGMENT_COLOR`銆乣main` 鍏ㄩ兘
   寰楄嚜宸卞０鏄庛€傛贩鐢ㄤ細琚摼鎺ュ櫒鎶撲綇锛歳aw 椤剁偣 + 鐢熸垚鐗囨 鈫?鐢熸垚鐨勯偅浠?    `in VS_OUT { FragPos, TexCoords, Normal } fs_in` 浼氬涓婁竴涓粠娌″啓杩囪繖浜?varying 鐨勯《鐐归樁娈碉紱
11. 鍙傛暟閲岀殑 `default` 鍊煎彲鑳借嚜甯﹂€楀彿锛坄vec4(1.0, 2.0, 3.0, 4.0)`锛夛紝鍙傛暟琛ㄦ壂鎻忓櫒蹇呴』鎸夋嫭鍙?    娣卞害鎵惧垎闅旈€楀彿锛屽惁鍒?`default` 浼氳鎴垚 `vec4(1.0`锛?12. `#include` 鐜板湪鏄?*鏋勫缓鏈?*灞曞紑鐨勶紝姣忎釜鏂囦欢姣忎釜闃舵鍙睍寮€涓€娆★紝涓?    `EngineUBO.ovfxh`/`Utils.ovfxh` 鍥犱负 prelude 鑷甫浼氳涓㈡帀鈥斺€旀墍浠ヨ€佹枃浠堕噷閭ｄ袱琛?include
    鐣欑潃鏄?*闈欓粯鏃犲**鐨勶紙涓嶄細閲嶅澹版槑锛夛紝浣嗗缓璁垹鎺夈€?

---

## 9. 宸插畾鐨勪笁涓棶棰橈紙2026-10 鍐崇瓥锛?
| 闂 | 鍐冲畾 |
|---|---|
| `materialParams` 鍛藉悕 | **鍏堢畝鍗曟潵**锛氬弬鏁板悕淇濇寔鐜版湁 `u_Albedo` / `u_Roughness` 杩欑被锛屼笉寮曞叆 `materialParams.baseColor` 缁撴瀯浣撹闂紝`SetProperty` 璋冪敤鐐逛笉鐢ㄦ敼锛屽厛鎶婃祦绋嬭窇閫氾紱閲嶆瀯鍚庢湡鍐嶈縼鍒?Filament 鐨勫舰寮?|
| `.ovshader` 鍐呭 | **淇濈暀鏈€缁?GLSL 鏂囨湰**锛堜綋绉崲鍙鎬с€佸彲璋冭瘯銆乣--dump-glsl` 鍙鍑猴級锛涗笉鍑?SPIR-V锛圙L 4.5 娑堣垂涓嶄簡锛?|
| 鍙嶅皠杈撳嚭 | **鍚屾椂杈撳嚭 JSON**锛坄--dump-json`锛岀瓑浠?`matc -r`锛夛紝鏉愯川缂栬緫鍣ㄧ洿鎺ュ悆 JSON锛屼笉蹇呰В鏋愪簩杩涘埗 chunk |

## 10. 涓?Filament 鐨勫凡鐭ュ樊寮傦紙鏄庣‘璁板綍锛岄伩鍏嶈浠ヤ负鎶勬紡锛?
- **鍙樹綋鏋氫妇**锛欶ilament 鏄浐瀹氭灇涓撅紙`determineSurfaceVariants`锛夛紝鎴戜滑绗竴鐗堜粛鏄?feature 鐨勫箓闆嗭紱
  `Standard` 鐜板湪鏄?3 pass 脳 2^6 feature = **192 涓彉浣?*锛堜綋绉棶棰樺凡鐢?搂5 鐨?`SHDRSTAG` 瑙ｅ喅锛?  浣嗗彉浣撴暟鏈韩娌℃湁鏀舵暃锛夈€傚浐瀹氭灇涓?+ `variantFilter` 鏄笅涓€浠惰鍋氱殑浜嬨€?- **璺緞杩借釜 4 涓?shader 涓嶈縼**锛埪? "鍓╀綑"锛夛細Filament 娌℃湁"杩愯鏃跺線婧愮爜鎻?define 鍐嶇紪璇?鐨勬満鍒讹紝
  瀹冮潬鍥哄畾鍙樹綋鏋氫妇 + 鏉愯川瀹炰緥锛涙垜浠殑 PathTrace 鎭板ソ闈犲墠鑰咃紝鎵€浠ユ案涔呯暀鍦ㄦ棫璺緞涓婏紝绛夎矾寰勮拷韪櫒
  閲嶆瀯鏃惰繛鍔犺浇鏂瑰紡涓€璧锋敼銆傝繖涔熸槸鏁翠釜杩佺Щ鍞竴鏈敹鍙ｇ殑鍦版柟銆?- **娣卞害/闃村奖 pass**锛欶ilament 鐢?`DEP` 鍙樹綋浣嶏紝娣卞害鍙樹綋鍙紪椤剁偣闃舵銆佺墖鍏冮樁娈垫暣涓烦杩囷紱鎴戜滑浠嶄繚鐣?  `passes : [ SHADOW_PASS ]` 鏄剧ず澹版槑锛岀墖鍏冮樁娈电収缂栵紙`Unlit` 浠婂ぉ灏辨槸闈?`#if defined(SHADOW_PASS)` 鍙樼┖锛夈€?  绛夊彉浣撴ā鍨嬭惤鍦板悗缁熶竴銆?- **鏁存鑷鐨勯樁娈?*锛埪?.4锛夛細Filament 鎶婂彉鎹㈤攣姝诲湪鐢熸垚鐨?`main()` 閲岋紝鏉愯川鍙兘鏀瑰睘鎬э紱鎴戜滑鍏佽
  `vertex { void main() }` / `fragment { void main() }` 鍏ㄦ潈鎺ョ銆傝繖鏉℃槸涓烘晳鐜版湁 8 涓?鑷繁鍐?  `gl_Position`/鑷甫 varying"鐨?shader 鑰屽姞鐨勶紝浠ｄ环鏄潗鏂欎笉鍐嶅彈楠ㄦ灦绾︽潫銆侳ilament 鐨勭瓑浠风墿鏄?  CPU 渚ч鍙樻崲锛坆illboard锛夋垨鑷畾涔?vertex domain锛屽睘浜庝互鍚庡彲浠ラ€愪釜鏀跺洖鐨勫€恒€?- **geometry 闃舵**锛欶ilament 鐨勬潗璐ㄦā鍨嬫病鏈夌涓夐樁娈碉紙瀹冮潬 CPU/椤剁偣鐫€鑹插櫒鍋氱嚎瀹藉睍寮€锛夈€傛垜浠繚鐣欎簡
  GL 鐨勫嚑浣曢樁娈碉紙`geometry { }`锛夛紝鍥犱负 `GizmoLine`/`GeomertySurface`/`SectionFace`/`GizmoCell`/
  `SectionContour` 鐨勮竟缂樺睍寮€鍏ㄥ湪閲岄潰銆傝繖鏄案涔呮€у樊寮傦紝涓嶆槸寰呭姙銆傞檮甯︿竴鏉¤嚜鍒涜鍒欙細geometry 鍧楄嫢
  鏁村潡琚崟涓?feature 瀹堝崼鍖呬綇锛屽氨鍙湪璇?feature 寮€鐨勫彉浣撻噷鐢熸垚锛堣€?loader 鐨?skip hack 鐨勭瓑浠风墿锛?  瑙?搂8 鍧?8锛夆€斺€擣ilament 娌℃湁鍑犱綍闃舵锛岃嚜鐒朵篃娌℃湁杩欐潯銆?- **鍙傛暟榛樿鍊?*锛埪?.4 鐨?`default`锛夛細Filament 鐢?`variables` + 鏉愯川瀹炰緥锛屾垜浠洿鎺ユ妸 GLSL 鍒濆鍖栧€?  鍐欏洖 uniform 澹版槑锛岄潬 GL 鎶婂垵鍊煎弽灏勮繘灞炴€ц〃锛坄Material::UpdateProperties`锛夈€?- **鏉愯川 DSL**锛欶ilament 鐨?`material { }` 鏈夊嚑鍗佷釜瀛楁锛坄requires` / `variables` / `subpass` / `stereoscopicType` 鈥︼級锛?  鎴戜滑鍙疄鐜扮敤寰楀埌鐨勯偅浜涳紝瀛楁鍚嶄繚鎸佷竴鑷达紝鍚庣画鎸夐渶琛ャ€?

---

## 11. 鏉愯川缂栬緫鍣紙绠＄嚎楠岃瘉锛?
`Moon/editor/UI/MaterialPanel/MaterialEditorPanel`锛歞ock 闈㈡澘锛岄€変竴涓?`Engine/Shaders` 涓嬬殑
`.ovfx`銆佹敼婧愮爜銆丄pply 鐑噸杞姐€傚畠瀛樺湪鐨勬剰涔夋槸鎶?鏋勫缓鏈熷嚭鍖?鈫?杩愯鏃跺彧璇诲寘"杩欐潯閾惧湪缂栬緫鍣ㄩ噷
璺戦€氫竴閬嶏紝椤哄甫璇佹槑鍙嶅皠/閿欒鍥炴寚閮界湡鐨勮兘鐢ㄣ€?
閾捐矾锛?
```
婧愮爜 鈹€鈹€Moon::Matc::CompileSource锛堣繘绋嬪唴锛孧oonMatcLib锛夆攢鈹€鈻?鍖呭瓧鑺?     鈹€鈹€ShaderLoader::CreateFromPackage锛堣鍙ｄ笂涓嬫枃閲岀紪 GLSL锛夆攢鈹€鈻?Shader*
     鈹€鈹€Material::SetShader + 鍦烘櫙閲岀敤鏃?Shader 鐨勬潗璐ㄤ竴璧锋崲 鈹€鈹€鈻?瑙嗗彛/棰勮绔嬪埢鏇存柊
```

涓夋潯缁忛獙锛堥兘韪╄繃锛夛細

1. **缂栬瘧 GLSL 蹇呴』鏈?current 鐨?GL 涓婁笅鏂?*銆傞潰鏉挎槸 Qt 鍥炶皟锛屼笂涓嬫枃褰掕鍙ｏ紝鎵€浠?寤?Shader"
   鍜?娓查瑙?閮借蛋 `ViewerWidget` 鐨勪袱涓叆鍙ｏ紙`CreateShaderFromPackage` / `RenderMaterialPreview`锛夛紝
   闈㈡澘鍙嬁 `Shader*` 鍜?`QImage`銆傜洿鎺ュ湪鎸夐挳鍥炶皟閲?`glCompileShader` 浼氭妸浜嬩欢寰幆鎸傛
   锛堣繘绋嬭繕鍦ㄣ€佺敾闈㈠仠浣忥級锛屽緢闅炬煡锛?2. **鏋勯€犳湡涓嶈 `setFloating(true)`**锛屼篃涓嶈鍦ㄦ涔嬪墠纰?GL鈥斺€攄ock 闈㈡澘鏄湪缂栬緫鍣ㄩ潰鏉胯繕娌″缓瀹屾椂
   鏋勯€犵殑锛岃繖涓ょ鎿嶄綔閮戒細璁╁惎鍔ㄦ寕浣忋€傞瑙堣绛夎鍙ｄ笂涓嬫枃灏辩华锛氶潰鏉跨敤 300 ms 杞
   `ViewerWidget::HasGLContext()`锛屽氨缁悗鍐嶅仛绗竴娆￠瑙堬紱
3. **NVIDIA 椹卞姩涓嶈 `GL_GOOGLE_cpp_style_line_directive`**锛屽彧缁欎竴鏉?warning 骞跺拷鐣?`#line` 閲岀殑
   鏂囦欢鍚嶃€備絾瀹炴祴鎶ラ敊浠嶇劧鏄?`Unlit.ovfx(39) : error C0000: ...`锛堢紪璇戝櫒鐢ㄧ殑鏄?`#line` 鐨勮鍙烽儴鍒嗭級銆?   鏋勫缓鏈?glslang 鍒欐槸瀹屾暣鏀寔锛坄ssaoblur.ovfx:27` 閭ｇ锛夈€傛墍浠ヨ鍙峰洖鎸囧湪涓や釜绔兘鎴愮珛锛屽彧鏄?   椹卞姩渚т細澶氫竴琛屾棤瀹冲憡璀︺€?
棰勮鐢ㄧ殑鐞冧綋鍜岀浉鏈哄湪 `Core::Rendering::MaterialPreviewRenderer`锛堣嚜甯?FBO銆佽嚜甯?`EngineUBO`銆?绋嬪簭鍖?UV 鐞冿級锛屽拰鍦烘櫙鍏辩敤 `Core/Rendering/EngineUBO.h` 鐨勫竷灞€锛坰td140 鍋忕Щ鐢?static_assert 閽変綇锛夈€?娌℃湁鐏厜锛歚Standard` 杩欑被 lit 鏉愯川棰勮浼氭槸榛戠殑锛寀nlit 鐨勬甯搞€?
