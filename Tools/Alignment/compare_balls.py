"""Compare two material-preview screenshots of a sphere.

The P0 alignment check: both previews are put in the same state (same HDR, Reset
View, same metallic/roughness/base colour), screenshotted, and reduced to a few
numbers that can be tracked from run to run:

  * luminance mean / median / std / p90 / p99 over the ball (display sRGB)
  * specular highlight centroid and area (top 0.5% luminance)
  * 36-bin angular profile (mean luminance per direction around the ball)
  * 8-bin radial profile
  * normalised cross-correlation of the two grey disks
  * gradient-orientation histogram correlation (catches mirrored/rotated
    reflections even when the average brightness matches)

Usage:
  python compare_balls.py --ref ref.png --test moon.png \
      --ref-ball cx,cy,r --test-ball cx,cy,r --out runs/dielectric_00.png

Without --*-ball the ball is auto-detected as the largest bright region; that
works for dielectric spheres (metallic 0) but not for dark mirror balls, where
the ROI has to be given explicitly (the preview layout is stable, so it is
measured once).

The ball is resampled into a 256x256 disk, so the two screenshots may have
different sizes, DPI or window scaling.
"""
import argparse
import csv
import math
import os
import sys
from collections import deque

import numpy as np
from PIL import Image, ImageFilter

SIZE = 256


def luma(img):
    return img[..., 0] * 0.2126 + img[..., 1] * 0.7152 + img[..., 2] * 0.0722


def auto_ball(img):
    """Largest bright blob -> (cx, cy, r). Dielectrics only."""
    g = luma(img)
    mask = g > max(0.35, float(np.percentile(g, 90)))
    h, w = mask.shape
    small = 4
    m = mask[::small, ::small]
    seen = np.zeros_like(m, bool)
    best = None
    for y in range(m.shape[0]):
        for x in range(m.shape[1]):
            if not m[y, x] or seen[y, x]:
                continue
            q = deque([(y, x)])
            seen[y, x] = True
            n = 0
            minx = maxx = x
            miny = maxy = y
            while q:
                cy, cx = q.popleft()
                n += 1
                minx = min(minx, cx); maxx = max(maxx, cx)
                miny = min(miny, cy); maxy = max(maxy, cy)
                for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    ny, nx = cy + dy, cx + dx
                    if 0 <= ny < m.shape[0] and 0 <= nx < m.shape[1] and m[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        q.append((ny, nx))
            if best is None or n > best[0]:
                best = (n, minx, miny, maxx, maxy)
    if best is None or best[0] < 50:
        raise SystemExit("auto_ball: no bright blob found; pass --*-ball cx,cy,r")
    _, minx, miny, maxx, maxy = best
    cx = (minx + maxx + 1) * 0.5 * small
    cy = (miny + maxy + 1) * 0.5 * small
    r = max(maxx - minx + 1, maxy - miny + 1) * 0.5 * small
    return cx, cy, r


def bilinear_grey(g, x, y):
    h, w = g.shape
    x0 = np.floor(x).astype(int)
    y0 = np.floor(y).astype(int)
    fx = x - x0
    fy = y - y0
    x0c = np.clip(x0, 0, w - 1); x1c = np.clip(x0 + 1, 0, w - 1)
    y0c = np.clip(y0, 0, h - 1); y1c = np.clip(y0 + 1, 0, h - 1)
    return (g[y0c, x0c] * (1 - fx) * (1 - fy) + g[y0c, x1c] * fx * (1 - fy)
            + g[y1c, x0c] * (1 - fx) * fy + g[y1c, x1c] * fx * fy)


def refine_circle(img, cx, cy, r, rays=96, samples=80):
    """Fit the silhouette: for each ray, take the strongest radial gradient around r,
    then least-squares fit a circle to those edge points. Removes the few-pixel bias a
    hand-picked ROI has, which otherwise dominates NCC/highlight-centroid metrics."""
    g = luma(img)
    ts = np.linspace(0.0, 2.0 * math.pi, rays, endpoint=False)
    rs = np.linspace(r * 0.80, r * 1.20, samples)
    pts = []
    for t in ts:
        xs = cx + np.cos(t) * rs
        ys = cy + np.sin(t) * rs
        vals = bilinear_grey(g, xs, ys)
        grad = np.abs(np.diff(vals))
        # A silhouette is a monotonic step, so prefer the largest gradient close to r.
        idx = int(np.argmax(grad))
        rr = rs[idx] + 0.5 * (rs[1] - rs[0])
        pts.append((cx + math.cos(t) * rr, cy + math.sin(t) * rr))
    p = np.array(pts)
    # x^2 + y^2 = 2a x + 2b y + c
    A = np.column_stack([2 * p[:, 0], 2 * p[:, 1], np.ones(len(p))])
    b = (p ** 2).sum(axis=1)
    sol, *_ = np.linalg.lstsq(A, b, rcond=None)
    a, bb, c = sol
    rr = math.sqrt(max(c + a * a + bb * bb, 1e-6))
    return float(a), float(bb), float(rr)


def sample_disk(img, cx, cy, r, size=SIZE):
    """Bilinear sample of the disk into a size x size square; outside the disk = 0."""
    h, w, _ = img.shape
    out = np.zeros((size, size, 3), np.float64)
    mask = np.zeros((size, size), bool)
    yy, xx = np.mgrid[0:size, 0:size]
    u = (xx + 0.5) / size * 2.0 - 1.0
    v = (yy + 0.5) / size * 2.0 - 1.0
    # Leave the outermost 1.5% out: the silhouette pixels are a mix of ball and
    # background, and their gradient dominates correlation metrics.
    mask = u * u + v * v <= 0.985 * 0.985
    sx = cx + u * r
    sy = cy + v * r
    x0 = np.floor(sx).astype(int); y0 = np.floor(sy).astype(int)
    fx = (sx - x0)[..., None]; fy = (sy - y0)[..., None]
    x0c = np.clip(x0, 0, w - 1); x1c = np.clip(x0 + 1, 0, w - 1)
    y0c = np.clip(y0, 0, h - 1); y1c = np.clip(y0 + 1, 0, h - 1)
    out = (img[y0c, x0c] * (1 - fx) * (1 - fy) + img[y0c, x1c] * fx * (1 - fy)
           + img[y1c, x0c] * (1 - fx) * fy + img[y1c, x1c] * fx * fy)
    out[~mask] = 0.0
    return out, mask


def ncc(a, b, mask):
    va = a[mask] - a[mask].mean()
    vb = b[mask] - b[mask].mean()
    d = va.std() * vb.std()
    return float((va * vb).mean() / d) if d > 1e-9 else 0.0


def angular_profile(g, mask, bins=36):
    h, w = g.shape
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx + 0.5) / w * 2 - 1
    v = (yy + 0.5) / h * 2 - 1
    ang = (np.degrees(np.arctan2(v, u)) + 360.0) % 360.0
    idx = (ang / 360.0 * bins).astype(int) % bins
    prof = np.zeros(bins)
    for i in range(bins):
        m = mask & (idx == i)
        prof[i] = g[m].mean() if m.any() else 0.0
    return prof


