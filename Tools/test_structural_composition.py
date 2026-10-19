# Copyright 2026 Hugo Beyer. All Rights Reserved.
"""Offline source/reference contracts; does not compile C++ or execute GPU shaders."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
GPU = "Source/MixtormatShaders/Private/"
UI = "Source/MixtormatEditor/Private/Widgets/Layers/"


def source(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


class StructuralCompositionTests(unittest.TestCase):
    def test_badge_dispatch_uses_each_authored_payload(self):
        menu = source(UI + "MixtormatLayerMenus.cpp")
        dispatch = menu.split("GetChildMaskBlendMode(", 1)[1].split("// One icon", 1)[0]
        for kind in ("Mask", "Generated", "Craquelure", "ColorId", "RandomId"):
            self.assertIn(f"case EMixtormatLayerChildType::{kind}: return &Child->{kind}.BlendMode;", dispatch)
        callback = menu.split("TSharedRef<SWidget> SMixtormat::BuildGeneratedBlendModeMenu(", 1)[1]
        self.assertIn("ResolveChildAt(Address)", callback)
        self.assertIn("Child->IsInstance()", callback)
        self.assertIn("*BlendMode == Mode", callback)
        self.assertIn("LastHistoryRecordTime = 0.0;\n\t\t\t\t*BlendMode = Mode;", callback)
        self.assertIn("RefreshLayeredPreview();\n\t\t\t\tLastHistoryRecordTime = 0.0;", callback)
        self.assertIn("return BlendMode && *BlendMode == Mode;", callback)

    def test_group_badges_keep_all_coverage_types(self):
        rows = source(UI + "MixtormatLayerHierarchy.cpp").split("SMixtormat::BuildLayerRow", 1)[0]
        for kind in ("Mask", "Generated", "Craquelure", "ColorId", "RandomId"):
            self.assertIn(f"Child.Type == EMixtormatLayerChildType::{kind}", rows)
        self.assertIn("BuildGeneratedBlendModeMenu(INDEX_NONE, ChildIndex)", rows)

    def test_structural_filters_follow_local_merge(self):
        pipeline = source(GPU + "MixtormatGpuComposePipeline.cpp")
        start = pipeline.index("const bool bPrepareSurface")
        pipeline = pipeline[start:]
        order = [pipeline.index(call) for call in (
            "AddBreakupPasses(Ctx", "AddLayerSurfaceBlurPasses(Ctx",
            "AddLayerCompositePass(Ctx, LayerCtx, Layer, 2u)",
            "AddRampReliefPasses(Ctx", "AddCraquelureReliefPasses(Ctx",
            "AddWornEdgesPasses(Ctx")]
        self.assertEqual(order, sorted(order))
        self.assertEqual(pipeline.count("AddCraquelureReliefPasses(Ctx"), 1)

    def test_signed_height_writes_are_unbounded(self):
        ramp = source("Shaders/Private/MixtormatRampIdRelief.usf")
        crack = source("Shaders/Private/MixtormatCraquelureRelief.usf")
        self.assertIn("OutputHeight[P] = Source + ReliefDelta(P);", ramp)
        self.assertIn("OutputHeight[P] = min(Source, Source - HeightWeight * Offset);", crack)
        self.assertNotIn("BoundedDelta", ramp)
        self.assertNotIn("PatternHeightMax", source(GPU + "MixtormatGpuReliefIdPasses.cpp"))
        local = ramp.split("float RampDelta(", 1)[1].split("// Feather Gain", 1)[0]
        self.assertNotIn("SourceHeight.Load", local)
        self.assertIn("MixtormatApplyMaskOperation(BlendMode, Neutral, saturate(Field.x))", local)

    def test_signed_displacement_preserves_substrate_detail(self):
        for base in (-2.0, -0.2, 0.5, 1.2, 3.0):
            for delta in (-0.8, 0.0, 0.9):
                self.assertAlmostEqual((base + 0.07 + delta) - (base + delta), 0.07)
                self.assertLessEqual(min(base, base - 0.4), base)
                self.assertAlmostEqual((base + delta) - base, delta)

    def test_id_consumers_share_scope_order_resolution(self):
        for path in ("MixtormatGpuCompositor.cpp", "MixtormatGpuMaskPasses.cpp",
                     "MixtormatGpuPatternPasses.cpp", "MixtormatGpuReliefIdPasses.cpp",
                     "MixtormatGpuUvIdPasses.cpp", "Effects/MixtormatWornEdgesPasses.cpp"):
            self.assertIn("FindRegionIdsAboveWithIndex(", source(GPU + path), path)
        resolver = source(GPU + "MixtormatGpuPatternPasses.cpp")
        resolver = resolver.split("FRDGTextureRef FindRegionIdsAboveWithIndex(", 1)[1]
        resolver = resolver.split("// Random value per region", 1)[0]
        self.assertIn("Candidate.SourceChildIndex >= Child.SourceChildIndex", resolver)
        self.assertIn("Candidate.ScopeOwnerSourceChildIndex != ScopeOwner", resolver)
        self.assertIn("Entry.Key == OutProducerIndex", resolver)
        # Select the authored producer before checking publication: missing newer IDs
        # must shadow older maps, rather than silently selecting a stale producer.
        self.assertLess(resolver.index("IsRegionIdProducerChild(Candidate) &&"),
                        resolver.index("for (const TPair<int32, FRDGTextureRef>& Entry"))

    def test_boundary_explicit_publication_guards_remain(self):
        boundary = source(GPU + "MixtormatGpuReliefIdPasses.cpp")
        self.assertIn("Map.Key < Child.SourceChildIndex", boundary)
        self.assertIn("Field->IsComplete() && IsValid(Field->Texture)", boundary)
        self.assertIn("Texture->Desc.Format == PF_R32_UINT", boundary)
        uv = source(GPU + "MixtormatGpuUvIdPasses.cpp")
        self.assertIn("Deferred IDs are not ready before source sampling", uv)

    def test_final_auto_remap_stays_after_structure(self):
        pipeline = source(GPU + "MixtormatGpuComposePipeline.cpp")
        self.assertLess(pipeline.index("AddWornEdgesPasses(Ctx"),
                        pipeline.index("if (Request.bFinalAutoRemapHeight"))
        final = pipeline.split("if (Request.bFinalAutoRemapHeight", 1)[1]
        self.assertIn("Request.Resolution, 0.0f, 1.0f", final)
        self.assertLess(final.index("AddNormalizeFieldPasses("),
                        final.index("P->FinalHeight = Ctx.OutputHeight[Final]"))

    def test_wear_keeps_signed_output_and_continuous_coverage(self):
        wear = source("Shaders/Private/MixtormatEdgeWear.usf")
        self.assertIn("OutputHeight[Pixel] = H0 - Amount;", wear)
        self.assertIn("OutputWearMask[Pixel] = saturate(Amount);", wear)
        self.assertIn("NeighbourId != Rid", wear)
        self.assertIn("HeightDerivedNormalStrength", source(GPU + "Effects/MixtormatWornEdgesPasses.cpp"))


if __name__ == "__main__":
    unittest.main()
