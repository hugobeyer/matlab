// Copyright 2026 Hugo Beyer. All Rights Reserved.

#include "Widgets/SMixtormat.h"
#include "Widgets/SMixtormatInternal.h"
#include "MixtormatLayerGroups.h"
#include "Style/MixtormatDesignTokens.h"
#include "UI/Menus/MixtormatMenuBuilder.h"
#include "UI/Atoms/SMixtormatChip.h"
#include "UI/Containers/SMixtormatInspectorCard.h"
#include "UI/Layers/SMixtormatLayerIcon.h"
#include "UI/Rows/SMixtormatRow.h"
#include "Widgets/Input/SSpinBox.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UnrealClient.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "SMixtormat"

extern const EMixtormatUVRotation GMixtormatUVRotations[4];

// The Exact ID picker's click surface: the Region IDs preview drawn at a fixed square size, with
// the whole image as one target. Local position over local size is the UV, which is the entire
// reason the picker is a flat image rather than a click in the 3D viewport -- recovering a UV
// from a mesh hit needs a project-wide setting this plugin cannot guarantee, and fails silently
// where it is off.
class SMixtormatRegionIdPickSurface final : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnPicked, FVector2D);

	SLATE_BEGIN_ARGS(SMixtormatRegionIdPickSurface) {}
		SLATE_ARGUMENT(TSharedPtr<FSlateBrush>, Brush)
		SLATE_EVENT(FOnPicked, OnPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnPicked = InArgs._OnPicked;
		ChildSlot
		[
			SNew(SImage).Image(InArgs._Brush.IsValid() ? InArgs._Brush.Get() : nullptr)
		];
	}

	virtual FReply OnMouseButtonDown(
		const FGeometry& MyGeometry,
		const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
		{
			return FReply::Unhandled();
		}
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D Size = MyGeometry.GetLocalSize();
		if (Size.X <= 0.0 || Size.Y <= 0.0)
		{
			return FReply::Unhandled();
		}
		OnPicked.ExecuteIfBound(FVector2D(
			FMath::Clamp(Local.X / Size.X, 0.0, 1.0),
			FMath::Clamp(Local.Y / Size.Y, 0.0, 1.0)));
		return FReply::Handled();
	}

private:
	FOnPicked OnPicked;
};

TSharedRef<SWidget> SMixtormat::BuildRampIdBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	// The set Ramp From IDs offers, which is deliberately not the whole enum: Replace would
	// discard the height under the region rather than meeting it, which is never what a ramp is
	// for.
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::Overlay,
		EMixtormatMaskBlendMode::Difference,
		EMixtormatMaskBlendMode::Exclusion
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->BlendMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->BlendMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdRotationMenu()
{
	MixtormatMenu::FBuilder Menu;
	for (const EMixtormatUVRotation Rotation : GMixtormatUVRotations)
	{
		Menu.Item(
			MixtormatUI::UVRotationText(Rotation),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Rotation]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->Rotation = Rotation;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Rotation]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->Rotation == Rotation;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatColorIdMode Modes[] = {
		EMixtormatColorIdMode::ExactId,
		EMixtormatColorIdMode::ColorRange,
	};
	for (const EMixtormatColorIdMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::ColorIdModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatColorIdMask* C = GetSelectedColorId())
				{
					C->Mode = Mode;
					RefreshLayeredPreview();
					// The row's own kind text does not move, but the inspector swaps a whole
					// section either way, and the preview has to be asked for again.
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C && C->Mode == Mode;
			}));
	}
	return Menu.Build();
}

bool SMixtormat::CanPickRegionId() const
{
	// The pick buffer is written only while a Region IDs preview is being composited, and cleared
	// to the sentinel otherwise, so offering the picker at any other time would hand the artist a
	// black square and a value of -1. The message the popover shows instead names the eye to turn
	// on, which is the actual next step.
	return DebugPreviewMode == EMixtormatDebugPreviewMode::ChildOutput
		&& ChildPreviewTarget.Kind == EMixtormatPreviewOutputKind::RegionIds
		&& ChildPreviewTarget.IsValid()
		&& !PreviewViewports.IsEmpty()
		&& PreviewViewports[0].IsValid();
}

bool SMixtormat::ReadRegionIdAtUV(const FVector2D UV, int32& OutRegionId) const
{
	if (PreviewViewports.IsEmpty() || !PreviewViewports[0].IsValid())
	{
		return false;
	}
	UTextureRenderTarget2D* Pick = PreviewViewports[0]->GetRegionIdPick();
	FTextureRenderTargetResource* Resource =
		Pick ? Pick->GameThread_GetRenderTargetResource() : nullptr;
	if (!Resource)
	{
		return false;
	}

	const int32 X = FMath::Clamp(
		FMath::FloorToInt(UV.X * static_cast<double>(Pick->SizeX)), 0, Pick->SizeX - 1);
	const int32 Y = FMath::Clamp(
		FMath::FloorToInt(UV.Y * static_cast<double>(Pick->SizeY)), 0, Pick->SizeY - 1);

	// One texel, read as FLinearColor: the target is PF_R32_FLOAT, so the red channel comes back
	// as the exact float the pick pass wrote. ReadPixels would quantise it to 8 bits, which would
	// turn every id into the same handful of values.
	TArray<FLinearColor> Pixels;
	if (!Resource->ReadLinearColorPixels(
		Pixels, FReadSurfaceDataFlags(), FIntRect(X, Y, X + 1, Y + 1))
		|| Pixels.IsEmpty())
	{
		return false;
	}

	const float Raw = Pixels[0].R;
	if (Raw < 0.0f)
	{
		// The no-region sentinel. Grout, or a pixel outside every region -- there is no id to
		// take, so the field keeps whatever it had.
		return false;
	}
	OutRegionId = FMath::RoundToInt(Raw);
	return true;
}

void SMixtormat::PickRegionIdAtUV(const FVector2D UV)
{
	int32 PickedId = 0;
	if (!ReadRegionIdAtUV(UV, PickedId))
	{
		return;
	}
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		C->ExactRegionId = PickedId;
		RefreshLayeredPreview();
	}
}

TSharedRef<SWidget> SMixtormat::BuildRegionIdPickerPopup()
{
	if (!CanPickRegionId())
	{
		return SNew(SBox)
			.Padding(MixtormatTokens::TileGap)
			.WidthOverride(MixtormatTokens::MaskPickerWidth * 0.5f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("RegionIdPickerUnavailable",
					"Turn on the Region IDs preview on the Pattern IDs, Cluster IDs or ID Group "
					"node above this mask -- the eye in its inspector header -- then open this "
					"again. The picker reads the ID map that preview is built from."))
			];
	}

	// The composite's own debug target: exactly the pixels the viewport is showing, so the region
	// clicked here is the region seen there. The brush is held on the panel rather than rebuilt
	// per open, because a brush pointing at a render target has to outlive the widget drawing it.
	if (!RegionIdPreviewBrush.IsValid())
	{
		RegionIdPreviewBrush = MakeShared<FSlateBrush>();
	}
	UTextureRenderTarget2D* DebugTarget = PreviewViewports[0]->GetCompositedDebug();
	RegionIdPreviewBrush->SetResourceObject(DebugTarget);
	RegionIdPreviewBrush->ImageSize = FVector2D(
		MixtormatTokens::MaskPickerWidth, MixtormatTokens::MaskPickerWidth);

	const float ViewSize = MixtormatTokens::MaskPickerWidth;
	return SNew(SBox)
		.Padding(MixtormatTokens::TileGap)
		.WidthOverride(ViewSize)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("RegionIdPickerHint",
					"Click a region to take its ID. Anywhere with no region -- grout, or a gap -- "
					"leaves the current ID alone, and so does closing this without clicking."))
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox)
				.WidthOverride(ViewSize)
				.HeightOverride(ViewSize)
				[
					// The whole image is one click target. Local position over local size is the
					// UV directly -- the debug target is the composition, square and unrotated
					// relative to itself, and the pick buffer went through the same quarter turn
					// the debug view did, so the two are always in the same frame.
					SNew(SMixtormatRegionIdPickSurface)
					.Brush(RegionIdPreviewBrush)
					.OnPicked_Lambda([this](const FVector2D UV)
					{
						PickRegionIdAtUV(UV);
						FSlateApplication::Get().DismissAllMenus();
					})
				]
			]
		];
}

