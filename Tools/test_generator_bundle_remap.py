# Copyright 2026 Hugo Beyer. All Rights Reserved.
"""Offline reference/static contracts; these do not execute or compile GPU shaders."""

import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
INVALID_REGION = 0xFFFFFFFF


def anchor(uv, size):
    if not all(math.isfinite(v) for v in uv) or min(size) <= 0:
        return None
    return tuple(min(math.floor((v - math.floor(v)) * n), n - 1)
                 for v, n in zip(uv, size))


def taps(uv, size):
    texel = tuple((v - math.floor(v)) * n - 0.5 for v, n in zip(uv, size))
    base = tuple(math.floor(v) for v in texel)
    tx, ty = (v - math.floor(v) for v in texel)
    weights = ((1 - tx) * (1 - ty), tx * (1 - ty), (1 - tx) * ty, tx * ty)
    return [(((base[0] + (i & 1)) % size[0],
              (base[1] + (i >> 1)) % size[1]), w) for i, w in enumerate(weights)]


def bed_coordinate(uv, size, ids, positions):
    p = anchor(uv, size)
    owner, t0 = ids[p], positions[p]
    if owner == INVALID_REGION:
        return t0
    kept = [(positions[q], w) for q, w in taps(uv, size)
            if w > 0 and ids[q] == owner and abs(positions[q] - t0) < 0.5]
    return sum(t * w for t, w in kept) / sum(w for _, w in kept)


def valid_distance(uv, size, field):
    kept = [(field[p][0], w) for p, w in taps(uv, size)
            if w > 0 and field[p][1] > 0.5 and all(math.isfinite(v) for v in field[p])]
    return sum(d * w for d, w in kept) / sum(w for _, w in kept) if kept else None


def coordinate_sample(uv, size, coordinate):
    displacement = [0.0, 0.0]
    for p, weight in taps(uv, size):
        centre = tuple((c + 0.5) / n for c, n in zip(p, size))
        stored = coordinate(centre)
        for axis in range(2):
            displacement[axis] += weight * (stored[axis] - centre[axis])
    return tuple(v + d for v, d in zip(uv, displacement))


class GeneratorBundleRemapTests(unittest.TestCase):
    def test_wrapped_anchor_and_discrete_random(self):
        size = (7, 3)
        for uv in ((-2.01, -1.0), (0.0, 1.0), (4.0, -3.0),
                   (math.nextafter(1.0, 0.0), math.nextafter(1.0, 0.0))):
            p = anchor(uv, size)
            self.assertTrue(all(0 <= c < n for c, n in zip(p, size)))
        self.assertEqual(anchor((1.0, -1.0), size), (0, 0))
        self.assertIsNone(anchor((math.nan, 0.0), size))
        self.assertIsNone(anchor((math.inf, 0.0), size))
        ids = {(x, y): 100 + x for x in range(7) for y in range(3)}
        randoms = {p: ids[p] / 1000 for p in ids}
        p = anchor((0.5, 0.5), size)
        self.assertEqual((ids[p], randoms[p]), (103, 0.103))
        ids[p] = INVALID_REGION
        self.assertEqual(ids[anchor((0.5, 0.5), size)], INVALID_REGION)

    def test_owner_and_phase_branch(self):
        size, uv = (2, 2), (0.5, 0.5)
        ids = {(x, y): 7 for x in range(2) for y in range(2)}
        positions = {(0, 0): 0.98, (1, 0): 0.02, (0, 1): 0.04, (1, 1): 0.06}
        # Repeated IDs (including BedPeriod=1) do not permit averaging through the reset.
        self.assertAlmostEqual(bed_coordinate(uv, size, ids, positions), 0.04)
        ids[(0, 1)] = 8
        self.assertAlmostEqual(bed_coordinate(uv, size, ids, positions), 0.04)
        positions[(0, 0)] = 0.56  # Exact half-cycle ambiguity is excluded.
        ids[(0, 0)] = 7
        self.assertAlmostEqual(bed_coordinate(uv, size, ids, positions), 0.04)
        ids[(1, 1)] = INVALID_REGION
        positions[(1, 1)] = -3.0
        self.assertEqual(bed_coordinate(uv, size, ids, positions), -3.0)

    def test_distance_validity_sign_and_nonunit_metric(self):
        field = {(0, 0): (-2.0, 1.0), (1, 0): (1e9, 0.0),
                 (0, 1): (-4.0, 1.0), (1, 1): (1e9, 0.0)}
        self.assertEqual(valid_distance((0.5, 0.5), (2, 2), field), -3.0)
        self.assertIsNone(valid_distance((0.75, 0.25), (2, 2), field))
        g, x, y = (2.0, 0.0), (0.5, 0.0), (0.0, 1.0)
        pulled = (sum(a * b for a, b in zip(g, x)), sum(a * b for a, b in zip(g, y)))
        ratio = math.hypot(*g) / math.hypot(*pulled)
        self.assertEqual(-3.0 * ratio, -6.0)
        self.assertEqual(math.hypot(*g) / math.hypot(*g), 1.0)

    def test_lifted_map_seam_and_composition_order(self):
        size = (32, 16)
        translated = lambda p: (p[0] + 3.25, p[1] - 2.0)
        for uv in ((-0.01, 0.5), (0.99, 0.5), (1.01, 0.5)):
            actual = coordinate_sample(uv, size, translated)
            for got, expected in zip(actual, translated(uv)):
                self.assertAlmostEqual(got, expected)
        # U o W, not displacement addition, with two noncommuting periodic maps.
        u = lambda p: (p[0] + 0.1 * math.sin(2 * math.pi * p[1]), p[1])
        w = lambda p: (p[0], p[1] + 0.25)
        p = (0.5 / size[0], 0.5 / size[1])
        actual = coordinate_sample(w(p), size, u)
        self.assertAlmostEqual(actual[0], u(w(p))[0])
        self.assertNotAlmostEqual(actual[0], w(u(p))[0])

    def test_cpu_shader_wiring_and_ownership(self):
        cpu = (ROOT / 'Source/MixtormatShaders/Private/MixtormatGpuGeneratorPasses.cpp').read_text()
        shader = (ROOT / 'Shaders/Private/MixtormatGeneratorBundle.usf').read_text()
        remap = cpu.split('void RemapGeneratorBundle(', 1)[1].split('// The bedding', 1)[0]
        self.assertIn('const FGeneratorBundle Source = Bundle;', remap)
        self.assertIn('Source.NamedMaskDescriptors.Find', remap)
        self.assertIn('Source.BoundaryField, Source.RegionIds, Descriptor.InvalidDistance', remap)
        self.assertIn('Moved.NamedMasks[Mask.Key] = Source.Coverage;', remap)
        self.assertNotIn('RemapBundleField(Ctx, Source.Height', remap)
        self.assertNotIn('RemapBundleField(Ctx, Source.Coverage', remap)
        self.assertNotIn('RockEdgeDistance', remap)
        self.assertNotIn('PebbleEdgeDistance', remap)
        self.assertIn('SHADER_PERMUTATION_INT("BUNDLE_STAGE", 12)', cpu)
        self.assertIn('BUNDLE_STAGE == 10', shader)
        self.assertIn('BUNDLE_STAGE == 11', shader)
        self.assertIn('Scale = SourceLength / DestinationLength;', shader)
        self.assertIn('MixtormatGeneratorWarpSampleCoordinate(VectorField, UV, OutputSize)', shader)


if __name__ == '__main__':
    unittest.main()
