#!/usr/bin/env python3
"""Where --flakes' tolerances come from.

The flake shader (source/Shaders.cpp, kFlakeFragment) draws each flake as an
ellipse with semi-axes w1, w2 (each at least half a pixel), its coverage a
one-pixel ramp across the edge, clamp(0.5 - (r - 1) / |grad r|), with r the
ellipse's own radius, box-filtered over every pixel by a 4 x 4 midpoint rule.
The CPU scales it by s1 s2 / (w1 w2 + (w1/w2 + w2/w1) / 24), the ramp's
integral in closed form (Snowglobe.cpp, buildFlakes), so the pixels should
sum to the true area pi s1 s2 and centre on the true centre.

This draws 6,000 seeded ellipses the same way -- 0.2 to 6 px, every aspect
from 1 to 1/20, every angle and sub-pixel offset -- and prints the worst
area and centroid errors the box rule leaves. The harness's tolerances
(kFlakeAreaTolerance, kFlakePositionTolerance) round them up.

    python3 tools/coverage.py [--within AREA POSITION]

`--within` (verify.sh passes the harness's two constants) exits 1 if the box
rule's worst error is not inside them.
"""
import math
import sys

import numpy as np


def cover(px, py, cx, cy, a1, a2):
    rx, ry = px - cx, py - cy
    l1, l2 = a1 @ a1, a2 @ a2
    qx = (rx * a1[0] + ry * a1[1]) / l1
    qy = (rx * a2[0] + ry * a2[1]) / l2
    r = np.sqrt(qx * qx + qy * qy)
    rr = np.maximum(r, 1e-6)
    gx = (qx / rr) * a1[0] / l1 + (qy / rr) * a2[0] / l2
    gy = (qx / rr) * a1[1] / l1 + (qy / rr) * a2[1] / l2
    d = (r - 1.0) / np.maximum(np.hypot(gx, gy), 1e-6)
    return np.clip(0.5 - d, 0.0, 1.0)


def main():
    rng = np.random.default_rng(3)
    offsets = (np.arange(4) + 0.5) / 4 - 0.5
    centres = np.arange(-10, 11) + 0.5
    X, Y = np.meshgrid(centres, centres)
    worst_area = worst_centre = 0.0
    for _ in range(6000):
        s1 = math.exp(rng.uniform(math.log(0.2), math.log(6.0)))
        s2 = s1 * rng.uniform(0.05, 1.0)
        th = rng.uniform(0.0, math.pi)
        cx, cy = rng.uniform(-0.5, 0.5, 2)
        w1, w2 = max(s1, 0.5), max(s2, 0.5)
        u = np.array([math.cos(th), math.sin(th)])
        v = np.array([-u[1], u[0]])
        c = np.zeros_like(X)
        for ox in offsets:
            for oy in offsets:
                c += cover(X + ox, Y + oy, cx, cy, w1 * u, w2 * v)
        c /= 16.0
        alpha = s1 * s2 / (w1 * w2 + (w1 / w2 + w2 / w1) / 24.0)
        worst_area = max(worst_area, abs(c.sum() * alpha / (math.pi * s1 * s2) - 1.0))
        mx, my = (c * X).sum() / c.sum(), (c * Y).sum() / c.sum()
        worst_centre = max(worst_centre, math.hypot(mx - cx, my - cy))
    print(f"6000 ellipses, 0.2-6 px, aspect 1-1/20: worst area {100 * worst_area:.3f} %, worst centroid {worst_centre:.4f} px")
    if len(sys.argv) == 4 and sys.argv[1] == "--within":
        area, position = float(sys.argv[2]), float(sys.argv[3])
        inside = worst_area < area and worst_centre < position
        print(f"{'ok  ' if inside else 'FAIL'} inside the harness's tolerances ({100 * area:.1f} %, {position} px): {'yes' if inside else 'NO'}")
        return 0 if inside else 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