FReply SMixtormat::AddColorIdEntry()
{
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		if (C->Colors.Num() < FMixtormatColorIdMask::MaxColors)
		{
			// A new entry is white rather than a copy of the last. Duplicating the last colour
			// would add a row that selects exactly what is already selected, which reads as the
			// button having done nothing.
			C->Colors.Add(FLinearColor::White);
			RefreshLayeredPreview();
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveColorIdEntry(const int32 ColorIndex)
{
	if (FMixtormatColorIdMask* C = GetSelectedColorId())
	{
		if (C->Colors.IsValidIndex(ColorIndex))
		{
			C->Colors.RemoveAt(ColorIndex);
			RefreshLayeredPreview();
		}
	}
	return FReply::Handled();
}

void SMixtormat::SetColorIdColor(
	const FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex,
	const int32 ColorIndex)
{
	// Resolved by index rather than through GetSelectedColorId, because the picker is modeless:
	// the selection can move while it is open, and committing to whatever happens to be selected
	// when the user drags a swatch would edit a different node from the one they opened.
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	if (Child.Type != EMixtormatLayerChildType::ColorId
		|| !Child.ColorId.Colors.IsValidIndex(ColorIndex))
	{
		return;
	}
	Child.ColorId.Colors[ColorIndex] = NewColor;
	RefreshLayeredPreview();
}

FReply SMixtormat::OpenColorIdPicker(const int32 ColorIndex)
{
	const FMixtormatColorIdMask* Selected = GetSelectedColorId();
	if (!Selected || !Selected->Colors.IsValidIndex(ColorIndex))
	{
		return FReply::Handled();
	}

	const int32 LayerIndex = SelectedLayerIndex;
	const int32 ChildIndex = SelectedMaskIndex;
	const FLinearColor OriginalColor = Selected->Colors[ColorIndex];

	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = OriginalColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex](const FLinearColor NewColor)
		{
			SetColorIdColor(NewColor, LayerIndex, ChildIndex, ColorIndex);
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex, OriginalColor](const FLinearColor)
		{
			SetColorIdColor(OriginalColor, LayerIndex, ChildIndex, ColorIndex);
		});
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildColorIdControls()
{
	const auto Id = [this]() { return GetSelectedColorId(); };

	const auto Slider = [this, Id](
		const FText& Label,
		float FMixtormatColorIdMask::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatColorIdMask>(Label, Id, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// SELECTION first, because it decides what the rest of this panel is even about.
	//
	// Exact ID compares the integer Region ID published by the nearest ID node above this mask --
	// Pattern IDs, Cluster IDs or ID Group -- and is the mode to reach for when the regions
	// were generated in this stack. Color Range samples an authored ID map and accepts whatever
	// lands within Threshold of a chosen colour, which is the mode for a map that arrived with the
	// mesh. The two share nothing but the blend and shaping tail, so each hides the other's rows
	// rather than leaving half the panel inert.
	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("IdGrpSelection", "Selection"), nullptr,
			SNew(SMixtormatLayerIcon)
				.Size(MixtormatTokens::GroupCardLeadingIconSize).bVisibility(true)
				.bOn_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::LayerMask; })
				.bActive_Lambda([this]() { return DebugPreviewMode == EMixtormatDebugPreviewMode::LayerMask; })
				.ToolTipText(LOCTEXT("PreviewColorId", "Preview this selection in unlit dark red and cyan"))
				.OnClicked_Lambda([this]() { ToggleFeaturePreview(EMixtormatDebugPreviewMode::LayerMask); }));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("IdSelectionMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::ColorIdModeText(C->Mode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdModeMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("IdSelectionModeHint",
			"Exact ID selects one discrete Region ID from the ID node above this mask, as an "
			"integer comparison -- so the selection is unaffected by whatever colour the Region "
			"IDs preview happens to paint that region, and unaffected by re-seeding the preview. "
			"Color Range compares the sampled colour of an authored ID map against a chosen "
			"colour, within Threshold and feathered by Width.")));

	// Exact ID only. A numeric entry rather than a slider: Region IDs are pixel indices, so the
	// useful range runs to the square of the composition resolution and no slider can address it
	// meaningfully. Turn on the Region IDs preview eye on the node above to see which regions
	// exist while you set this.
	AddSliderRow(Panel, SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Mode == EMixtormatColorIdMode::ExactId
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::Make(
				LOCTEXT("IdExactRegion", "Region ID"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, MixtormatTokens::TileGap, 0.0f)
				[
					// The eyedropper. Opens the live Region IDs preview and takes the integer id
					// of whatever is clicked -- never a colour, and never a guess at which id a
					// colour came from.
					SNew(SMixtormatChip)
					.Text(LOCTEXT("IdExactPick", "Pick"))
					.ToolTip(LOCTEXT("IdExactPickHint",
						"Click a region in the Region IDs preview to take its ID. Needs that "
						"preview turned on, from the eye in the inspector header of the ID node "
						"above this mask. Closing the popover without clicking changes nothing."))
					.OnGetMenuContent(
						FOnGetContent::CreateSP(this, &SMixtormat::BuildRegionIdPickerPopup))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
				SNew(SBox)
				.WidthOverride(MixtormatTokens::RowFieldMinWidth)
				[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.MinSliderValue(0)
				.MaxSliderValue(4096)
				.Delta(1)
				.Value_Lambda([this]()
				{
					const FMixtormatColorIdMask* C = GetSelectedColorId();
					return C ? C->ExactRegionId : 0;
				})
				.OnValueChanged_Lambda([this](const int32 NewValue)
				{
					if (FMixtormatColorIdMask* C = GetSelectedColorId())
					{
						C->ExactRegionId = FMath::Max(NewValue, 0);
						RefreshLayeredPreview();
					}
				})
				]
				],
				LOCTEXT("IdExactRegionHint",
					"The Region ID to select. Compared as an integer against the map published by "
					"the nearest ID node above -- never reconstructed from a preview colour, so it "
					"survives a change of seed or of preview palette. A mask in this mode with no "
					"ID node above it is skipped rather than blended, the same as Random From IDs."))
		]);

	// Everything from here to the end of Placement belongs to Color Range. An Exact ID mask has
	// no map to pick, no colours to list and nothing to place: it reads the Region IDs at the
	// composition's own resolution, where a UV transform would interpolate labels.
	TSharedRef<SVerticalBox> Range = SNew(SVerticalBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Mode == EMixtormatColorIdMode::ExactId
				? EVisibility::Collapsed
				: EVisibility::Visible;
		});

	// The map. Any Texture2D rather than the library gallery the other mask slots offer: an ID
	// map arrives with the mesh from whatever built it, and it is not a Mixtormat asset and never
	// will be.
	const TSharedRef<SVerticalBox> RangeCards = Range;
		Range = AddCard(RangeCards, LOCTEXT("IdGrpSource", "ID Map"));
	Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SObjectPropertyEntryBox)
		.AllowedClass(UTexture2D::StaticClass())
		.DisplayThumbnail(false)
		.AllowClear(true)
		.ToolTipText(LOCTEXT("IdTextureHint", "The ID map. Import it with sRGB off and compression set to an uncompressed format: both settings move the colours the map stores, and a selection is a comparison against a colour picked out of them. DXT in particular invents intermediate values along every ID boundary."))
		.ObjectPath_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C ? C->IdTexture.ToSoftObjectPath().ToString() : FString();
		})
		.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
		{
			if (FMixtormatColorIdMask* C = GetSelectedColorId())
			{
				C->IdTexture = TSoftObjectPtr<UTexture2D>(AssetData.ToSoftObjectPath());
				RefreshLayeredPreview();
			}
		})
	];

	// The selection. One row per colour: a swatch that opens a picker, and a button that drops
	// it. Rebuilt rather than bound, because the row count is the data here -- adding an ID adds
	// a widget, which no attribute can express.
	Range = AddCard(RangeCards, LOCTEXT("IdGrpColors", "Selected IDs"));

	// Every row that could exist is laid out once and shows itself when the selection reaches it.
	// The panel is built at construction, long before anything is selected, so a loop over the
	// current entries would bake in whatever the count happened to be then -- which is zero.
	for (int32 ColorIndex = 0; ColorIndex < FMixtormatColorIdMask::MaxColors; ++ColorIndex)
	{
		{
			Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([this, ColorIndex]()
				{
					const FMixtormatColorIdMask* C = GetSelectedColorId();
					return C && C->Colors.IsValidIndex(ColorIndex)
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.ToolTipText(LOCTEXT("IdSwatchHint", "The colour to select. Pick it out of the ID map with the eyedropper in the colour window."))
					.OnClicked_Lambda([this, ColorIndex]() { return OpenColorIdPicker(ColorIndex); })
					[
						SNew(SColorBlock)
						.Color_Lambda([this, ColorIndex]()
						{
							const FMixtormatColorIdMask* C = GetSelectedColorId();
							return C && C->Colors.IsValidIndex(ColorIndex)
								? C->Colors[ColorIndex]
								: FLinearColor::Black;
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				.Padding(MixtormatTokens::TileGap, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
					.Text(LOCTEXT("IdRemoveColor", "Remove"))
					.OnClicked_Lambda([this, ColorIndex]() { return RemoveColorIdEntry(ColorIndex); })
				]
			];
		}
	}

	Range->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.Text(LOCTEXT("IdAddColor", "Add ID"))
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatColorIdMask* C = GetSelectedColorId();
			return C && C->Colors.Num() < FMixtormatColorIdMask::MaxColors;
		})
		.OnClicked_Lambda([this]() { return AddColorIdEntry(); })
	];

	AddSliderRow(Range, MixtormatRow::MakePair(
		// Threshold and Width, not Tolerance and Softness. The fields keep their serialised names
		// -- nothing saved moves -- but the labels say what they do: one sets where acceptance
		// cuts off, the other how wide the transition around it is.
		Slider(LOCTEXT("IdThreshold", "Threshold"), &FMixtormatColorIdMask::Tolerance, 0.0, 1.0, 0.10, 0.001,
			LOCTEXT("IdThresholdHint", "How far from a selected colour still counts, as a distance in RGB. The diagonal of the colour cube is about 1.73, so this is small by nature: the default admits the wobble a compressed map leaves across a flat region without reaching a neighbouring ID. Raise it until the part fills in; if it starts claiming its neighbours, the map wants a cleaner import rather than a wider tolerance.")),
		Slider(LOCTEXT("IdWidth", "Width"), &FMixtormatColorIdMask::Softness, 0.0, 0.5, 0.02, 0.001,
			LOCTEXT("IdWidthHint", "Width of the transition either side of Threshold. The map is point sampled -- the average of two IDs is a third colour that names neither -- so the selection edge is a hard texel boundary, and this is what feathers it."))));

	Range = AddCard(RangeCards, LOCTEXT("IdGrpPlacement", "Placement"));
	AddSliderRow(Range, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingX", "Tiling X"), Id, &FMixtormatColorIdMask::TilingX, 1.0, 16.0, 1,
			LOCTEXT("IdTilingXHint", "Integer only. A fractional scale lands mid-texel at the UV wrap and seams.")),
		MakeMemberSliderInt<FMixtormatColorIdMask>(
			LOCTEXT("IdTilingY", "Tiling Y"), Id, &FMixtormatColorIdMask::TilingY, 1.0, 16.0, 1,
			LOCTEXT("IdTilingYHint", "Integer only, for the same reason as Tiling X."))));
	AddSliderRow(Range, MixtormatRow::MakePair(
		Slider(LOCTEXT("IdOffsetU", "Offset U"), &FMixtormatColorIdMask::UVOffsetX, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetUHint", "Moves where the map is read from, in UV. Unrelated to Offset under Blend, which lifts the mask value instead.")),
		Slider(LOCTEXT("IdOffsetV", "Offset V"), &FMixtormatColorIdMask::UVOffsetY, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("IdOffsetVHint", "Moves where the map is read from, in UV."))));
	AddSliderRow(Range, MixtormatRow::MakePair(
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipU", "Flip U"), Id, &FMixtormatColorIdMask::bFlipU,
			LOCTEXT("IdFlipUHint", "Mirrors the map horizontally before it is tiled.")),
		MakeMemberToggle<FMixtormatColorIdMask>(
			LOCTEXT("IdFlipV", "Flip V"), Id, &FMixtormatColorIdMask::bFlipV,
			LOCTEXT("IdFlipVHint", "Mirrors the map vertically before it is tiled. The usual fix when a map was authored under the other texture-coordinate convention."))));
	AddSliderRow(Range, MixtormatRow::MakeDropdown(
		LOCTEXT("IdRotation", "Rotation"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::UVRotationText(C->Rotation) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdRotationMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("IdRotationHint", "Quarter turns only. An arbitrary angle drags the corners of the tile outside the wrapped domain and seams.")));

	AddSliderRow(Cards, RangeCards);

	Panel = AddCard(Cards, LOCTEXT("IdGrpBlend", "Blend"));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("IdBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatColorIdMask* C = GetSelectedColorId();
				return C ? MixtormatUI::MaskBlendModeText(C->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildColorIdBlendModeMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("IdBlendModeHint", "How the selection combines with the mask accumulated above it in this layer. Max is what unions two ID nodes; Multiply is what intersects one with a painted mask.")));
	AddSliderRow(Panel, Slider(LOCTEXT("IdWeight", "Weight"), &FMixtormatColorIdMask::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("IdWeightHint", "How far the blend is taken. 0 is the off switch for this node, and it costs nothing -- the pass is skipped rather than run to reproduce its input.")));
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatColorIdMask* ColorId = GetSelectedColorId();
		return ColorId ? &ColorId->Shaping : nullptr;
	});
	Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedColorId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ColorIdHeading", "COLOR ID"))
			.InitiallyExpanded(true)

			[
				Panel
			]
		];
}

