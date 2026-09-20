
from pathlib import Path
import shutil

ROOT = Path.cwd()

def req(rel):
    p = ROOT / rel
    if not p.exists():
        raise SystemExit(f"Missing required file: {rel}")
    return p

def backup(p):
    b = p.with_suffix(p.suffix + ".breakup.bak")
    if not b.exists():
        shutil.copy2(p, b)

def write(rel, text):
    p = req(rel)
    backup(p)
    p.write_text(text, encoding="utf-8", newline="\n")
    print("updated", rel)

def replace_function(text, signature, replacement):
    s = text.find(signature)
    if s < 0:
        raise RuntimeError(f"Function not found: {signature}")
    b = text.find("{", s)
    depth = 0
    end = None
    for i in range(b, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        raise RuntimeError(f"Unbalanced function: {signature}")
    return text[:s] + replacement + text[end:]

def replace_region(text, start_marker, end_marker, replacement):
    s = text.find(start_marker)
    e = text.find(end_marker, s)
    if s < 0 or e < 0:
        raise RuntimeError(f"Region not found: {start_marker} -> {end_marker}")
    return text[:s] + replacement + text[e:]

gpu_rel = "Source/MixtormatShaders/Private/MixtormatGpuEffectPasses.cpp"
gpu = req(gpu_rel).read_text(encoding="utf-8")

shader_classes = r'''// Breakup is two dispatches: the first evaluates the three procedural SDF families once and
// publishes signed distance + stable per-piece random; the second interprets that field against
// the incoming height. No iterative state, min/max reduction or ping-pong.
class FMixtormatBreakupFieldCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupFieldCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupFieldCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(uint32, Seed)
		SHADER_PARAMETER(int32, MacroCells)
		SHADER_PARAMETER(int32, MidCells)
		SHADER_PARAMETER(int32, DetailCells)
		SHADER_PARAMETER(float, Density)
		SHADER_PARAMETER(float, SizeMin)
		SHADER_PARAMETER(float, SizeMax)
		SHADER_PARAMETER(float, Stretch)
		SHADER_PARAMETER(float, Angularity)
		SHADER_PARAMETER(float, Jitter)
		SHADER_PARAMETER(int32, OperationMid)
		SHADER_PARAMETER(int32, OperationDetail)
		SHADER_PARAMETER(float, BlendSmooth)
		SHADER_PARAMETER(float, DistortAmount)
		SHADER_PARAMETER(int32, DistortFrequency)
		SHADER_PARAMETER(uint32, InvertField)
		SHADER_PARAMETER(uint32, HasRegionIds)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<uint>, RegionIds)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float2>, OutputField)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupFieldCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"FieldCS",
	SF_Compute);

class FMixtormatBreakupApplyCS final : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FMixtormatBreakupApplyCS);
	SHADER_USE_PARAMETER_STRUCT(FMixtormatBreakupApplyCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, OutputSize)
		SHADER_PARAMETER(float, Relief)
		SHADER_PARAMETER(float, FoldHeight)
		SHADER_PARAMETER(float, FoldWidth)
		SHADER_PARAMETER(float, CreaseWidth)
		SHADER_PARAMETER(float, CreaseDepth)
		SHADER_PARAMETER(float, PushAmount)
		SHADER_PARAMETER(float, PushWidth)
		SHADER_PARAMETER(float, Variation)
		SHADER_PARAMETER(float, Strength)
		SHADER_PARAMETER(uint32, UsePlacementMask)
		SHADER_PARAMETER(float, PlacementMaskTiling)
		SHADER_PARAMETER(uint32, InvertMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, SourceHeight)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float2>, BreakupField)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, LayerMask)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, PlacementMaskTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LinearWrapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputHeight)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<float>, OutputCoverage)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(
	FMixtormatBreakupApplyCS,
	"/Plugin/Mixtormat/Private/MixtormatBreakup.usf",
	"ApplyCS",
	SF_Compute);

'''

gpu = replace_region(
    gpu,
    "// Min/max of a scalar texture",
    "// Worn Edges is a post-composite height filter.",
    shader_classes + "// Worn Edges is a post-composite height filter."
)

add_breakup = r'''void AddBreakupPasses(
		FMixtormatComposeContext& Ctx,
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer)
	{
		FRDGBuilder& GraphBuilder = Ctx.GraphBuilder;
		const FRenderRequest& Request = Ctx.Request;
		TMap<FRHITexture*, FRDGTextureRef>& RegisteredTextures = Ctx.RegisteredTextures;
		FRDGTextureRef* const OutputN = Ctx.OutputN;
		FRDGTextureRef* const OutputRAM = Ctx.OutputRAM;
		FRDGTextureRef* const HeightTargets = Ctx.OutputHeight;
		const int32 LayerIndex = LayerCtx.LayerIndex;
		const int32 WriteIndex = LayerIndex & 1;
		const FRDGTextureRef PlacementDummy = LayerCtx.PeelFieldDummy;

		TShaderMapRef<FMixtormatBreakupFieldCS> FieldShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatBreakupApplyCS> ApplyShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		TShaderMapRef<FMixtormatCarveShadeCS> CarveShadeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		const FIntVector Groups(
			FMath::DivideAndRoundUp(Request.Resolution.X, 8),
			FMath::DivideAndRoundUp(Request.Resolution.Y, 8),
			1);

		for (int32 BreakupIndex = 0; BreakupIndex < LayerCtx.PendingBreakups.Num(); ++BreakupIndex)
		{
			const FPendingBreakup& PendingBreakup = LayerCtx.PendingBreakups[BreakupIndex];
			if (!PendingBreakup.Effect || PendingBreakup.Effect->BreakupAmount <= 0.0f)
				continue;

			const FEffectRenderData& Breakup = *PendingBreakup.Effect;
			const bool bUsePlacementMask =
				!PendingBreakup.bHasScopedMask && Breakup.BreakupPlacementMask.IsValid();
			FRDGTextureRef PlacementMask = bUsePlacementMask
				? RegisterTexture(GraphBuilder, RegisteredTextures, Breakup.BreakupPlacementMask,
					TEXT("Mixtormat.BreakupPlacementMask"))
				: PlacementDummy;

			const FRDGTextureDesc FieldDesc = FRDGTextureDesc::Create2D(
				Request.Resolution, PF_G16R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);
			const FRDGTextureDesc CoverageDesc = FRDGTextureDesc::Create2D(
				Request.Resolution, PF_R16F, FClearValueBinding::Black,
				TexCreate_ShaderResource | TexCreate_UAV);

			FRDGTextureRef Field = GraphBuilder.CreateTexture(FieldDesc, TEXT("Mixtormat.Breakup.Field"));
			FRDGTextureRef Coverage = GraphBuilder.CreateTexture(CoverageDesc, TEXT("Mixtormat.Breakup.Coverage"));
			FRDGTextureRef SourceH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.SourceH"));
			FRDGTextureRef ResultH = GraphBuilder.CreateTexture(
				HeightTargets[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.Height"));
			FRDGTextureRef ResultN = GraphBuilder.CreateTexture(
				OutputN[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.Normal"));
			FRDGTextureRef ResultRAM = GraphBuilder.CreateTexture(
				OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.HeightDerivedRAM"));

			AddCopyTexturePass(GraphBuilder, HeightTargets[WriteIndex], SourceH);

			FMixtormatBreakupFieldCS::FParameters* FP =
				GraphBuilder.AllocParameters<FMixtormatBreakupFieldCS::FParameters>();
			FP->OutputSize = Request.Resolution;
			FP->Seed = Breakup.BreakupSeed;
			FP->MacroCells = Breakup.BreakupMacroCells;
			FP->MidCells = Breakup.BreakupMidCells;
			FP->DetailCells = Breakup.BreakupDetailCells;
			FP->Density = Breakup.BreakupDensity;
			FP->SizeMin = Breakup.BreakupSizeMin;
			FP->SizeMax = Breakup.BreakupSizeMax;
			FP->Stretch = Breakup.BreakupStretch;
			FP->Angularity = Breakup.BreakupAngularity;
			FP->Jitter = Breakup.BreakupIrregularity;
			FP->OperationMid = Breakup.BreakupMidOperation;
			FP->OperationDetail = Breakup.BreakupDetailOperation;
			FP->BlendSmooth = Breakup.BreakupSmoothness;
			FP->DistortAmount = Breakup.BreakupDistortion;
			FP->DistortFrequency = Breakup.BreakupDistortionFrequency;
			FP->InvertField = Breakup.bBreakupInvert ? 1u : 0u;
			FP->HasRegionIds = PendingBreakup.RegionIds != nullptr ? 1u : 0u;
			FP->RegionIds = PendingBreakup.RegionIds ? PendingBreakup.RegionIds : Ctx.EmptyRegionIds;
			FP->OutputField = GraphBuilder.CreateUAV(Field);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Breakup.L%d.%d.Field", LayerIndex, BreakupIndex),
				FieldShader, FP, Groups);

			FMixtormatBreakupApplyCS::FParameters* AP =
				GraphBuilder.AllocParameters<FMixtormatBreakupApplyCS::FParameters>();
			AP->OutputSize = Request.Resolution;
			AP->Relief = Breakup.BreakupRelief;
			AP->FoldHeight = Breakup.BreakupFold;
			AP->FoldWidth = Breakup.BreakupFoldWidth;
			AP->CreaseWidth = Breakup.BreakupCreaseWidth;
			AP->CreaseDepth = Breakup.BreakupCrease;
			AP->PushAmount = Breakup.BreakupPush;
			AP->PushWidth = Breakup.BreakupPushWidth;
			AP->Variation = Breakup.BreakupVariation;
			AP->Strength = Breakup.BreakupAmount;
			AP->UsePlacementMask = bUsePlacementMask ? 1u : 0u;
			AP->PlacementMaskTiling = Breakup.BreakupMaskTiling;
			AP->InvertMask =
				!PendingBreakup.bHasScopedMask && Breakup.bBreakupInvertMask ? 1u : 0u;
			AP->SourceHeight = SourceH;
			AP->BreakupField = Field;
			AP->LayerMask = PendingBreakup.FeatureMask;
			AP->PlacementMaskTexture = PlacementMask;
			AP->LinearWrapSampler =
				TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
			AP->OutputHeight = GraphBuilder.CreateUAV(ResultH);
			AP->OutputCoverage = GraphBuilder.CreateUAV(Coverage);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Mixtormat.Breakup.L%d.%d.Apply", LayerIndex, BreakupIndex),
				ApplyShader, AP, Groups);

			AddHeightDerivedNormalPass(
				Ctx, SourceH, ResultH, OutputN[WriteIndex], OutputRAM[WriteIndex],
				ResultN, ResultRAM, Request.Resolution,
				HeightDerivedNormalStrength, 0.35f, true, TEXT("Breakup"));

			FRDGTextureRef FinalRAM = ResultRAM;
			if (Breakup.BreakupRoughnessAmount != 0.0f)
			{
				FRDGTextureRef ShadeRAM = GraphBuilder.CreateTexture(
					OutputRAM[WriteIndex]->Desc, TEXT("Mixtormat.Breakup.RoughnessRAM"));
				FMixtormatCarveShadeCS::FParameters* SP =
					GraphBuilder.AllocParameters<FMixtormatCarveShadeCS::FParameters>();
				SP->OutputSize = Request.Resolution;
				SP->RoughnessAmount = Breakup.BreakupRoughnessAmount;
				SP->CarveDepth = 1.0f;
				SP->UseCoverageTexture = 1u;
				SP->CoverageTexture = Coverage;
				SP->SourceHeight = SourceH;
				SP->CarvedHeight = ResultH;
				SP->SourceRAM = FinalRAM;
				SP->LinearWrapSampler =
					TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
				SP->OutputRAM = GraphBuilder.CreateUAV(ShadeRAM);
				FComputeShaderUtils::AddPass(
					GraphBuilder,
					RDG_EVENT_NAME("Mixtormat.Breakup.L%d.%d.Roughness", LayerIndex, BreakupIndex),
					CarveShadeShader, SP, Groups);
				FinalRAM = ShadeRAM;
			}

			AddCopyTexturePass(GraphBuilder, ResultH, HeightTargets[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, ResultN, OutputN[WriteIndex]);
			AddCopyTexturePass(GraphBuilder, FinalRAM, OutputRAM[WriteIndex]);
		}
	}'''

gpu = replace_function(gpu, "void AddChippingPasses(", add_breakup)

queue_breakup = r'''void QueuePendingBreakup(
		FMixtormatLayerPassContext& LayerCtx,
		const FLayerRenderData& Layer,
		const FChildRenderData& Child,
		const FEffectRenderData& Effect,
		FRDGTextureRef FeatureMask)
	{
		FPendingBreakup& Pending = LayerCtx.PendingBreakups.AddDefaulted_GetRef();
		Pending.Effect = &Effect;
		Pending.FeatureMask = FeatureMask;
		Pending.RegionIds = FindRegionIdsAbove(LayerCtx.RegionIdMaps, Child.SourceChildIndex);
		Pending.bHasScopedMask = Layer.Children.ContainsByPredicate(
			[&Child](const FChildRenderData& Candidate)
			{
				return Candidate.Type == EMixtormatLayerChildType::Mask
					&& Candidate.ScopeOwnerSourceChildIndex == Child.SourceChildIndex;
			});
	}'''

gpu = replace_function(gpu, "void QueuePendingChipping(", queue_breakup)
gpu = gpu.replace("before Chipping", "before Breakup")
gpu = gpu.replace("erosion and chipping", "erosion and Breakup")
write(gpu_rel, gpu)

h_rel = "Source/MixtormatEditor/Private/Widgets/SMixtormat.h"
h = req(h_rel).read_text(encoding="utf-8")
h = h.replace("GetSelectedChipping", "GetSelectedBreakup")
h = h.replace("AddChippingToLayer", "AddBreakupToLayer")
h = h.replace("BuildChippingControls", "BuildBreakupControls")
write(h_rel, h)

layers_rel = "Source/MixtormatEditor/Private/Widgets/SMixtormat_Layers.cpp"
layers = req(layers_rel).read_text(encoding="utf-8")
layers = layers.replace("GetSelectedChipping", "GetSelectedBreakup")
layers = layers.replace("AddChippingToLayer", "AddBreakupToLayer")
layers = layers.replace("EMixtormatEffectType::Chipping", "EMixtormatEffectType::Breakup")
layers = layers.replace("ChippingEffectName", "BreakupEffectName")
layers = layers.replace('"Chipping"', '"Breakup"')
layers = layers.replace("AddChipping", "AddBreakup")
write(layers_rel, layers)

badge_rel = "Source/MixtormatEditor/Private/UI/Layers/MixtormatLayerBadges.cpp"
badge = req(badge_rel).read_text(encoding="utf-8")
badge = badge.replace(
    'case EMixtormatEffectType::Chipping:  return LOCTEXT("EffectBadgeChipping", "CHIP");',
    'case EMixtormatEffectType::Breakup:   return LOCTEXT("EffectBadgeBreakup", "BREAK");'
)
write(badge_rel, badge)

insp_rel = "Source/MixtormatEditor/Private/Widgets/SMixtormat_Inspector.cpp"
insp = req(insp_rel).read_text(encoding="utf-8")

breakup_controls = r'''TSharedRef<SWidget> SMixtormat::BuildBreakupControls()
{
	const auto Breakup = [this]() { return GetSelectedBreakup(); };
	const auto Slider = [this, Breakup](
		const FText& Label, float FMixtormatLayerEffect::* Member,
		const double Min, const double Max, const double Default, const double Snap,
		const FText& Hint = FText::GetEmpty())
	{
		return MakeMemberSlider<FMixtormatLayerEffect>(
			Label, Breakup, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpShape", "Shape")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupScale", "Scale"), Breakup,
			&FMixtormatLayerEffect::BreakupScale, 1.0, 64.0, 6),
		Slider(LOCTEXT("BreakupDensity", "Density"), &FMixtormatLayerEffect::BreakupDensity,
			0.0, 1.0, 0.72, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupSize", "Size"), &FMixtormatLayerEffect::BreakupSize,
			0.05, 0.75, 0.32, 0.005),
		Slider(LOCTEXT("BreakupStretch", "Stretch"), &FMixtormatLayerEffect::BreakupStretch,
			1.0, 2.0, 1.6, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupAngularity", "Angularity"), &FMixtormatLayerEffect::BreakupAngularity,
			0.0, 1.0, 0.72, 0.01),
		Slider(LOCTEXT("BreakupIrregularity", "Irregularity"), &FMixtormatLayerEffect::BreakupIrregularity,
			0.0, 1.0, 0.38, 0.01)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpStructure", "Structure")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupRelief", "Relief"), &FMixtormatLayerEffect::BreakupRelief,
			-0.5, 0.5, -0.06, 0.0025,
			LOCTEXT("BreakupReliefHint", "Signed: negative carves/torns, positive raises rock or plates.")),
		Slider(LOCTEXT("BreakupFold", "Fold"), &FMixtormatLayerEffect::BreakupFold,
			0.0, 0.5, 0.025, 0.0025)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupCrease", "Crease"), &FMixtormatLayerEffect::BreakupCrease,
			0.0, 0.5, 0.018, 0.001),
		Slider(LOCTEXT("BreakupPush", "Push"), &FMixtormatLayerEffect::BreakupPush,
			-128.0, 128.0, 0.0, 0.25)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpVariation", "Variation")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupDetail", "Detail"), &FMixtormatLayerEffect::BreakupDetail,
			0.0, 1.0, 0.5, 0.01),
		Slider(LOCTEXT("BreakupDistortion", "Distortion"), &FMixtormatLayerEffect::BreakupDistortion,
			0.0, 32.0, 5.6, 0.1)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupVariation", "Variation"), &FMixtormatLayerEffect::BreakupVariation,
			0.0, 1.0, 0.25, 0.01),
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupSeed", "Seed"), Breakup,
			&FMixtormatLayerEffect::BreakupSeed, 0.0, 9999.0, 1)));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpAdvanced", "Advanced")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupSizeVariation", "Size Variation"), &FMixtormatLayerEffect::BreakupSizeVariation,
			0.0, 0.75, 0.3125, 0.01),
		Slider(LOCTEXT("BreakupSmoothness", "Smoothness"), &FMixtormatLayerEffect::BreakupSmoothness,
			0.0, 1.0, 0.30, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupDistortionFrequency", "Distort Freq"), Breakup,
			&FMixtormatLayerEffect::BreakupDistortionFrequency, 1.0, 16.0, 3),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("BreakupInvert", "Invert"), Breakup,
			&FMixtormatLayerEffect::bBreakupInvert)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupFoldWidth", "Fold Width"), &FMixtormatLayerEffect::BreakupFoldWidth,
			0.25, 128.0, 16.0, 0.25),
		Slider(LOCTEXT("BreakupCreaseWidth", "Crease Width"), &FMixtormatLayerEffect::BreakupCreaseWidth,
			0.25, 64.0, 1.25, 0.05)));
	AddSliderRow(Panel, Slider(
		LOCTEXT("BreakupPushWidth", "Push Width"), &FMixtormatLayerEffect::BreakupPushWidth,
		1.0, 256.0, 24.0, 0.5));

	AddSliderRow(Panel, MixtormatRow::MakeCaption(LOCTEXT("BreakupGrpOutput", "Output")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BreakupAmount", "Amount"), &FMixtormatLayerEffect::BreakupAmount,
			0.0, 1.0, 1.0, 0.01),
		Slider(LOCTEXT("BreakupRoughness", "Roughness"), &FMixtormatLayerEffect::BreakupRoughnessAmount,
			-1.0, 1.0, 0.0, 0.01)));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatLayerEffect>(
			LOCTEXT("BreakupMaskTiling", "Mask Tiling"), Breakup,
			&FMixtormatLayerEffect::BreakupMaskTiling, 1.0, 16.0, 1),
		MakeMemberToggle<FMixtormatLayerEffect>(
			LOCTEXT("BreakupMaskInvert", "Invert Mask"), Breakup,
			&FMixtormatLayerEffect::bBreakupInvertMask)));

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedBreakup() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("BreakupHeading", "BREAKUP"))
			.InitiallyExpanded(true)
			[
				Panel
			]
		];
}'''

insp = replace_function(insp, "TSharedRef<SWidget> SMixtormat::BuildChippingControls()", breakup_controls)
insp = insp.replace("BuildChippingControls()", "BuildBreakupControls()")
write(insp_rel, insp)

tests_rel = "Source/MixtormatEditor/Private/Tests/MixtormatCompositorTests.cpp"
tests = req(tests_rel).read_text(encoding="utf-8")
start = tests.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(\n\tFMixtormatChippingIdentityTest,")
end = tests.find("IMPLEMENT_SIMPLE_AUTOMATION_TEST(\n\tFMixtormatBlurScopeTest,", start)
if start < 0 or end < 0:
    raise RuntimeError("Old Chipping test block not found")

breakup_test = r'''IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMixtormatBreakupIdentityTest,
	"Mixtormat.Compositor.BreakupIdentity",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter
		| EAutomationTestFlags::NonNullRHI)

bool FMixtormatBreakupIdentityTest::RunTest(const FString& Parameters)
{
	using namespace MixtormatCompositorTests;
	(void)Parameters;

	FMixtormatGpuCompositor Compositor;
	if (!TestTrue(TEXT("Compositor initialises"),
		Compositor.Initialize(FIntPoint(TestResolution, TestResolution))))
		return false;

	UTexture2D* WhiteMask = LoadObject<UTexture2D>(
		nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	if (!TestNotNull(TEXT("Breakup placement mask exists"), WhiteMask))
		return false;

	FMixtormatLayer Layer;
	Layer.Type = EMixtormatLayerType::Fill;

	FMixtormatLayerChild BreakupChild;
	BreakupChild.Type = EMixtormatLayerChildType::Effect;
	BreakupChild.Effect.ProceduralType = EMixtormatEffectType::Breakup;
	BreakupChild.Effect.BreakupAmount = 0.0f;
	BreakupChild.Effect.BreakupRelief = -0.15f;
	BreakupChild.Effect.BreakupFold = 0.05f;
	BreakupChild.Effect.BreakupCrease = 0.02f;
	BreakupChild.Effect.BreakupMaskTexture =
		TSoftObjectPtr<UTexture2D>(FSoftObjectPath(WhiteMask));
	Layer.Children.Add(BreakupChild);

	TArray<FMixtormatLayer> Layers;
	Layers.Add(Layer);

	if (!TestTrue(TEXT("Breakup at zero composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ZeroHeight;
	if (!TestTrue(TEXT("Breakup zero height reads"),
		ReadHeight(Compositor.GetHeightOutput(), ZeroHeight)))
		return false;

	Layers[0].Children.RemoveAt(0);
	if (!TestTrue(TEXT("Reference composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ReferenceHeight;
	TArray<FLinearColor> ReferenceBaseColor;
	if (!ReadHeight(Compositor.GetHeightOutput(), ReferenceHeight)
		|| !ReadTarget(Compositor.GetBaseColorOutput(), ReferenceBaseColor))
		return false;

	int32 IdentityDifferences = 0;
	for (int32 Index = 0; Index < ZeroHeight.Num(); ++Index)
		IdentityDifferences += ZeroHeight[Index].R == ReferenceHeight[Index].R ? 0 : 1;
	TestEqual(TEXT("Breakup Amount 0 is exact identity"), IdentityDifferences, 0);

	BreakupChild.Effect.BreakupAmount = 1.0f;
	Layers[0].Children.Add(BreakupChild);
	if (!TestTrue(TEXT("Active Breakup composes"),
		ComposeAndWait(Compositor, Layers, FMixtormatDebugPreviewSettings())))
		return false;

	TArray<FLinearColor> ActiveHeight;
	TArray<FLinearColor> ActiveBaseColor;
	if (!ReadHeight(Compositor.GetHeightOutput(), ActiveHeight)
		|| !ReadTarget(Compositor.GetBaseColorOutput(), ActiveBaseColor))
		return false;

	int32 HeightChanges = 0;
	int32 BaseColorChanges = 0;
	for (int32 Index = 0; Index < ActiveHeight.Num(); ++Index)
	{
		HeightChanges += FMath::Abs(ActiveHeight[Index].R - ReferenceHeight[Index].R) > 1.0e-4f ? 1 : 0;
		BaseColorChanges += ActiveBaseColor[Index].Equals(ReferenceBaseColor[Index], 1.0e-5f) ? 0 : 1;
	}
	TestTrue(TEXT("Active Breakup changes height"), HeightChanges > 0);
	TestEqual(TEXT("Breakup does not author base color"), BaseColorChanges, 0);

	return true;
}

'''
tests = tests[:start] + breakup_test + tests[end:]
write(tests_rel, tests)

config = ROOT / "Config/DefaultMixtormat.ini"
redirect = '+EnumRedirects=(OldName="/Script/MixtormatRuntime.EMixtormatEffectType",ValueChanges=(("Chipping","Breakup")))'
if config.exists():
    backup(config)
    cfg = config.read_text(encoding="utf-8")
    if redirect not in cfg:
        if "[CoreRedirects]" not in cfg:
            cfg += "\n[CoreRedirects]\n"
        cfg += redirect + "\n"
        config.write_text(cfg, encoding="utf-8", newline="\n")
else:
    config.parent.mkdir(parents=True, exist_ok=True)
    config.write_text("[CoreRedirects]\n" + redirect + "\n", encoding="utf-8", newline="\n")
print("updated Config/DefaultMixtormat.ini")

for rel in (
    "Shaders/Private/MixtormatChipping.usf",
    "Shaders/Private/MixtormatReduceMinMax.usf",
):
    p = ROOT / rel
    if p.exists():
        backup(p)
        p.unlink()
        print("deleted", rel)

print("\nApplied. Build UE 5.8 Development Win64/SM6 next.")
