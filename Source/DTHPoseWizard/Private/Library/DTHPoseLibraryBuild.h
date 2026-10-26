#pragma once

#include "CoreMinimal.h"

class UDTHPoseLibrary;

/** One line of a build or write report. */
struct FDTHPoseMessage
{
	enum class ESeverity : uint8 { Info, Warning, Error };
	ESeverity Severity = ESeverity::Info;
	FString Text;
};

namespace DTHPoseLibraryBuild
{
	/**
	 * Builds (or rebuilds in place) the library asset at LibraryPath from the sampler's DTH_PoseSamples.json. The same
	 * samples always give the same library.
	 */
	UDTHPoseLibrary* Build(const FString& SamplesFile, const FString& LibraryPath, TArray<FDTHPoseMessage>& OutReport);

	/** Why a morph a preset changes is not reproduced on a DTH character; empty when DTH reproduces it from the bones. */
	FString MissingReason(const FString& Morph);
}