void SMixtormat::SetHsvPaletteColor(
	const FLinearColor NewColor,
	const int32 LayerIndex,
	const int32 ChildIndex,
	const int32 ColorIndex)
{
	if (!ResolveChild(LayerIndex, ChildIndex))
	{
		return;
	}
	FMixtormatLayerChild& Child = *ResolveChild(LayerIndex, ChildIndex);
	if (Child.Type != EMixtormatLayerChildType::HsvFilter
		|| !Child.HsvFilter.Palette.IsValidIndex(ColorIndex))
	{
		return;
	}
	Child.HsvFilter.Palette[ColorIndex] = NewColor;
	RefreshLayeredPreview();
}

FReply SMixtormat::OpenHsvPalettePicker(const int32 ColorIndex)
{
	const FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter();
	if (!Selected || !Selected->Palette.IsValidIndex(ColorIndex))
	{
		return FReply::Handled();
	}

	const int32 LayerIndex = SelectedLayerIndex;
	const int32 ChildIndex = SelectedMaskIndex;
	const FLinearColor OriginalColor = Selected->Palette[ColorIndex];

	FColorPickerArgs PickerArgs;
	PickerArgs.bIsModal = false;
	PickerArgs.bUseAlpha = false;
	PickerArgs.ParentWidget = SharedThis(this);
	PickerArgs.InitialColor = OriginalColor;
	PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex](const FLinearColor NewColor)
		{
			SetHsvPaletteColor(NewColor, LayerIndex, ChildIndex, ColorIndex);
		});
	PickerArgs.OnColorPickerCancelled = FOnColorPickerCancelled::CreateLambda(
		[this, LayerIndex, ChildIndex, ColorIndex, OriginalColor](const FLinearColor)
		{
			SetHsvPaletteColor(OriginalColor, LayerIndex, ChildIndex, ColorIndex);
		});
	OpenColorPicker(PickerArgs);
	return FReply::Handled();
}

FReply SMixtormat::AddHsvPaletteEntry()
{
	if (FMixtormatHsvIdFilter* Hsv = GetSelectedHsvFilter())
	{
		if (Hsv->Palette.Num() < FMixtormatHsvIdFilter::MaxPaletteColors)
		{
			// Seeded from the last entry rather than from black, so adding a stop extends a ramp
			// the artist is already building instead of dropping a hole in the middle of it.
			//
			// Copied to a local first, and it has to be: Add can reallocate, and TArray asserts
			// outright on being handed a reference into the container it is growing.
			const FLinearColor Seeded =
				Hsv->Palette.IsEmpty() ? FLinearColor::White : Hsv->Palette.Last();
			Hsv->Palette.Add(Seeded);
			RefreshLayeredPreview();
			RebuildLayerList();
		}
	}
	return FReply::Handled();
}

FReply SMixtormat::RemoveHsvPaletteEntry(const int32 ColorIndex)
{
	if (FMixtormatHsvIdFilter* Hsv = GetSelectedHsvFilter())
	{
		if (Hsv->Palette.IsValidIndex(ColorIndex))
		{
			Hsv->Palette.RemoveAt(ColorIndex);
			RefreshLayeredPreview();
			RebuildLayerList();
		}
	}
	return FReply::Handled();
}

