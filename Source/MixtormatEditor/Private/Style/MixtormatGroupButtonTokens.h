// Copyright 2026 Hugo Beyer. All Rights Reserved.

#pragma once

// Shared action targets. Include this header in the parent live-theme registry;
// do not redeclare these in DesignTokens. Ranges mirror ui-prototype/app.js.
namespace MixtormatTokens
{
	inline float GroupButtonGradientTop = 0.18f;             // 0..1
	inline float GroupButtonGradientBottom = 0.03f;          // 0..1
	inline float GroupButtonHoverGradientTop = 0.32f;        // 0..1
	inline float GroupButtonHoverGradientBottom = 0.08f;     // 0..1
	inline float GroupButtonSelectedGradientTop = 0.45f;     // 0..1
	inline float GroupButtonSelectedGradientBottom = 0.12f;  // 0..1
	inline float GroupButtonGradientSaturation = 1.5f;       // 0..4
	inline float GroupButtonHairlineOpacity = 0.16f;         // 0..1
	inline float GroupButtonHoverHairlineOpacity = 0.4f;     // 0..1
	inline float GroupButtonSelectedHairlineOpacity = 0.6f;  // 0..1
	inline float GroupButtonHairlineWidth = 1.0f;            // 0..4 px
	inline float GroupButtonHairlineSaturation = 1.5f;       // 0..4
	inline float GroupButtonFontSize = 9.0f;                 // 6..20 px
	inline float GroupButtonFontWeight = 400.0f;             // 100..900, named font face required
	inline float GroupButtonTracking = 0.0f;                // 0..4 px, convert to 1/1000 em
	inline float GroupButtonHeight = 24.0f;                 // 12..40 px
	inline float GroupButtonTextOpacity = 0.78f;             // 0..1
	inline float GroupButtonSeparatorWidth = 1.0f;           // 0..4 px
	inline float GroupButtonSeparatorHeight = 14.0f;         // 0..32 px
	inline float GroupButtonSeparatorOpacity = 0.08f;        // 0..1
}
