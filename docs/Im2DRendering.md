# Im2D：自研 2D 绘制层（替代 ImGui 绘制）

> 记录时间：2026-10-22。相关代码在 `MoonRender/include/Rendering/Overlay/` 与
> `MoonRender/src/Rendering/Overlay/`（`Im2D.*`、`Im2DFont.*`、`Im2DRender.*`）。
> 定位：编辑器所有的屏幕空间绘制从 ImGui 的 `ImDrawList` 迁到这套自研实现；
> **ImGui 之后只用于调试**（帧率、临时面板、探针）。

## 1. 为什么不用 ImGui 的画

之前编辑器的 2D 覆盖层（草图标注、尺寸 widget、view cube）都是往
`ImGui::GetForegroundDrawList()` 里加几何，再用 ImGui 的 renderer 画出来。问题是：

- 每处调用都要拖进 ImGui 的窗口/上下文概念（font、draw data、io、全局状态）；
- 几何、字体、纹理都绑在 ImGui 的数据结构上，想做"文字/图标落在草图平面里"这类改造时
  没有着力点（ImGui 的 `AddText` 只能轴对齐，`AddImage` 只能轴对齐矩形）；
- ImGui 的 renderer 只在它自己的绘制阶段可用，覆盖层的提交时机被 ImGui 绑死。

Im2D 把这层拆出来：一个独立的顶点/索引/命令缓冲 + 自己的字体图集 + 自己的 GL 提交。

## 2. 三层结构

```
Im2D::DrawList        屏幕空间几何，逐图元写入（颜色 0xAABBGGRR，与 ImGui 一致）
    ├─ 顶点/索引缓冲：std::vector<DrawVert> / std::vector<uint32_t>
    └─ 命令缓冲：DrawCmd { textureId, indexOffset, elementCount, vertexOffset, clipRect }
Im2D::Font             freetype 字体图集：按需烘焙、1024² RGBA、UTF-8、多 face 回退
Im2DRender             GL 提交：自己的 program/VAO/VBO/EBO + 1x1 白纹理 + 字体纹理
```

### 2.1 DrawList

- 颜色编码沿用 ImGui：`0xAABBGGRR`，内存里四个字节是 R、G、B、A，正好是 GL 顶点格式；
  用 `Im2D::ColorU32(r,g,b,a)` 构造（float 或 0-255 两组重载）。
- 顶点 `DrawVert { Vec2 pos; Vec2 uv; uint32_t col; }`，可直接 `glBufferData`。
- 图元：`AddLine`、`AddRect`、`AddRectFilled`、`AddCircle`、`AddCircleFilled`、
  `AddTriangle(Filled)`、`AddQuad(Filled)`、`AddPolyline`、`AddConvexPolyFilled`、
  `AddImage`、`AddImageQuad`、`AddText`，以及路径 API（`PathLineTo/PathArcTo/PathRect/
  PathStroke/PathFillConvex/PathClear`）。
- 抗锯齿：填充用"核心 + 1px 羽化边"，粗线用"核心四边形 + 两侧羽化 + 顶点处小圆盘做 join/cap"。
- 状态：`PushClipRect/PopClipRect`（写进命令的 `clipRect`，提交时 `glScissor`）、
  `PushTexture/PopTexture`（写进命令的 `textureId`）。两者都会先结束当前命令。
- **索引用绝对顶点索引**：命令只记录 `indexOffset`，提交时不要再加 base vertex。

### 2.2 Font（freetype，按字号烘焙）

- `Load({主字体, 回退...}, 参考字号)`：主字体是 `Moon/Data/Engine/Fonts/Ruda-Bold.ttf`
  （和 ImGui 的一致），回退到 `C:/Windows/Fonts/msyh.ttc` 让中文有字形。
- **一个字号一张图集**：`FindGlyph(codepoint, size)` 把 size 量化到整像素，第一次用到时创建
  该字号的 1024² RGBA 图集并烘焙。这样文字是按实际绘制大小光栅化的，不是把一张大图缩放 ——
  和 ImGui 1.92 的做法一致，也是"清晰锐利"的关键（缩放大图集在非烘焙尺寸上一定发虚）。
- **轻 hinting**（`FT_LOAD_TARGET_LIGHT`）：完整 hinting 会把 12–20px 的字形竖笔宽度改坏，
  完全不 hinting 又会让竖笔抖动；light 保留字形、只对齐竖笔。
- 图集：货架打包、1px 间隔、白色 + coverage 放 alpha。`advanceX` 保留 26.6 的小数部分，
  所以字距是均匀的、不会每个字被取整到整像素。