TSharedRef<SWidget> SMixtormat::BuildHsvFilterControls()
{
	const auto Hsv = [this]() { return GetSelectedHsvFilter(); };

	const auto Slider = [this, Hsv](
		const FText& Label,
		float FMixtormatHsvIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatHsvIdFilter>(Label, Hsv, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// The palette, and it is the half that matters. Jitter alone can only wander from wherever
	// the texture already sits; a palette lets the variation be aimed at colours that were
	// chosen. Same row shape as the colour ID selection, for the same reason: a colour is picked
	// by looking at it.
	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("HsvGrpPalette", "Palette"));

	// Every row that could exist is laid out once and shows itself when the data reaches it. The
	// panel is built at construction, long before anything is selected, so a loop over the
	// current entries would bake in whatever the count happened to be then -- which is zero.
	for (int32 ColorIndex = 0; ColorIndex < FMixtormatHsvIdFilter::MaxPaletteColors; ++ColorIndex)
	{
		Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
		[
			SNew(SHorizontalBox)
			.Visibility_Lambda([this, ColorIndex]()
			{
				const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
				return H && H->Palette.IsValidIndex(ColorIndex)
					? EVisibility::Visible : EVisibility::Collapsed;
			})
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.ToolTipText(LOCTEXT("HsvSwatchHint", "A stop on the palette regions draw from. Stops are evenly spaced and a region can land anywhere between two of them."))
				.OnClicked_Lambda([this, ColorIndex]() { return OpenHsvPalettePicker(ColorIndex); })
				[
					SNew(SColorBlock)
					.Color_Lambda([this, ColorIndex]()
					{
						const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
						return H && H->Palette.IsValidIndex(ColorIndex)
							? H->Palette[ColorIndex]
							: FLinearColor::Black;
					})
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			.Padding(MixtormatTokens::TileGap, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
				.Text(LOCTEXT("HsvRemoveColor", "Remove"))
				.OnClicked_Lambda([this, ColorIndex]() { return RemoveHsvPaletteEntry(ColorIndex); })
			]
		];
	}

	Panel->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, MixtormatTokens::TileGap)
	[
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.Text(LOCTEXT("HsvAddColor", "Add Colour"))
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatHsvIdFilter* H = GetSelectedHsvFilter();
			return H && H->Palette.Num() < FMixtormatHsvIdFilter::MaxPaletteColors;
		})
		.OnClicked_Lambda([this]() { return AddHsvPaletteEntry(); })
	];

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvMixMin", "Tint Min"), &FMixtormatHsvIdFilter::RampMixMin, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("HsvMixMinHint", "How far a region tints toward the colour it sampled, at the low end. A range rather than one number so most regions can sit near the texture's own colour with a few pulled much further.")),
		Slider(LOCTEXT("HsvMixMax", "Tint Max"), &FMixtormatHsvIdFilter::RampMixMax, 0.0, 1.0, 0.15, 0.01,
			LOCTEXT("HsvMixMaxHint", "The high end of the same range. At 1 a region takes the palette colour outright and loses the texture's own; the useful territory is well below that."))));

	// Min/max pairs rather than a +/- amount, so variation can be biased: hue 0 to 0.1 shifts
	// only warm, which a symmetric amount cannot express.
	Panel = AddCard(Cards, LOCTEXT("HsvGrpJitter", "Jitter"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvHueMin", "Hue Min"), &FMixtormatHsvIdFilter::HueMin, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("HsvHueMinHint", "Added, because hue is circular. -1 to 1 spans a full turn either way, the same convention as the layer's own Hue Shift -- so a couple of hundredths is already clearly visible on a flat surface. Both ends at 0 switches hue jitter off.")),
		Slider(LOCTEXT("HsvHueMax", "Hue Max"), &FMixtormatHsvIdFilter::HueMax, -1.0, 1.0, 0.0, 0.005,
			LOCTEXT("HsvHueMaxHint", "The other end of the hue range. Set both positive to shift only warm, both negative to shift only cool."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvSatMin", "Sat Min"), &FMixtormatHsvIdFilter::SaturationMin, 0.0, 2.0, 0.9, 0.01,
			LOCTEXT("HsvSatMinHint", "A multiplier around 1, because saturation is a magnitude rather than a position on a circle. 1 on both ends leaves saturation alone.")),
		Slider(LOCTEXT("HsvSatMax", "Sat Max"), &FMixtormatHsvIdFilter::SaturationMax, 0.0, 2.0, 1.1, 0.01,
			LOCTEXT("HsvSatMaxHint", "The high end of the saturation multiplier."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("HsvValMin", "Value Min"), &FMixtormatHsvIdFilter::ValueMin, 0.0, 2.0, 0.9, 0.01,
			LOCTEXT("HsvValMinHint", "A multiplier around 1, like saturation. This is the one that reads as regions being fired differently, and the one to reach for first.")),
		Slider(LOCTEXT("HsvValMax", "Value Max"), &FMixtormatHsvIdFilter::ValueMax, 0.0, 2.0, 1.1, 0.01,
			LOCTEXT("HsvValMaxHint", "The high end of the value multiplier."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatHsvIdFilter>(
		LOCTEXT("HsvSeed", "Seed"), Hsv, &FMixtormatHsvIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("HsvSeedHint", "Reshuffles which region gets which colour without changing any of the ranges. All five draws -- palette position, tint amount, hue, saturation, value -- come off one hash of this and the region ID, so they move together.")));
			Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedHsvFilter() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("HsvFilterHeading", "HSV FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatHsvIdFilter* Selected = GetSelectedHsvFilter())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("HsvEnabledHint", "Enable this HSV filter")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildPatternModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatPatternMode Modes[] = {
		EMixtormatPatternMode::Grid,
		EMixtormatPatternMode::RunningBond,
		EMixtormatPatternMode::Herringbone,
		EMixtormatPatternMode::Basketweave,
		EMixtormatPatternMode::Hex,
		EMixtormatPatternMode::OctagonSquare,
		EMixtormatPatternMode::Flagstone,
		EMixtormatPatternMode::Voronoi,
		EMixtormatPatternMode::Hopscotch,
		EMixtormatPatternMode::FrenchAshlar,
		EMixtormatPatternMode::FracturePlates
	};
	for (const EMixtormatPatternMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::PatternModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatPatternFilter* Pattern = GetSelectedPatternId())
				{
					Pattern->PatternMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern && Pattern->PatternMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildGridModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatGridMode Modes[] = {
		EMixtormatGridMode::Straight,
		EMixtormatGridMode::Staggered,
		EMixtormatGridMode::Diamond
	};
	for (const EMixtormatGridMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::GridModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatPatternFilter* Pattern = GetSelectedPatternId())
				{
					Pattern->GridMode = Mode;
					RefreshLayeredPreview();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern && Pattern->GridMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildPatternIdControls()
{
	const auto Pattern = [this]() { return GetSelectedPatternId(); };
	const auto Slider = [this, Pattern](
		const FText& Label,
		float FMixtormatPatternFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatPatternFilter>(
			Label, Pattern, Member, Min, Max, Default, Snap, Hint);
	};
	const auto Toggle = [this](
		const FText& Label,
		bool FMixtormatPatternFilter::* Member,
		const FText& Hint) -> TSharedRef<SWidget>
	{
		return MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Member]()
				{
					const FMixtormatPatternFilter* P = GetSelectedPatternId();
					return P && P->*Member ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Member](const ECheckBoxState State)
				{
					if (FMixtormatPatternFilter* P = GetSelectedPatternId())
					{
						P->*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			Hint);
	};

	// Amounts are neutral at zero, not at the legacy struct defaults (which apply relief
	// and shading). Keep authored, currently dormant modifiers accessible as well.
	const auto HasLegacyTreatment = [Pattern]()
	{
		const FMixtormatPatternFilter* P = Pattern();
		if (!P)
		{
			return false;
		}
		const FMixtormatPatternFilter Defaults;
		return P->bUVVariation
			|| P->bOrthogonalUV != Defaults.bOrthogonalUV
			|| P->UVRotationMin != Defaults.UVRotationMin
			|| P->UVRotationMax != Defaults.UVRotationMax
			|| P->UVScaleMin != Defaults.UVScaleMin
			|| P->UVScaleMax != Defaults.UVScaleMax
			|| P->UVOffset != Defaults.UVOffset
			|| P->bRandomFlipU != Defaults.bRandomFlipU
			|| P->bRandomFlipV != Defaults.bRandomFlipV
			|| P->HeightAmount != 0.0f
			|| P->GapHeight != 0.0f
			|| P->BevelHeight != 0.0f
			|| P->EdgeRoughnessAmount != 0.0f
			|| P->HeightRandom != Defaults.HeightRandom
			|| P->Profile != Defaults.Profile
			|| P->ProfileRandom != Defaults.ProfileRandom
			|| P->Feather != Defaults.Feather
			|| P->FeatherRandom != Defaults.FeatherRandom
			|| P->FeatherGain != Defaults.FeatherGain
			|| P->bRelativeEdgeWidth != Defaults.bRelativeEdgeWidth
			|| P->BevelWidthPixels != Defaults.BevelWidthPixels
			|| P->BevelWidthCells != Defaults.BevelWidthCells
			|| P->BevelVariation != Defaults.BevelVariation
			|| P->BevelInsetPixels != Defaults.BevelInsetPixels
			|| P->EdgeRoughness != Defaults.EdgeRoughness;
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	TSharedRef<SVerticalBox> LegacyTreatment = SNew(SVerticalBox);
		const TSharedRef<SVerticalBox> LegacyCards = LegacyTreatment;

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("PatternGrpLattice", "Lattice"));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("PatternMode", "Pattern Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
				return Pattern
					? MixtormatUI::PatternModeText(Pattern->PatternMode)
					: FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildPatternModeMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("PatternModeHint", "Selects the procedural topology used to publish Pattern regions. Fracture Plates generates hierarchical irregular fracture plates for cracked plaster, concrete, asphalt, stone, and similar broken surfaces.")));
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && Pattern->PatternMode == EMixtormatPatternMode::Grid
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			MixtormatRow::MakeDropdown(
				LOCTEXT("PatternGridMode", "Grid Mode"),
				MixtormatRow::MakeChip(
					TAttribute<FText>::CreateLambda([this]()
					{
						const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
						return Pattern
							? MixtormatUI::GridModeText(Pattern->GridMode)
							: FText::GetEmpty();
					}),
					FOnGetContent::CreateSP(this, &SMixtormat::BuildGridModeMenu),
										nullptr, TAttribute<FText>(), 0.0f),
				LOCTEXT("PatternGridModeHint", "Selects the straight, staggered, or diamond Grid topology."))
		]);
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && Pattern->PatternMode != EMixtormatPatternMode::Hex;
		})
		[
			MakeMemberSliderInt<FMixtormatPatternFilter>(
				LOCTEXT("PatternRows", "Rows"), Pattern, &FMixtormatPatternFilter::Rows, 1.0, 256.0, 8,
				LOCTEXT("PatternRowsHint", "Rows across one UV repeat. Hex derives this from Columns and output aspect."))
		],
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternColumns", "Columns"), Pattern, &FMixtormatPatternFilter::Columns, 1.0, 256.0, 8,
			LOCTEXT("PatternColumnsHint", "Columns across one UV repeat. Set Columns to 1 for stripe-like regions."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && (Pattern->PatternMode == EMixtormatPatternMode::RunningBond
				|| (Pattern->PatternMode == EMixtormatPatternMode::Grid
					&& Pattern->GridMode == EMixtormatGridMode::Staggered));
		})
		[
			Slider(LOCTEXT("PatternRowOffset", "Row Offset"), &FMixtormatPatternFilter::RowOffset, 0.0, 1.0, 0.0, 0.005,
				LOCTEXT("PatternRowOffsetHint", "Alternating-row shift in cell units. Used by Staggered Grid and Running Bond."))
		],
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatPatternFilter* Pattern = GetSelectedPatternId();
			return Pattern && (Pattern->PatternMode == EMixtormatPatternMode::RunningBond
				|| Pattern->PatternMode == EMixtormatPatternMode::Flagstone
				|| Pattern->PatternMode == EMixtormatPatternMode::Voronoi
				|| Pattern->PatternMode == EMixtormatPatternMode::FracturePlates);
		})
		[
			Slider(LOCTEXT("PatternJitter", "Jitter"), &FMixtormatPatternFilter::Jitter, 0.0, 1.0, 0.0, 0.01,
				LOCTEXT("PatternJitterHint", "Varies Running Bond bricks, or the feature points used by Flagstone, Voronoi and Fracture Plates."))
		]));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternSwapAxes", "Swap Axes"), &FMixtormatPatternFilter::bSwapAxes,
			LOCTEXT("PatternSwapAxesHint", "Swaps the lattice axes without changing the ID contract; useful for bars and directional patterns.")),
		Slider(LOCTEXT("PatternGap", "Gap"), &FMixtormatPatternFilter::GapPixels, 0.0, 64.0, 0.0, 0.25,
			LOCTEXT("PatternGapHint", "Region-less grout width in output pixels. Gap pixels emit the invalid-region sentinel, so HSV/Random/Ramp From IDs pass through there."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("PatternRounding", "Rounding"), &FMixtormatPatternFilter::Rounding, 0.0, 1.0, 0.0, 0.005,
			LOCTEXT("PatternRoundingHint", "Rounds the cell corners by blending the two nearest walls instead of taking a hard minimum, so a chamfer fillets into the corner rather than creasing. In cell fractions. 0 is the true Voronoi corner.")));

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternGapRandom", "Gap Random"), &FMixtormatPatternFilter::GapRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternGapRandomHint", "Varies the grout once per piece, for every Pattern Mode. The wall never moves -- each side of it pulls back by its own draw, so the gap between two pieces is the sum of two independent amounts and no two boundaries come out the same width. Symmetric about Gap. A piece is never cut back so far that it disappears, however small it is. Needs a Gap above 0.")),
		Slider(LOCTEXT("PatternGapSlide", "Piece Slide"), &FMixtormatPatternFilter::GapSlide, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternGapSlideHint", "Spends that same grout unevenly around the piece instead of ringing it, so pieces sit off centre in their own sockets. Bounded by each piece's half-gap, so a piece slides only into the room it already has and can never cross into its neighbour. Needs a Gap above 0."))));

	// Fracture Plates' own block, gated the same way Grid Mode is: the mode owns these controls,
	// so they are only in the panel when it is selected. Cells X/Y, Jitter and Seed are the shared
	// lattice rows above and stay there -- Fracture Plates is a Pattern topology like the others,
	// not a second pattern system with its own copy of the lattice.
	const TSharedRef<SVerticalBox> FractureRows = SNew(SVerticalBox);
	const TSharedRef<SVerticalBox> FractureCard = AddCard(
		FractureRows, LOCTEXT("PatternGrpFracture", "Fracture Plates"));
	AddSliderRow(FractureCard, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureSizeVariation", "Size Variation"),
			&FMixtormatPatternFilter::FractureSizeVariation, 0.0, 1.0, 0.3, 0.01,
			LOCTEXT("PatternFractureSizeVariationHint", "Spread of the additive power weights that decide how much territory a plate wins from its neighbours. At 0 every plate is the same importance and the result is even pavement; raising it grows a few plates at the expense of the rest, which is where the mix of very large and small pieces comes from. Additive rather than multiplicative, so a weight moves a boundary while leaving it straight.")),
		Slider(LOCTEXT("PatternFractureSecondaryAmount", "Secondary Amount"),
			&FMixtormatPatternFilter::FractureSecondaryAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("PatternFractureSecondaryAmountHint", "Probability that a primary plate fractures internally at all. A plate that does not stays whole and publishes one ID, so this is the control for how much of the surface reads as large unbroken pieces against locally shattered ones."))));
	AddSliderRow(FractureCard, MixtormatRow::MakePair(
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternFractureSecondaryMin", "Secondary Min"), Pattern,
			&FMixtormatPatternFilter::FractureSecondaryMin, 2.0, 8.0, 2,
			LOCTEXT("PatternFractureSecondaryMinHint", "Fewest pieces a fracturing plate breaks into. Below 2 is not a fracture, so this end is bounded.")),
		MakeMemberSliderInt<FMixtormatPatternFilter>(
			LOCTEXT("PatternFractureSecondaryMax", "Secondary Max"), Pattern,
			&FMixtormatPatternFilter::FractureSecondaryMax, 2.0, 8.0, 3,
			LOCTEXT("PatternFractureSecondaryMaxHint", "Most pieces a fracturing plate breaks into. Raised below Secondary Min, it follows it."))));
	AddSliderRow(FractureCard, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureSecondaryRadius", "Secondary Radius"),
			&FMixtormatPatternFilter::FractureSecondaryRadius, 0.05, 0.75, 0.34, 0.005,
			LOCTEXT("PatternFractureSecondaryRadiusHint", "How far the secondary sites sit from their parent's, in cell fractions. Small values put the split near the middle of the plate; large ones push the pieces out toward its walls. The split is always clipped to its parent, whatever this is set to.")),
		Slider(LOCTEXT("PatternFractureSecondaryJitter", "Secondary Jitter"),
			&FMixtormatPatternFilter::FractureSecondaryJitter, 0.0, 1.0, 0.55, 0.01,
			LOCTEXT("PatternFractureSecondaryJitterHint", "Breaks up the even ring the secondary sites are laid on, in angle and in radius, so a split plate does not come out as a regular pie."))));
	AddSliderRow(FractureCard, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureEdgeIrregularity", "Edge Irregularity"),
			&FMixtormatPatternFilter::FractureEdgeIrregularity, 0.0, 32.0, 8.0, 0.1,
			LOCTEXT("PatternFractureEdgeIrregularityHint", "How far, in output pixels, the fracture field displaces a plate wall from the straight line the power diagram would give it. The field is piecewise planar, so the wall stays a chain of straight runs meeting at angles rather than becoming a curve.")),
		Slider(LOCTEXT("PatternFractureEdgeScale", "Edge Scale"),
			&FMixtormatPatternFilter::FractureEdgeScale, 8.0, 512.0, 96.0, 1.0,
			LOCTEXT("PatternFractureEdgeScaleHint", "The run length of those straight segments, in output pixels. Absolute: Rows and Columns do not stretch it, so changing the plate count leaves the crack character alone."))));
	AddSliderRow(FractureCard, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFractureEdgeDetail", "Edge Detail"),
			&FMixtormatPatternFilter::FractureEdgeDetail, 0.0, 16.0, 2.5, 0.05,
			LOCTEXT("PatternFractureEdgeDetailHint", "A second, shorter octave of the same field: the small branching kinks that sit on the long primary fracture runs.")),
		Slider(LOCTEXT("PatternFractureEdgeDetailScale", "Edge Detail Scale"),
			&FMixtormatPatternFilter::FractureEdgeDetailScale, 4.0, 128.0, 24.0, 0.5,
			LOCTEXT("PatternFractureEdgeDetailScaleHint", "Run length of the detail octave, in output pixels. Also absolute."))));
	AddSliderRow(Panel,
		SNew(SBox)
		.Visibility_Lambda([this]()
		{
			const FMixtormatPatternFilter* SelectedPattern = GetSelectedPatternId();
			return SelectedPattern
				&& SelectedPattern->PatternMode == EMixtormatPatternMode::FracturePlates
				? EVisibility::Visible
				: EVisibility::Collapsed;
		})
		[
			FractureRows
		]);

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatPatternFilter>(
		LOCTEXT("PatternSeed", "Seed"), Pattern, &FMixtormatPatternFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("PatternSeedHint", "Reshuffles feature jitter and every per-region UV, height and bevel draw while preserving the lattice.")));

	const FText LegacyHint = LOCTEXT("PatternLegacyTreatmentHint", "Legacy Pattern UV/relief settings are preserved for compatibility. New setups should use UV From IDs and Relief From IDs.");
	AddSliderRow(LegacyTreatment,
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text(LegacyHint));
	LegacyTreatment = AddCard(LegacyCards, LOCTEXT("PatternGrpUV", "UV Variation"));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternUVEnable", "Enable"), &FMixtormatPatternFilter::bUVVariation,
			LOCTEXT("PatternUVEnableHint", "Transforms the layer source independently around each pattern region centre.")),
		Toggle(LOCTEXT("PatternUVOrthogonal", "90° Only"), &FMixtormatPatternFilter::bOrthogonalUV,
			LOCTEXT("PatternUVOrthogonalHint", "Snaps random region rotation to 90-degree steps, preserving the source tile's periodic orientation."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVRotMin", "Rot Min"), &FMixtormatPatternFilter::UVRotationMin, -360.0, 360.0, 0.0, 1.0,
			LOCTEXT("PatternUVRotMinHint", "Low end of the per-region source rotation range in degrees.")),
		Slider(LOCTEXT("PatternUVRotMax", "Rot Max"), &FMixtormatPatternFilter::UVRotationMax, -360.0, 360.0, 360.0, 1.0,
			LOCTEXT("PatternUVRotMaxHint", "High end of the per-region source rotation range in degrees."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternUVScaleMin", "Scale Min"), &FMixtormatPatternFilter::UVScaleMin, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMinHint", "Low end of the per-region source scale multiplier.")),
		Slider(LOCTEXT("PatternUVScaleMax", "Scale Max"), &FMixtormatPatternFilter::UVScaleMax, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("PatternUVScaleMaxHint", "High end of the per-region source scale multiplier."))));
	AddSliderRow(LegacyTreatment, Slider(
		LOCTEXT("PatternUVOffset", "Offset"), &FMixtormatPatternFilter::UVOffset, 0.0, 1.0, 0.0, 0.01,
		LOCTEXT("PatternUVOffsetHint", "Maximum random source translation per region, as a fraction of one source repeat.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Toggle(LOCTEXT("PatternFlipU", "Flip U"), &FMixtormatPatternFilter::bRandomFlipU,
			LOCTEXT("PatternFlipUHint", "Randomly mirrors the source across U per region.")),
		Toggle(LOCTEXT("PatternFlipV", "Flip V"), &FMixtormatPatternFilter::bRandomFlipV,
			LOCTEXT("PatternFlipVHint", "Randomly mirrors the source across V per region."))));

	LegacyTreatment = AddCard(LegacyCards, LOCTEXT("PatternGrpRelief", "Relief"));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternGapHeight", "Gap Height"), &FMixtormatPatternFilter::GapHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternGapHeightHint", "Where the grout sits relative to the cells. Negative sinks it into a trench, positive stands it proud as a raised mortar line. Needs a Gap above 0 -- without one every pixel belongs to a cell and there is nothing outside the IDs to move.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternHeight", "Height"), &FMixtormatPatternFilter::HeightAmount, 0.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternHeightHint", "How far each cell stands off the base. The face stays flat -- for a slope across each cell, stack Ramp From IDs over this.")),
		Slider(LOCTEXT("PatternHeightRandom", "Height Random"), &FMixtormatPatternFilter::HeightRandom, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("PatternHeightRandomHint", "How far below Height a cell may be drawn, as a multiplier. At 0 every cell sits at full Height; at 1 they spread the whole way down to the base. Never negative -- a cell below the base would feather back up at its wall and read as a recessed panel in a raised frame."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternProfile", "Profile"), &FMixtormatPatternFilter::Profile, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileHint", "The chamfer's cross-section, from the grout line up to the flat of the cell. -1 is a cove that hugs the grout then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose that lifts away and rounds over. Never changes the chamfer's width or height.")),
		Slider(LOCTEXT("PatternProfileRandom", "Profile Random"), &FMixtormatPatternFilter::ProfileRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternProfileRandomHint", "Offsets the roundness per cell, so one cell's bullnose can be its neighbour's cove."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternFeather", "Feather"), &FMixtormatPatternFilter::Feather, 0.0, 0.5, 0.15, 0.005,
			LOCTEXT("PatternFeatherHint", "Eases each cell's height out at its boundary so neighbouring pieces meet through a ramp rather than a one-texel cliff.")),
		Slider(LOCTEXT("PatternFeatherRandom", "Feather Random"), &FMixtormatPatternFilter::FeatherRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternFeatherRandomHint", "Varies the feather width once per cell, so the run-out is not identical on every piece."))));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternFeatherGain", "Feather Gain"), &FMixtormatPatternFilter::FeatherGain, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("PatternFeatherGainHint", "What the feather does on the way up, rather than how wide it is. The run-out is a straight line, and a straight line is the one shape a normal map cannot show -- a normal reads a change in slope, and a constant ramp has none, so the band lights as a single flat facet however much height it moves. Gain bends the curve: the slope leaving the wall goes from 1 to 1 + Gain, and past 1 it arcs above the face and leaves a raised lip just inside the edge. Both ends stay pinned, so the grout wall and the flat face never move.")));


	LegacyTreatment = AddCard(LegacyCards, LOCTEXT("PatternGrpEdges", "Edges"));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelHeight", "Height"), &FMixtormatPatternFilter::BevelHeight, -1.0, 1.0, 0.0, 0.001,
			LOCTEXT("PatternBevelHeightHint", "Stands each cell proud of the grout, with the chamfer ramping down to it. Negative sinks the cell face below the grout instead. The gap itself is untouched either way -- that is Gap Height.")),
		Slider(LOCTEXT("PatternBevelWidth", "Width"), &FMixtormatPatternFilter::BevelWidthPixels, 0.25, 64.0, 4.0, 0.25,
			LOCTEXT("PatternBevelWidthHint", "Chamfer width, in output pixels or as a fraction of the cell depending on Relative Width below."))));
	AddSliderRow(LegacyTreatment, Toggle(
		LOCTEXT("PatternRelativeEdge", "Relative Width"), &FMixtormatPatternFilter::bRelativeEdgeWidth,
		LOCTEXT("PatternRelativeEdgeHint", "Measures the chamfer as a fraction of the cell instead of in output pixels: 0 at the wall, 1 at the point furthest inside. Frames every cell the same way whatever its size or aspect, and is normalised against how far jitter pushes the deepest interior point. Off keeps an even visual width across cells of different sizes. Width comes from Width (Cells) when on and Width when off.")));
	AddSliderRow(LegacyTreatment,
		Slider(LOCTEXT("PatternBevelWidthCells", "Width (Cells)"), &FMixtormatPatternFilter::BevelWidthCells, 0.0, 1.0, 0.25, 0.005,
			LOCTEXT("PatternBevelWidthCellsHint", "The chamfer width used in Relative mode, as a fraction of the way from the cell wall to its deepest interior point. 1 runs the chamfer all the way in, leaving no flat face.")));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternBevelVariation", "Variation"), &FMixtormatPatternFilter::BevelVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternBevelVariationHint", "Varies bevel width once per region.")),
		Slider(LOCTEXT("PatternBevelInset", "Inset"), &FMixtormatPatternFilter::BevelInsetPixels, -32.0, 32.0, 0.0, 0.25,
			LOCTEXT("PatternBevelInsetHint", "Slides the chamfer across the grout line in output pixels. Negative puts it out in the gap, positive pulls it onto the cell face."))));
	AddSliderRow(LegacyTreatment, MixtormatRow::MakePair(
		Slider(LOCTEXT("PatternEdgeRoughness", "Roughness"), &FMixtormatPatternFilter::EdgeRoughness, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("PatternEdgeRoughnessHint", "Roughness value approached at region edges. Applied after the layer composite, so it intentionally bypasses the layer Roughness Influence control.")),
		Slider(LOCTEXT("PatternEdgeRoughnessAmount", "Amount"), &FMixtormatPatternFilter::EdgeRoughnessAmount, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("PatternEdgeRoughnessAmountHint", "Strength of edge roughness. 0 leaves the packed roughness channel unchanged."))));

	const TSharedRef<SVerticalBox> LegacyContainer = SNew(SVerticalBox);
		const TSharedRef<SVerticalBox> LegacyRows = AddCard(
			LegacyContainer, LOCTEXT("PatternLegacyTreatmentHeading", "LEGACY TREATMENT"));
		AddSliderRow(LegacyRows, LegacyCards);
		Panel = Cards;
		Panel->AddSlot().AutoHeight().Padding(0.0f, MixtormatTokens::SliderRowGap, 0.0f, 0.0f)
	[
		SNew(SBox)
		.Visibility_Lambda([HasLegacyTreatment]()
		{
			return HasLegacyTreatment() ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SBox)
			.ToolTipText(LegacyHint)
			[
				LegacyContainer
			]
		]
	];

	return SNew(SBox)
		.Visibility_Lambda([this]()
		{
			return GetSelectedPatternId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("PatternIdHeading", "PATTERN IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				.Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::PatternId))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatPatternFilter* Selected = GetSelectedPatternId();
							return Selected && Selected->bEnabled
								? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatPatternFilter* Selected = GetSelectedPatternId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("PatternEnabledHint", "Enable this Pattern ID producer"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildRampIdControls()
{
	const auto Ramp = [this]() { return GetSelectedRampId(); };

	const auto Slider = [this, Ramp](
		const FText& Label,
		float FMixtormatRampIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatRampIdFilter>(Label, Ramp, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	// Relief first, because it is the whole point of the node. The gradient controls below shape
	// what the ramp does; these decide whether it does anything at all.
	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("RampGrpRelief", "Relief"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RampIntensity", "Intensity"), &FMixtormatRampIdFilter::HeightAmount, 0.0, 0.5, 0.05, 0.001,
			LOCTEXT("RampIntensityHint", "How strongly each region's ramp meets the surface. A blend weight, so 0 leaves the surface untouched under every mode and skips the pass entirely.")),
		Slider(LOCTEXT("RampIntensityRandom", "Random Intensity"), &FMixtormatRampIdFilter::IntensityRandom, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RampIntensityRandomHint", "Per-region jitter on that strength. 0 leaves every region at full Intensity -- unlike Pattern IDs' Height pair, a uniform ramp strength is meaningful on its own, since every region still tilts in its own direction."))));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("RampBlendMode", "Blend"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R ? MixtormatUI::MaskBlendModeText(R->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildRampIdBlendModeMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("RampBlendModeHint", "How the ramp meets the height under it. Add/Sub is the centred case -- a region rises on one side exactly as much as it falls on the other -- and Min carves, Multiply darkens. The normal is derived from the blended height rather than blended separately, so it always describes the surface actually written.")));

	Panel = AddCard(Cards, LOCTEXT("RampGrpGradient", "Gradient"));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RampRotate", "Random Rotation"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->bRotateRandom ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->bRotateRandom = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("RampRotateHint", "Gives every region's gradient its own direction. The ramp fits its region's bounding box exactly at any angle, so turning this on never clips or flattens it. Off puts every gradient on the same axis, which reads as a comb over the whole surface rather than as pieces that settled independently.")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("RampAngleStep", "Angle Stepping"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatRampIdFilter* R = GetSelectedRampId();
				return R && R->bAngleStepping ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatRampIdFilter* R = GetSelectedRampId())
				{
					R->bAngleStepping = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("RampAngleStepHint", "Snaps every region's angle to a multiple of the step below, so a lattice reads as deliberately laid rather than scattered.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("RampAngleStepDegrees", "Step"), &FMixtormatRampIdFilter::AngleStepDegrees, 1.0, 90.0, 5.0, 0.5,
			LOCTEXT("RampAngleStepDegreesHint", "The snap interval in degrees. Applied to the true screen-space angle, so on a long brick 45 degrees is 45 degrees on screen and the steps stay visually even.")));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatRampIdFilter>(
		LOCTEXT("RampSeed", "Seed"), Ramp, &FMixtormatRampIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("RampSeedHint", "Reshuffles which region gets which angle and strength without changing any of the ranges. Independent of the cluster filter's controls, so reseeding here does not re-segment.")));
			Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRampId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RampIdHeading", "RAMP FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::RampId))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatRampIdFilter* Selected = GetSelectedRampId();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatRampIdFilter* Selected = GetSelectedRampId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("RampEnabledHint", "Enable this ramp filter"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildUvIdControls()
{
	const auto Uv = [this]() { return GetSelectedUvId(); };

	const auto Slider = [this, Uv](
		const FText& Label,
		float FMixtormatUvIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatUvIdFilter>(Label, Uv, Member, Min, Max, Default, Snap, Hint);
	};

	const auto Checkbox = [this](
		const FText& Label,
		bool FMixtormatUvIdFilter::* Member,
		const FText& Hint)
	{
		return MixtormatRow::Make(
			Label,
			MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this, Member]()
				{
					const FMixtormatUvIdFilter* U = GetSelectedUvId();
					return U && (U->*Member) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}),
				FOnCheckStateChanged::CreateLambda([this, Member](const ECheckBoxState State)
				{
					if (FMixtormatUvIdFilter* U = GetSelectedUvId())
					{
						U->*Member = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
					}
				})),
			Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("UvIdGrpTransform", "Transform"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdRotationMin", "Rotation Min"), &FMixtormatUvIdFilter::RotationMin, -360.0, 360.0, 0.0, 1.0,
			LOCTEXT("UvIdRotationMinHint", "The low end of each region's rotation draw, in degrees. Equal limits turn every region by the same fixed amount.")),
		Slider(LOCTEXT("UvIdRotationMax", "Rotation Max"), &FMixtormatUvIdFilter::RotationMax, -360.0, 360.0, 360.0, 1.0,
			LOCTEXT("UvIdRotationMaxHint", "The high end of that draw. The angle is taken about the region's own centre, which is measured from the ID map rather than supplied by the producer -- so this works after Pattern IDs, Cluster IDs or ID Group alike."))));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdOrthogonal", "Orthogonal"),
		&FMixtormatUvIdFilter::bOrthogonal,
		LOCTEXT("UvIdOrthogonalHint", "Snaps the drawn rotation to quarter turns. A quarter turn is a permutation of the unit square, so it never disturbs the source tiling; an arbitrary angle can. This is a snap on the random draw, not a recovered intrinsic axis -- an arbitrary region has no direction to recover.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdScaleMin", "Scale Min"), &FMixtormatUvIdFilter::ScaleMin, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("UvIdScaleMinHint", "The low end of each region's zoom on the source. Below 1 magnifies the texture inside the region; above 1 fits more of it in.")),
		Slider(LOCTEXT("UvIdScaleMax", "Scale Max"), &FMixtormatUvIdFilter::ScaleMax, 0.05, 8.0, 1.0, 0.01,
			LOCTEXT("UvIdScaleMaxHint", "The high end of that draw. Equal limits give every region the same zoom."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("UvIdOffsetU", "Offset X"), &FMixtormatUvIdFilter::OffsetU, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UvIdOffsetUHint", "How far a region's source read may slip along U from its own centre, as a fraction of the source tile. This is what stops neighbouring regions showing the same patch of texture.")),
		Slider(LOCTEXT("UvIdOffsetV", "Offset Y"), &FMixtormatUvIdFilter::OffsetV, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("UvIdOffsetVHint", "The same along V. Per-axis, unlike Pattern IDs' single Offset: a plank wants slip along its length and none across it."))));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdFlipU", "Random Flip U"),
		&FMixtormatUvIdFilter::bRandomFlipU,
		LOCTEXT("UvIdFlipUHint", "Mirrors roughly half the regions across U, drawn per region. A mirror maps the unit square onto itself exactly, so it never seams.")));
	AddSliderRow(Panel, Checkbox(
		LOCTEXT("UvIdFlipV", "Random Flip V"),
		&FMixtormatUvIdFilter::bRandomFlipV,
		LOCTEXT("UvIdFlipVHint", "The same across V.")));

	Panel = AddCard(Cards, LOCTEXT("UvIdGrpRandom", "Random"));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatUvIdFilter>(
		LOCTEXT("UvIdSeed", "Seed"), Uv, &FMixtormatUvIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("UvIdSeedHint", "Reshuffles which region gets which rotation, scale, offset and flip without changing any of the ranges. Independent of the producer's seed, so reseeding here does not re-generate the regions.")));
			Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedUvId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("UvIdHeading", "UV FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(EMixtormatDebugPreviewMode::LayerUV,
						LOCTEXT("UvIdPreviewUVHint", "Show the UV this layer samples as a gradient: red = U, green = V, with lines every eighth of a tile."))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatUvIdFilter* Selected = GetSelectedUvId();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatUvIdFilter* Selected = GetSelectedUvId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("UvIdEnabledHint", "Enable this UV filter"))
				])
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildBoundaryIdControls()
{
	const auto Boundary = [this]() { return GetSelectedBoundaryId(); };
	const auto Slider = [this, Boundary](const FText& Label,
		float FMixtormatBoundaryIdFilter::* Member, const double Min, const double Max,
		const double Default, const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatBoundaryIdFilter>(
			Label, Boundary, Member, Min, Max, Default, 0.01, Hint);
	};
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(LOCTEXT("BoundaryIdSource", "Region IDs"),
		MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatBoundaryIdFilter* Selected = GetSelectedBoundaryId();
			if (!Selected)
			{
				return LOCTEXT("BoundaryIdNearestSource", "Nearest preceding Region IDs");
			}
			const FMixtormatOutputReference& Ref = Selected->RegionIdsSource;
			if (!Ref.SourceLayerId.IsValid() && !Ref.SourceChildId.IsValid() && Ref.OutputName.IsNone())
			{
				return LOCTEXT("BoundaryIdNearestSource", "Nearest preceding Region IDs");
			}
			if (!Ref.HasSource())
			{
				return LOCTEXT("BoundaryIdMissingSource", "Missing Region IDs source");
			}
			const auto FindLabel = [this, &Ref](const FText& OwnerName,
				const TArray<FMixtormatLayerChild>& Children)
			{
				const FMixtormatLayerChild* Source = Children.FindByPredicate(
					[&Ref](const FMixtormatLayerChild& Child) { return Child.ChildId == Ref.SourceChildId; });
				return Source ? FText::Format(LOCTEXT("BoundaryIdSourceLabel", "{0} / {1}"),
					OwnerName, GetLayerChildName(*Source)) : LOCTEXT("BoundaryIdMissingSource", "Missing Region IDs source");
			};
			for (const FMixtormatLayer& Layer : WorkingLayers)
			{
				if (Layer.LayerId == Ref.SourceLayerId) return FindLabel(Layer.DisplayName, Layer.Children);
			}
			for (const FMixtormatLayerGroup& Group : WorkingLayerGroups)
			{
				if (Group.GroupId == Ref.SourceLayerId) return FindLabel(Group.DisplayName, Group.Children);
			}
			return LOCTEXT("BoundaryIdMissingSource", "Missing Region IDs source");
		}), FOnGetContent::CreateSP(this, &SMixtormat::BuildBoundaryIdSourceMenu),
					nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("BoundaryIdSourceHint", "Unassigned reads the nearest preceding valid Region IDs. Explicit sources must precede this child; unavailable sources do not fall back to another map.")));
	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("BoundaryIdBoundaryGroup", "Boundary"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BoundaryIdWidth", "Width"), &FMixtormatBoundaryIdFilter::WidthPixels, 0.0, 64.0, 4.0,
			LOCTEXT("BoundaryIdWidthHint", "Boundary width in output pixels.")),
		Slider(LOCTEXT("BoundaryIdSoftness", "Softness"), &FMixtormatBoundaryIdFilter::Softness, 0.0, 1.0, 0.5,
			LOCTEXT("BoundaryIdSoftnessHint", "Softness of the boundary mask transition."))));
	Panel = AddCard(Cards, LOCTEXT("BoundaryIdGapGroup", "Gap"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("BoundaryIdGapWidth", "Width"), &FMixtormatBoundaryIdFilter::GapWidthPixels, 0.0, 64.0, 8.0,
			LOCTEXT("BoundaryIdGapWidthHint", "Gap mask width in output pixels; does not change the source IDs.")),
		Slider(LOCTEXT("BoundaryIdGapSoftness", "Softness"), &FMixtormatBoundaryIdFilter::GapSoftness, 0.0, 1.0, 0.5,
			LOCTEXT("BoundaryIdGapSoftnessHint", "Softness of the gap mask transition."))));
	AddSliderRow(Panel, Slider(LOCTEXT("BoundaryIdGapBias", "Bias"),
		&FMixtormatBoundaryIdFilter::GapBiasPixels, -64.0, 64.0, 0.0,
		LOCTEXT("BoundaryIdGapBiasHint", "Expands or shrinks the gap radius in output pixels; does not assign an inside/outside sign.")));
	Panel = AddCard(Cards, LOCTEXT("BoundaryIdDistanceGroup", "Distance"));
	AddSliderRow(Panel, Slider(LOCTEXT("BoundaryIdDistanceRange", "Range"),
		&FMixtormatBoundaryIdFilter::DistanceRangePixels, 0.25, 256.0, 64.0,
		LOCTEXT("BoundaryIdDistanceRangeHint", "Pixel range mapped into the scalar Distance output.")));
	AddSliderRow(Panel, MakeMemberToggle<FMixtormatBoundaryIdFilter>(
		LOCTEXT("BoundaryIdInvertDistance", "Invert Distance"), Boundary,
		&FMixtormatBoundaryIdFilter::bInvertDistance,
		LOCTEXT("BoundaryIdInvertDistanceHint", "Invert Distance without changing Boundary or Gap.")));
			Panel = Cards;
	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedBoundaryId() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("BoundaryIdHeading", "BOUNDARY FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(MixtormatRow::MakeCheckbox(
				TAttribute<ECheckBoxState>::CreateLambda([this]()
				{
					const FMixtormatBoundaryIdFilter* Selected = GetSelectedBoundaryId();
					return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
				}), FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
				{
					if (FMixtormatBoundaryIdFilter* Selected = GetSelectedBoundaryId())
					{
						Selected->bEnabled = State == ECheckBoxState::Checked;
						RefreshLayeredPreview();
						RebuildLayerList();
					}
				}), LOCTEXT("BoundaryIdEnabledHint", "Enable Boundary From IDs")))
			[Panel]
		];
}

