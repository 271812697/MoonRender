# Filament 对齐：预览球 A/B 工具

P0 的目标：把 Moon 和 Qt-Filament 的材质预览放在**同一状态**下截图，再把球
归一化，输出一组可比较、可追踪的数字。每次改 IBL/光照/材质参数都跑一遍，
而不是靠肉眼判断“看起来差不多”。

## 截图约定

两边必须完全一致，否则数字没有意义：

- 同一 HDR（`Studio.hdr`）；参考侧日志里应出现 `[Env] found .../Studio.hdr`。
- IBL Rotation = 0。
- 两边都点 **Reset View**。
- 同一材质参数：Base `#b8b8b8`，Metallic / Roughness 取同一组值。
- 预览窗口尽量一样大（脚本会归一化，但同尺寸的对比最干净）。
- 截图只保留预览区域更好；脚本支持用 `--*-ball cx,cy,r` 指定球心/半径，
  否则会对亮球自动检测。

建议的 6 组参数（metallic × roughness）：

| # | metallic | roughness |
|---|---|---|
| 1 | 0 | 0 |
| 2 | 0 | 0.4 |
| 3 | 0 | 1 |
| 4 | 1 | 0 |
| 5 | 1 | 0.4 |
| 6 | 1 | 1 |

## 用法

```powershell
python Tools/Alignment/compare_balls.py `
  --ref  ref.png --test moon.png `
  --ref-ball 482,380,220 --test-ball 287,407,207 `
  --out  Build/_ab/dielectric_m0_r0.png `
  --csv  Build/_ab/metrics.csv --label dielectric_m0_r0
```

- 两边球心/半径已知时直接给 `--*-ball`；只给一个也可以，另一个自动检测。
- `--no-refine` 关闭轮廓圆拟合（默认会先拟合一次，去掉手挑 ROI 的几像素偏差）。
- 输出：终端表格 + 并排/差值图 + 追加一行的 CSV，方便看趋势。

## 指标怎么读

| 指标 | 含义 | 用途 |
|---|---|---|
| `mean` / `mean_diff_pct` | 球面平均亮度（显示 sRGB） | 总亮度标定（IBL intensity / sun / exposure） |
| `median` / `p90` / `p99` | 中位与高分位 | 对比度、高光强度 |
| `std` | 亮度标准差 | 对比度（受高光影响大） |
| `hp_ncc` | **高通**（去掉低频明暗）后的 NCC | **反射结构是否一致**，最能用的一项 |
| `orient_corr` | 梯度方向直方图相关 | 反射/高光方向，镜像或旋转会掉 |
| `ncc` | 原始亮度 NCC | 对相机/低频明暗敏感，仅作参考 |
| `angular_corr` / `l1` | 36 方位平均亮度 | 方位分布；目前仍受低频明暗影响 |
| `radial_l1` | 8 个半径环的平均亮度差 | 球面明暗衰减 |
| `peaks` / `peak_match_median` | 高光局部极大值数量/匹配距离 | 高光位置；多峰时需谨慎 |
| `hl_centroid_dist` | 最亮 0.5% 的加权质心距离 | 单一大高光时好用 |

当前经验阈值（先按这个跑，数据多了再收紧）：

- `|mean_diff_pct|` ≤ 3%
- `hp_ncc` ≥ 0.90
- `orient_corr` ≥ 0.95
- `radial_l1` ≤ 0.02
- `peak_match_median` ≤ 0.08（调好峰值匹配后）

## 标定循环

1. 固定 6 组参数，各截一对图，跑脚本，记录 CSV 第一版。
2. 只看 `mean_diff_pct` 调 `PreviewView` 的 `SetEnvironmentIntensity` / sun 强度；
   调完重跑 6 组，避免只对一组过拟合。
3. 亮度对了之后看 `hp_ncc` / `orient_corr`：掉说明反射结构/方向不对（环境
   朝向、prefilter、DFG），不要用 intensity 去掩盖。
4. 每次改代码都把新行追加到同一 CSV，用 `label` 区分版本。

## 已知限制 / 下一步

- **金属球自动检测不可靠**（球体本身很暗），P0 阶段先手给 `--*-ball`；后续可加
  Hough/轮廓拟合。
- 峰值匹配目前是“每个参考峰找最近测试峰”，多峰时会误配；需要做一对一匹配。
- `angular_corr` 应该改成在高通图上算，否则量到的是相机 rig 的低频明暗。
- 截图目前手工；下一步加 `capture_window.ps1`（按窗口标题抓客户区）减少人为差异。