def radial_profile(g, mask, bins=8):
    h, w = g.shape
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx + 0.5) / w * 2 - 1
    v = (yy + 0.5) / h * 2 - 1
    rad = np.sqrt(u * u + v * v)
    idx = np.clip((rad * bins).astype(int), 0, bins - 1)
    prof = np.zeros(bins)
    for i in range(bins):
        m = mask & (idx == i)
        prof[i] = g[m].mean() if m.any() else 0.0
    return prof


def highlight(g, mask, percentile=99.5):
    vals = g[mask]
    thr = float(np.percentile(vals, percentile))
    hl = mask & (g >= thr)
    if not hl.any():
        return 0.0, 0.0, 0.0, thr
    h, w = g.shape
    yy, xx = np.mgrid[0:h, 0:w]
    u = (xx + 0.5) / w * 2 - 1
    v = (yy + 0.5) / h * 2 - 1
    # Weight by how far above the threshold, so one clipped pixel does not move the
    # centroid as much as the body of a softbox reflection.
    weight = np.where(hl, g - thr, 0.0)
    wsum = max(float(weight.sum()), 1e-9)
    cx = float((weight * u).sum() / wsum)
    cy = float((weight * v).sum() / wsum)
    area = float(hl.sum() / mask.sum())
    return cx, cy, area, thr


def orientation_hist(g, mask, bins=18):
    gy, gx = np.gradient(g)
    mag = np.hypot(gx, gy)
    ang = (np.degrees(np.arctan2(gy, gx)) + 180.0) % 180.0
    idx = np.clip((ang / 180.0 * bins).astype(int), 0, bins - 1)
    hist = np.zeros(bins)
    for i in range(bins):
        hist[i] = mag[mask & (idx == i)].sum()
    return hist / max(hist.sum(), 1e-9)