TSharedRef<SWidget> SMixtormat::BuildReliefIdControls()
{
	const auto Relief = [this]() { return GetSelectedReliefId(); };

	const auto Slider = [this, Relief](
		const FText& Label,
		float FMixtormatReliefIdFilter::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatReliefIdFilter>(Label, Relief, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("ReliefIdGrpHeight", "Height"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdHeight", "Amount"), &FMixtormatReliefIdFilter::HeightAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdHeightHint", "The elevation every region gets above the surface under it. Each region is a flat face at its own height -- tilting one is Ramp From IDs, which composites over this.")),
		Slider(LOCTEXT("ReliefIdHeightRandom", "Variation"), &FMixtormatReliefIdFilter::HeightRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdHeightRandomHint", "How far below Amount a region may be drawn, as a multiplier on it. One-sided: at 0 every region sits at full Amount, at 1 they spread down to the base. A region that went below the base would feather back up at its own boundary and read as a recessed panel in a raised frame."))));

	Panel = AddCard(Cards, LOCTEXT("ReliefIdGrpProfile", "Profile"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdProfile", "Profile"), &FMixtormatReliefIdFilter::Profile, -1.0, 1.0, 0.25, 0.01,
			LOCTEXT("ReliefIdProfileHint", "The chamfer's cross-section. -1 is a cove that hugs the boundary then sweeps up into the face, 0 a straight flat chamfer, +1 a bullnose that rounds over onto it. Both ends stay pinned, so this changes the shape and never the width or height.")),
		Slider(LOCTEXT("ReliefIdProfileRandom", "Variation"), &FMixtormatReliefIdFilter::ProfileRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdProfileRandomHint", "Offsets the profile per region rather than scaling it, so one region's bullnose can be its neighbour's cove."))));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdFeather", "Feather"), &FMixtormatReliefIdFilter::Feather, 0.0, 0.5, 0.1, 0.005,
			LOCTEXT("ReliefIdFeatherHint", "Eases each region's elevation out at its own boundary, so neighbours at different heights meet through a ramp rather than a one-texel cliff. In region fractions, measured against the region's own reach, so it means the same on a large region and a small one.")),
		Slider(LOCTEXT("ReliefIdFeatherRandom", "Variation"), &FMixtormatReliefIdFilter::FeatherRandom, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdFeatherRandomHint", "Scales the run-out per region. One-sided: the draw only narrows the feather from the authored value, never widens it past what was asked for."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdFeatherGain", "Feather Gain"), &FMixtormatReliefIdFilter::FeatherGain, 0.0, 4.0, 0.0, 0.01,
			LOCTEXT("ReliefIdFeatherGainHint", "What the run-out does on the way up, rather than how wide it is. A straight ramp has no change in slope and lights as one flat facet; Gain bends it, and past 1 leaves a raised lip just inside the edge -- the rolled-over rim of a settled tile.")));

	Panel = AddCard(Cards, LOCTEXT("ReliefIdGrpBevel", "Bevel"));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelHeight", "Height"), &FMixtormatReliefIdFilter::BevelHeight, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdBevelHeightHint", "Signed, and it lifts the region face: positive stands the region proud with the chamfer ramping down to the boundary, negative sinks the face instead. The region-less band is untouched either way -- that is Gap Height below.")));
	AddSliderRow(Panel, MixtormatRow::Make(
		LOCTEXT("ReliefIdRelativeWidth", "Relative Width"),
		MixtormatRow::MakeCheckbox(
			TAttribute<ECheckBoxState>::CreateLambda([this]()
			{
				const FMixtormatReliefIdFilter* R = GetSelectedReliefId();
				return R && R->bRelativeWidth ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}),
			FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
			{
				if (FMixtormatReliefIdFilter* R = GetSelectedReliefId())
				{
					R->bRelativeWidth = State == ECheckBoxState::Checked;
					RefreshLayeredPreview();
				}
			})),
		LOCTEXT("ReliefIdRelativeWidthHint", "Measures the chamfer as a fraction of the way from a region's boundary to its deepest interior point, rather than in output pixels. Relative frames every region the same way whatever its size; absolute keeps an even visual width across regions of different sizes.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelWidthPixels", "Width"), &FMixtormatReliefIdFilter::BevelWidthPixels, 0.25, 64.0, 4.0, 0.25,
			LOCTEXT("ReliefIdBevelWidthPixelsHint", "The chamfer's width in output pixels. Used when Relative Width is off.")));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdBevelWidthCells", "Width (Relative)"), &FMixtormatReliefIdFilter::BevelWidthCells, 0.0, 1.0, 0.25, 0.01,
			LOCTEXT("ReliefIdBevelWidthCellsHint", "The same width as a region fraction. A separate control because a pixel width and a fraction need different ranges to be draggable at all.")));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdBevelVariation", "Variation"), &FMixtormatReliefIdFilter::BevelVariation, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdBevelVariationHint", "Narrows the chamfer per region. One-sided, like Height: the draw never widens one past the authored value.")),
		Slider(LOCTEXT("ReliefIdBevelInset", "Inset"), &FMixtormatReliefIdFilter::BevelInsetPixels, -32.0, 32.0, 0.0, 0.25,
			LOCTEXT("ReliefIdBevelInsetHint", "Slides the chamfer band across the boundary, in output pixels. Negative walks it outside the region, positive pulls it onto the face, zero starts it exactly at the boundary."))));
	AddSliderRow(Panel,
		Slider(LOCTEXT("ReliefIdGapHeight", "Gap Height"), &FMixtormatReliefIdFilter::GapHeight, -1.0, 1.0, 0.0, 0.01,
			LOCTEXT("ReliefIdGapHeightHint", "Where a region-less band sits relative to the regions -- Pattern IDs' grout, or any pixel the producer marked invalid. Negative sinks it into a trench, positive stands it proud as a raised mortar line. Gap width is topology and stays on the producer; only its height lives here, so the two compose instead of fighting.")));

	Panel = AddCard(Cards, LOCTEXT("ReliefIdGrpEdge", "Edge"));
	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("ReliefIdEdgeRoughness", "Roughness"), &FMixtormatReliefIdFilter::EdgeRoughness, 0.0, 1.0, 0.65, 0.01,
			LOCTEXT("ReliefIdEdgeRoughnessHint", "The roughness written along region boundaries -- the scuffed, unpolished band a worn edge has.")),
		Slider(LOCTEXT("ReliefIdEdgeRoughnessAmount", "Amount"), &FMixtormatReliefIdFilter::EdgeRoughnessAmount, 0.0, 1.0, 0.5, 0.01,
			LOCTEXT("ReliefIdEdgeRoughnessAmountHint", "How strongly that band replaces the roughness already there. At 0 the edge shading pass is skipped entirely."))));


	Panel = AddCard(Cards, LOCTEXT("ReliefIdGrpRandom", "Random"));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatReliefIdFilter>(
		LOCTEXT("ReliefIdSeed", "Seed"), Relief, &FMixtormatReliefIdFilter::Seed, 0.0, 64.0, 1,
		LOCTEXT("ReliefIdSeedHint", "Reshuffles which region gets which height, chamfer width and profile without changing any of the ranges. Independent of the producer's seed, so reseeding here does not re-generate the regions.")));
			Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedReliefId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("ReliefIdHeading", "RELIEF FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				MixtormatRow::MakeCheckbox(
					TAttribute<ECheckBoxState>::CreateLambda([this]()
					{
						const FMixtormatReliefIdFilter* Selected = GetSelectedReliefId();
						return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
					}),
					FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
					{
						if (FMixtormatReliefIdFilter* Selected = GetSelectedReliefId())
						{
							Selected->bEnabled = State == ECheckBoxState::Checked;
							RefreshLayeredPreview();
							RebuildLayerList();
						}
					}),
					LOCTEXT("ReliefIdEnabledHint", "Enable this relief filter")))
			[
				Panel
			]
		];
}