- 渲染端 `SyncFontAtlases()` 在每帧开头为每张图集建/更新一张 GL 纹理（脏了整张
  `glTexSubImage2D`，1 MB，比逐字形 sub-upload 简单且足够快）。
- 字形度量：`advanceX`、`offsetX/offsetY`（相对基线的左上角，y 已翻成屏幕方向）、
  `width/height`、`u0/v0/u1/v1`、`atlasIndex`（该字形在哪张图集里，绘制时据此绑纹理）。
  `v` 直接按图集行号算：`glTexImage2D` 的第一行就是 `v=0`。

### 2.3 Im2DRender（GL 提交）

- 每帧：`NewFrame()` 重置 draw list（并上传脏图集）；调用方通过 `GetDrawList()` / `GetFont()`
  追加几何；`EndFrame(driver, w, h)` 里补一条命令、上传缓冲、按命令 `glScissor` + 绑纹理 + 绘制。
- 着色器：`Position/Uv/Color` 顶点，`FragColor * texture(Texture, FragUv)`；未贴图的图元
  （`textureId == 0`）绑 1x1 白纹理，所以着色器只有一条路径。
- 状态：混合开、深度测试关、剔除关、裁剪开；进入/退出时保存并恢复 GL 状态。
- 上下文：`NewFrame`/`EndFrame` 必须在**渲染模块自己的上下文**里调用（现在由
  `ViewerWidget::paintGL` 在模块上下文中、帧内回调里调用）。

## 3. 与 ImGui 的 API 对照（迁移用）

| ImGui | Im2D |
| --- | --- |
| `ImVec2` | `Im2D::Vec2` |
| `ImU32` / `IM_COL32(r,g,b,a)` | `Im2D::ImU32` / `Im2D::ColorU32(r,g,b,a)` |
| `ImDrawList* dl = ImGui::GetForegroundDrawList()` | `Im2D::DrawList& dl = Im2DRender::Instance().GetDrawList()` |
| `dl->AddLine/AddRect/AddRectFilled/AddCircle/AddCircleFilled/AddPolyline/AddTriangleFilled` | 同名成员，参数顺序一致 |
| `dl->AddText(ImGui::GetFont(), size, pos, col, text)` | `dl.AddText(font, size, pos, col, text)`（`font = Im2DRender::Instance().GetFont()`） |
| `ImGui::CalcTextSize(text)` | `Im2D::DrawList::CalcTextSize(font, size, text)` |
| `ImPlotCustom::AddArrow(Transform(x,y,deg), col, len, th)` | `Im2D::AddArrow(list, Im2D::Transform(x,y,deg), col, len, th)` |
| `ImPlotCustom::drawDoubleArrow(...)` | `Im2D::AddDoubleArrow(list, ...)` |
| `ImPlotCustom::drawDoubleArcArrow(...)` | `Im2D::AddDoubleArcArrow(list, ...)` |
| `ImPlotCustom::AddTextTransform` | `Im2D::AddTextRotated(list, font, size, Transform, col, text)` |
| `ImGui::GetFontSize()` | 常量（草图标注用 `kAnnotationFontSize = 20`，与 `Im2DFont` 烘焙大小同一量级） |

颜色编码相同，所以 `IM_COL32` → `ColorU32` 是纯替换；`ImVec2` 的 `.x/.y` 也一样。

## 4. 已实现 / 未实现

和 ImGui 的 `ImDrawList` 对齐的图元（全部自带抗锯齿）：

| 类别 | 图元 |
| --- | --- |
| 线 | `AddLine`、`AddLineH`、`AddLineV`、`AddPolyline`（含 `Closed`）、`AddBezierCubic`、`AddBezierQuadratic` |
| 填充 | `AddRectFilled`、`AddRectFilledMultiColor`、`AddTriangleFilled`、`AddQuadFilled`、`AddCircleFilled`、`AddNgonFilled`、`AddEllipseFilled`、`AddConvexPolyFilled`、`AddConcavePolyFilled`（耳切三角化） |
| 描边 | `AddRect`、`AddTriangle`、`AddQuad`、`AddCircle`、`AddNgon`、`AddEllipse` |
| 纹理 | `AddImage`、`AddImageQuad`、`AddImageRounded` |
| 文本 | `AddText`（UTF-8、多 face 回退、**按字号烘焙**、`wrapWidth` 自动换行）、`CalcTextSize`、旋转文本 `AddTextRotated` |
| 路径 | `PathLineTo`、`PathArcTo`、`PathArcToFast`、`PathEllipticalArcTo`、`PathBezier*CurveTo`、`PathRect`、`PathStroke`、`PathFillConvex` |
| 状态 | `PushClipRect`/`PopClipRect`、`PushTexture`/`PopTexture`、`AddDrawCommand`、`PrimReserve`/`PrimUnreserve` |
| 形状助手 | `AddArrow`、`AddDoubleArrow`、`AddDoubleArcArrow`（含数值文字重载） |