def blurred(g, sigma):
    im = Image.fromarray((np.clip(g, 0.0, 1.0) * 255).astype(np.uint8))
    return np.asarray(im.filter(ImageFilter.GaussianBlur(sigma))).astype(np.float64) / 255.0


def highpass_ncc(gr, gt, mask, sigma=8.0):
    """NCC of the images minus their low-frequency shading.

    The mean/gradient of the ball moves with the camera rig; the reflections are what
    has to match, so compare the high-passed luminance instead of the raw one.
    """
    return ncc(gr - blurred(gr, sigma), gt - blurred(gt, sigma), mask)


def peaks(g, mask, percentile=99.0, min_dist=10):
    """Local maxima above a percentile, greedily thinned. Returns [(x, y), ...]."""
    thr = float(np.percentile(g[mask], percentile))
    mx = np.asarray(Image.fromarray((np.clip(g, 0, 1) * 255).astype(np.uint8))
                    .filter(ImageFilter.MaxFilter(3))).astype(np.float64) / 255.0
    local = mask & (g >= thr) & (g >= mx - 1e-6)
    ys, xs = np.nonzero(local)
    order = np.argsort(-g[ys, xs])
    picked = []
    for i in order:
        x, y = int(xs[i]), int(ys[i])
        if all((x - px) ** 2 + (y - py) ** 2 >= min_dist ** 2 for px, py in picked):
            picked.append((x, y))
    return picked


def metrics(ref_disk, test_disk, mask):
    gr = luma(ref_disk); gt = luma(test_disk)
    vr = gr[mask]; vt = gt[mask]
    out = {}
    out['ref_mean'] = float(vr.mean()); out['test_mean'] = float(vt.mean())
    out['mean_diff_pct'] = float((vt.mean() - vr.mean()) / max(vr.mean(), 1e-6) * 100)
    out['ref_median'] = float(np.median(vr)); out['test_median'] = float(np.median(vt))
    out['ref_std'] = float(vr.std()); out['test_std'] = float(vt.std())
    out['ref_p90'] = float(np.percentile(vr, 90)); out['test_p90'] = float(np.percentile(vt, 90))
    out['ref_p99'] = float(np.percentile(vr, 99)); out['test_p99'] = float(np.percentile(vt, 99))
    out['ncc'] = ncc(gr, gt, mask)
    out['hp_ncc'] = highpass_ncc(gr, gt, mask)
    pr = peaks(gr, mask); pt = peaks(gt, mask)
    out['ref_peaks'] = len(pr); out['test_peaks'] = len(pt)
    dists = []
    for (x, y) in pr:
        u = (x + 0.5) / SIZE * 2 - 1
        v = (y + 0.5) / SIZE * 2 - 1
        if pt:
            best = min(math.hypot(u - ((qx + 0.5) / SIZE * 2 - 1),
                                  v - ((qy + 0.5) / SIZE * 2 - 1)) for qx, qy in pt)
        else:
            best = 9.0
        dists.append(best)
    out['peak_match_median'] = float(np.median(dists)) if dists else 9.0
    out['orient_corr'] = float(np.corrcoef(orientation_hist(gr, mask), orientation_hist(gt, mask))[0, 1])
    pr = angular_profile(gr, mask); pt = angular_profile(gt, mask)
    out['angular_corr'] = float(np.corrcoef(pr, pt)[0, 1])
    out['angular_l1'] = float(np.abs(pr - pt).mean())
    out['radial_l1'] = float(np.abs(radial_profile(gr, mask) - radial_profile(gt, mask)).mean())
    hr = highlight(gr, mask); ht = highlight(gt, mask)
    out['ref_hl'] = hr; out['test_hl'] = ht
    out['hl_centroid_dist'] = float(math.hypot(hr[0] - ht[0], hr[1] - ht[1]))
    out['hl_area_ratio'] = float(ht[2] / max(hr[2], 1e-6))
    return out


def save_montage(ref_disk, test_disk, mask, path):
    diff = np.abs(luma(ref_disk) - luma(test_disk))[..., None].repeat(3, axis=2)
    panels = [ref_disk, test_disk, diff]
    canvas = np.zeros((SIZE, SIZE * 3 + 20, 3), np.uint8)
    for i, p in enumerate(panels):
        canvas[:, i * (SIZE + 10):i * (SIZE + 10) + SIZE] = np.clip(p * 255.0, 0, 255).astype(np.uint8)
    Image.fromarray(canvas).save(path)