TSharedRef<SWidget> SMixtormat::BuildIdGroupFeatureMenu()
{
	MixtormatMenu::FBuilder Menu;
	const auto Entry = [this, &Menu](const EMixtormatIdGroupMode Mode, const FText Label)
	{
		Menu.Item(Label, nullptr, FSimpleDelegate::CreateLambda([this, Mode]()
		{
			if (FMixtormatIdGroup* Group = GetSelectedIdGroup())
			{
				Group->Mode = Mode;
				RefreshLayeredPreview();
				RebuildLayerList();
			}
		}))
		.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
		{
			const FMixtormatIdGroup* Group = GetSelectedIdGroup();
			return Group && Group->Mode == Mode;
		}));
	};

	Entry(EMixtormatIdGroupMode::Difference, LOCTEXT("IdGroupModeDifference", "Difference"));
	Entry(EMixtormatIdGroupMode::Pair, LOCTEXT("IdGroupModePair", "Pair"));
	Entry(EMixtormatIdGroupMode::MaxId, LOCTEXT("IdGroupModeMaxId", "Max ID"));
	Entry(EMixtormatIdGroupMode::MinId, LOCTEXT("IdGroupModeMinId", "Min ID"));
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildIdGroupControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("IdGroupModeLabel", "Operation"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatIdGroup* Group = GetSelectedIdGroup();
				if (!Group)
				{
					return FText::GetEmpty();
				}
				switch (Group->Mode)
				{
				case EMixtormatIdGroupMode::Pair: return LOCTEXT("IdGroupModePair", "Pair");
				case EMixtormatIdGroupMode::MaxId: return LOCTEXT("IdGroupModeMaxId", "Max ID");
				case EMixtormatIdGroupMode::MinId: return LOCTEXT("IdGroupModeMinId", "Min ID");
				default: return LOCTEXT("IdGroupModeDifference", "Difference");
				}
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildIdGroupFeatureMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		TAttribute<FText>::CreateLambda([this]()
		{
			const FMixtormatIdGroup* Group = GetSelectedIdGroup();
			switch (Group ? Group->Mode : EMixtormatIdGroupMode::Difference)
			{
			case EMixtormatIdGroupMode::Pair:
				return LOCTEXT("IdGroupPairHint", "Hash ordered ID pairs to subdivide overlapping regions. Source order matters; the result does not retain recoverable parent assignments.");
			case EMixtormatIdGroupMode::MaxId:
				return LOCTEXT("IdGroupMaxIdHint", "Select the numerically largest valid ID per pixel. This is a numeric selector, not pair subdivision.");
			case EMixtormatIdGroupMode::MinId:
				return LOCTEXT("IdGroupMinIdHint", "Select the numerically smallest valid ID per pixel. This is a numeric selector, not pair subdivision.");
			default:
				return LOCTEXT("IdGroupDifferenceHint", "Fold ordered Region IDs sources, preserving equal IDs and hashing unequal overlaps.");
			}
		})));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(LOCTEXT("IdGroupSourcesLabel", "Sources"),
		MixtormatRow::MakeChip(LOCTEXT("IdGroupAddSource", "Add Source"),
			FOnGetContent::CreateLambda([this]() { return BuildIdGroupSourceMenu(GetSelectedChildAddress()); }),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("IdGroupSourcesHint", "Add live Region IDs references. Reorder or remove their subordinate rows in the stack; producers stay in place.")));
	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatIdGroup>(
		LOCTEXT("IdGroupBoundaryWidth", "Boundary Width"),
		[this]() { return GetSelectedIdGroup(); }, &FMixtormatIdGroup::BoundaryWidth, 1.0, 16.0, 1,
		LOCTEXT("IdGroupBoundaryWidthHint", "Width of the Boundary output in pixels, from 1 to 16. Does not change the Region IDs output.")));
	Panel->AddSlot().AutoHeight()
	[
		BuildChildOutputsControls(GetChildCapabilitiesForChildType(EMixtormatLayerChildType::IdGroup))
	];

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedIdGroup() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("IdGroupHeading", "ID GROUP"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeChildOutputPreviewButton(
						GetPreviewOutputSetForChildType(EMixtormatLayerChildType::IdGroup))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatIdGroup* Selected = GetSelectedIdGroup();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatIdGroup* Selected = GetSelectedIdGroup())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("IdGroupEnabledHint", "Enable this ID group"))
				])
			[
				Panel
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildRandomIdBlendModeMenu()
{
	MixtormatMenu::FBuilder Menu;
	const EMixtormatMaskBlendMode Modes[] = {
		EMixtormatMaskBlendMode::Replace,
		EMixtormatMaskBlendMode::Add,
		EMixtormatMaskBlendMode::Subtract,
		EMixtormatMaskBlendMode::Multiply,
		EMixtormatMaskBlendMode::Min,
		EMixtormatMaskBlendMode::Max,
		EMixtormatMaskBlendMode::AddSub,
		EMixtormatMaskBlendMode::Overlay
	};
	for (const EMixtormatMaskBlendMode Mode : Modes)
	{
		Menu.Item(
			MixtormatUI::MaskBlendModeText(Mode),
			nullptr,
			FSimpleDelegate::CreateLambda([this, Mode]()
			{
				if (FMixtormatRandomIdMask* R = GetSelectedRandomId())
				{
					R->BlendMode = Mode;
					RefreshLayeredPreview();
					RebuildLayerList();
				}
			}))
			.Checked(TAttribute<bool>::CreateLambda([this, Mode]()
			{
				const FMixtormatRandomIdMask* R = GetSelectedRandomId();
				return R && R->BlendMode == Mode;
			}));
	}
	return Menu.Build();
}

TSharedRef<SWidget> SMixtormat::BuildRandomIdControls()
{
	const auto Random = [this]() { return GetSelectedRandomId(); };

	const auto Slider = [this, Random](
		const FText& Label,
		float FMixtormatRandomIdMask::* Member,
		const double Min,
		const double Max,
		const double Default,
		const double Snap,
		const FText& Hint)
	{
		return MakeMemberSlider<FMixtormatRandomIdMask>(Label, Random, Member, Min, Max, Default, Snap, Hint);
	};

	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);

	AddSliderRow(Panel, MixtormatRow::MakePair(
		Slider(LOCTEXT("RandomMin", "Min"), &FMixtormatRandomIdMask::MinValue, 0.0, 1.0, 0.0, 0.01,
			LOCTEXT("RandomMinHint", "The low end of the range a region's value is drawn from. Narrowing the range is how the variation is kept subtle without touching anything downstream.")),
		Slider(LOCTEXT("RandomMax", "Max"), &FMixtormatRandomIdMask::MaxValue, 0.0, 1.0, 1.0, 0.01,
			LOCTEXT("RandomMaxHint", "The high end. Setting Min above Max runs the range backwards, which is the same picture as Invert."))));

	AddSliderRow(Panel, MakeMemberSliderInt<FMixtormatRandomIdMask>(
		LOCTEXT("RandomSeed", "Seed"), Random, &FMixtormatRandomIdMask::Seed, 0.0, 64.0, 1,
		LOCTEXT("RandomSeedHint", "Reshuffles which region gets which value without changing the range. Independent of the cluster filter's own controls, so reseeding here does not re-segment -- it is the cheap dial.")));

	const TSharedRef<SVerticalBox> Cards = Panel;
		Panel = AddCard(Cards, LOCTEXT("RandomGrpBlend", "Blend"));
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(
		LOCTEXT("RandomBlendMode", "Mode"),
		MixtormatRow::MakeChip(
			TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatRandomIdMask* R = GetSelectedRandomId();
				return R ? MixtormatUI::MaskBlendModeText(R->BlendMode) : FText::GetEmpty();
			}),
			FOnGetContent::CreateSP(this, &SMixtormat::BuildRandomIdBlendModeMenu),
						nullptr, TAttribute<FText>(), 0.0f),
		LOCTEXT("RandomBlendModeHint", "How the per-region value combines with the mask accumulated above it in this layer. Multiply against a painted mask is the common one: vary only where you already wanted the layer.")));

	AddSliderRow(Panel, Slider(LOCTEXT("RandomWeight", "Weight"), &FMixtormatRandomIdMask::Weight, 0.0, 1.0, 1.0, 0.01,
		LOCTEXT("RandomWeightHint", "How far the blend is taken. 0 is the off switch for this node, and it costs nothing -- the pass is skipped rather than run to reproduce its input.")));

	// The shared shaping block, and it is doing real work here rather than being boilerplate.
	// Contrast above 1 about the 0.5 midpoint pulls most regions toward the mean and leaves a
	// few outliers, which is what the prototype's ramp distribution mode was for.
	AddMaskShapingRows(Panel, [this]() -> FMixtormatMaskShaping*
	{
		FMixtormatRandomIdMask* R = GetSelectedRandomId();
		return R ? &R->Shaping : nullptr;
	});
	Panel = Cards;

	return SNew(SBox)
		.Visibility_Lambda([this]() { return GetSelectedRandomId() != nullptr ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("RandomIdHeading", "RANDOM FROM IDS"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, MixtormatTokens::InspectorFeatureButtonGap, 0.0f)
				[
					MakeFeaturePreviewButton(
						EMixtormatDebugPreviewMode::LayerMask,
						LOCTEXT("PreviewRandomId", "Preview this mask in unlit dark red and cyan"))
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MixtormatRow::MakeCheckbox(
						TAttribute<ECheckBoxState>::CreateLambda([this]()
						{
							const FMixtormatRandomIdMask* Selected = GetSelectedRandomId();
							return Selected && Selected->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}),
						FOnCheckStateChanged::CreateLambda([this](const ECheckBoxState State)
						{
							if (FMixtormatRandomIdMask* Selected = GetSelectedRandomId())
							{
								Selected->bEnabled = State == ECheckBoxState::Checked;
								RefreshLayeredPreview();
								RebuildLayerList();
							}
						}),
						LOCTEXT("RandomEnabledHint", "Enable this mask"))
				])
			[
				Panel
			]
		];
}


