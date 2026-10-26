#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeleton;

/**
 * Writes pose sequences: non-additive, bone tracks only, no curves (plan section 5), so face sequences and
 * MetaHuman face performances layer over them untouched.
 */
namespace DTHPoseWriter
{
	/** 30 fps, as DTH's own sequences. */
	FFrameRate FrameRate();

	/**
	 * Creates and saves an asset at Folder/Name with one frame per entry of Frames. Each frame maps bone names to
	 * track values (authored on the skeleton); bones not named are keyed at the skeleton's reference pose.
	 */
	UAnimSequence* Create(const FString& Folder, const FString& Name, USkeleton* Skeleton,
		TConstArrayView<TMap<FName, FTransform>> Frames, FString& OutError);
}