已知与 ImGui 的差异（写在这里，避免以后误以为漏了）：

- **粗折线接头**用"顶点处小圆盘"代替 ImGui 的精确 miter/bevel/butt-cap 构造：观感接近，代码简单；
- **没有 `AddCallback`**：没有对应的回调机制，也不需要；
- **没有 `ChannelsSplit` / `ChannelsMerge`**：分层绘制靠调用顺序，不需要多索引缓冲；
- **没有 `cpu_fine_clip_rect`**：文本裁剪只走 `PushClipRect` 的矩形裁剪；
- **没有 ImGui 的"白色像素纹理 AA"路径**（`AntiAliasedLinesUseTex`）：AA 全部用几何羽化，
  字体图集里不用预留白像素；
- **索引是绝对索引**：命令只带 `indexOffset`，提交时不要再加 base vertex（`Im2DRender.cpp` 里有注释）；
- 图集脏了整张重传（1 MB），不做逐字形 sub-upload。

## 5. 迁移进度

编辑器里的 2D 绘制已经全部走 Im2D；ImGui 只剩输入（鼠标光标形状）和调试覆盖层。

| 位置 | 内容 | 状态 |
| --- | --- | --- |
| `Moon/Sketcher/SketcherObjWidgetDraw.cpp` | 草图尺寸标注、约束图标、相切图标 | 已迁移 |
| `Moon/Interactive/Screen/ScreenWidget.*`、`HitShape.*` | 屏幕控件基类与命中形状的绘制 | 已迁移 |
| `Moon/Interactive/Widgets/ViewCubeWidget.*`、`SplitScreen.*` | 导航立方体按钮、分屏控件 | 已迁移 |
| `Moon/Interactive/Widgets/ClipPlane.cpp`、`Measurement.cpp` | 旋转读数、测量文字 | 已迁移 |
| `Moon/Interactive/Widgets/DrawSketchHandler.cpp` | 坐标/数值预览文字 | 已迁移 |
| `Moon/Interactive/Widgets/DrawSketchHandlerLine.cpp` | 预览双箭头、角度弧 | 已迁移 |
| `Moon/Interactive/Widgets/SmartDimensionWidget.cpp` | 智能尺寸预览（箭头/弧/旋转文字） | 已迁移 |
| `Moon/Interactive/Im3DRenderer.cpp` 的 `drawLineSplit` | gizmo 手柄圆点与连线 | 已迁移 |
| `Moon/Interactive/Widgets/RotateCenter.cpp` | `if (drawRect && false)` 死分支 | 已删除 |
| `Moon/editor/View/sceneview/viewerwidget.cpp` 的 FPS 文字 | 调试覆盖层 | **保留 ImGui** |

`Moon/Interactive/Im2DType.{h,cpp}`（早先那版半成品克隆）已删除，由本目录的实现取代。

## 6. 验证方式

- 编译：`cmake --build Build --target Moon --config Release -- /nologo /m:1`。
- 视觉：临时在 `ViewerWidget::paintGL` 的 `NewFrame()` 之后加一段探针绘制（圆角矩形、线、圆、
  三角、两行中英文），用 `MOON_SHOT` 存图检查 AA、混合、字形；验证完删掉探针。
- 迁移后要检查的地方：文字基线与 `CalcTextSize` 的高度是否与原 ImGui 布局一致（标签框大小、
  悬停命中区域），以及 `PushClipRect` 的裁剪是否和原来的 ImGui 窗口裁剪等价。

## 7. 参考

- `MoonRender/include/Rendering/Overlay/Im2D.h`：类型与 DrawList API
- `MoonRender/src/Rendering/Overlay/Im2D.cpp`：图元实现（AA 算法移植自 ImGui `imgui_draw.cpp`）
- `MoonRender/include/Rendering/Overlay/Im2DFont.h`、`src/Im2DFont.cpp`：freetype 图集
- `MoonRender/include/Rendering/Overlay/Im2DRender.h`、`src/Im2DRender.cpp`：GL 提交
- `docs/RenderingPlan.md`：CAD 渲染的整体目标与待办