TSharedRef<SWidget> SMixtormat::BuildOutputReferenceControls()
{
	TSharedRef<SVerticalBox> Panel = SNew(SVerticalBox);
	AddSliderRow(Panel, MixtormatRow::MakeDropdown(LOCTEXT("OutputReferenceSource", "Source"),
		SNew(SBox)
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
			return Child && !Child->IsInstance()
				&& Child->OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds;
		})
		[
			MixtormatRow::MakeChip(TAttribute<FText>::CreateLambda([this]()
			{
				const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
				return Child ? GetLayerChildName(*Child) : FText::GetEmpty();
			}), FOnGetContent::CreateLambda([this]() { return BuildOutputReferenceSourceMenu(GetSelectedChildAddress()); }),
							nullptr, TAttribute<FText>(), 0.0f)
		], LOCTEXT("OutputReferenceSourceHint", "A live output address, not a copy of the producer.")));
	Panel->AddSlot().AutoHeight()
	[
		SNew(SButton)
		.ButtonStyle(&FMixtormatStyle::Get().GetWidgetStyle<FButtonStyle>(TEXT("Mixtormat.CompactRowButton")))
		.Text(LOCTEXT("OutputReferenceGoToSource", "Go to Source"))
		.IsEnabled_Lambda([this]()
		{
			const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
			if (!Child) { return false; }
			FMixtormatChildAddress Source;
			Source.OwnerId = Child->IsInstance() ? Child->SourceLayerId : Child->OutputReference.SourceLayerId;
			Source.ChildId = Child->IsInstance() ? Child->SourceChildId : Child->OutputReference.SourceChildId;
			Source.OwnerType = MixtormatLayerGroups::FindGroup(WorkingLayerGroups, Source.OwnerId)
				? EMixtormatChildOwnerType::Group : EMixtormatChildOwnerType::Layer;
			return ResolveChildAt(Source) != nullptr;
		})
		.OnClicked_Lambda([this]() { return GoToChildInstanceSource(GetSelectedChildAddress()); })
	];
	return SNew(SBox)
		.Visibility_Lambda([this]() { return HasSelectedOutputReference() ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SMixtormatInspectorGroup)
			.Title(LOCTEXT("OutputReferenceHeading", "OUTPUT REFERENCE"))
			.InitiallyExpanded(true)
			.HeaderAction(
				SNew(SBox)
				.Visibility_Lambda([this]()
				{
					const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
					return Child && Child->OutputReference.Kind == EMixtormatPublishedFieldKind::RegionIds
						? EVisibility::Visible : EVisibility::Collapsed;
				})
				[MakeChildOutputPreviewButton(GetPreviewOutputSetForChildType(EMixtormatLayerChildType::OutputReference))])
			[Panel]
		];
}

bool SMixtormat::HasSelectedOutputReference() const
{
	const FMixtormatLayerChild* Child = ResolveChildAt(GetSelectedChildAddress());
	return Child && Child->Type == EMixtormatLayerChildType::OutputReference;
}
#undef LOCTEXT_NAMESPACE