def parse_ball(s):
    if not s:
        return None
    x, y, r = (float(v) for v in s.split(','))
    return x, y, r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--ref', required=True)
    ap.add_argument('--test', required=True)
    ap.add_argument('--ref-ball', default=None)
    ap.add_argument('--test-ball', default=None)
    ap.add_argument('--out', default=None)
    ap.add_argument('--csv', default=None)
    ap.add_argument('--label', default='pair')
    ap.add_argument('--no-refine', action='store_true',
                    help='skip the silhouette circle fit (keep the given ROI as-is)')
    args = ap.parse_args()

    ref = np.asarray(Image.open(args.ref).convert('RGB')).astype(np.float64) / 255.0
    test = np.asarray(Image.open(args.test).convert('RGB')).astype(np.float64) / 255.0

    rb = parse_ball(args.ref_ball) or auto_ball(ref)
    tb = parse_ball(args.test_ball) or auto_ball(test)
    if not args.no_refine:
        rb = refine_circle(ref, *rb)
        tb = refine_circle(test, *tb)
    print(f'ref  ball: {rb[0]:.1f},{rb[1]:.1f} r={rb[2]:.1f}')
    print(f'test ball: {tb[0]:.1f},{tb[1]:.1f} r={tb[2]:.1f}')

    ref_disk, mask = sample_disk(ref, *rb)
    test_disk, _ = sample_disk(test, *tb)
    m = metrics(ref_disk, test_disk, mask)

    print(f'{"metric":18} {"ref":>8} {"test":>8} {"diff":>9}')
    print(f'{"mean":18} {m["ref_mean"]:8.3f} {m["test_mean"]:8.3f} {m["mean_diff_pct"]:+8.2f}%')
    print(f'{"median":18} {m["ref_median"]:8.3f} {m["test_median"]:8.3f}')
    print(f'{"std":18} {m["ref_std"]:8.3f} {m["test_std"]:8.3f}')
    print(f'{"p90":18} {m["ref_p90"]:8.3f} {m["test_p90"]:8.3f}')
    print(f'{"p99":18} {m["ref_p99"]:8.3f} {m["test_p99"]:8.3f}')
    print(f'{"ncc":18} {m["ncc"]:8.3f}')
    print(f'{"hp_ncc":18} {m["hp_ncc"]:8.3f}   (reflections, low-frequency removed)')
    print(f'{"peaks":18} {m["ref_peaks"]:8d} {m["test_peaks"]:8d}   match median={m["peak_match_median"]:.3f}')
    print(f'{"angular_corr":18} {m["angular_corr"]:8.3f}   l1={m["angular_l1"]:.3f}')
    print(f'{"radial_l1":18} {m["radial_l1"]:8.3f}')
    print(f'{"orient_corr":18} {m["orient_corr"]:8.3f}')
    print(f'{"hl_centroid_dist":18} {m["hl_centroid_dist"]:8.3f}   area ratio={m["hl_area_ratio"]:.3f}')

    if args.out:
        os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
        save_montage(ref_disk, test_disk, mask, args.out)
        print('montage ->', args.out)

    if args.csv:
        os.makedirs(os.path.dirname(os.path.abspath(args.csv)), exist_ok=True)
        row = {'label': args.label,
               'ref_mean': m['ref_mean'], 'test_mean': m['test_mean'],
               'mean_diff_pct': m['mean_diff_pct'], 'ncc': m['ncc'], 'hp_ncc': m['hp_ncc'],
               'ref_peaks': m['ref_peaks'], 'test_peaks': m['test_peaks'],
               'peak_match_median': m['peak_match_median'],
               'angular_corr': m['angular_corr'], 'angular_l1': m['angular_l1'],
               'radial_l1': m['radial_l1'], 'orient_corr': m['orient_corr'],
               'hl_centroid_dist': m['hl_centroid_dist'], 'hl_area_ratio': m['hl_area_ratio']}
        exists = os.path.exists(args.csv)
        with open(args.csv, 'a', newline='', encoding='utf-8') as f:
            wr = csv.DictWriter(f, fieldnames=list(row.keys()))
            if not exists:
                wr.writeheader()
            wr.writerow(row)
        print('csv ->', args.csv)


if __name__ == '__main__':
    main()
