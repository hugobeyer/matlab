// Copyright 2026 Hugo Beyer. All Rights Reserved.
#pragma once
#include "MixtormatParameterBinding.h"

namespace MixtormatParameterDrivers
{
	enum class EExecution : uint8 { Unsupported, Float, Integer, EnumBins };
	struct FCapability
	{
		EExecution Execution = EExecution::Unsupported;
		FText Reason;
		int32 IntMin = 0;
		int32 IntMax = 0;
		bool IsSupported() const { return Execution != EExecution::Unsupported; }
	};
	// Explicit allowlist of actual GPU readers, not permission inferred from a property's type.
	MIXTORMATRUNTIME_API FCapability Capability(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target);
	MIXTORMATRUNTIME_API bool SourceSupported(const FMixtormatBindingScope& Scope, const FMixtormatParameterAddress& Target,
		FGuid SourceOwner, FGuid SourceChild, EMixtormatDriverSourceKind Kind, FText& Error);
	MIXTORMATRUNTIME_API bool ValidateEnumBins(const UEnum* Enum, const TArray<FMixtormatDriverEnumBin>& Bins, FText& Error);
	MIXTORMATRUNTIME_API bool ValidateTransfer(const FMixtormatParameterDriver& Driver, FText& Error);
	MIXTORMATRUNTIME_API float MapSignal(const FMixtormatParameterDriver& Driver, float Signal);
	MIXTORMATRUNTIME_API int32 QuantizeInteger(double MappedValue, int32 Minimum, int32 Maximum);
}
