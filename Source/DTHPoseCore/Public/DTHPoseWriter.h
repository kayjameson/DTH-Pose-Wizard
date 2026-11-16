#pragma once

#include "CoreMinimal.h"

class UAnimSequence;
class USkeleton;

/**
 * Writes pose sequences: non-additive bone tracks, and the face curves the pose's own face needs (DTH's face-board and
 * morph curves, only when the Daz preset or clip sets a face). A pose without a face has no curves, so face sequences
 * and MetaHuman face performances layer over it untouched.
 */
namespace DTHPoseWriter
{
	/** 30 fps, as DTH's own sequences. */
	DTHPOSECORE_API FFrameRate FrameRate();

	/**
	 * Creates and saves an asset at Folder/Name with one frame per entry of Frames. Each frame maps bone names to
	 * track values (authored on the skeleton); bones not named are keyed at the skeleton's reference pose. Rate: an animation
	 * clip's own frame rate (poses use the default).
	 */
	DTHPOSECORE_API UAnimSequence* Create(const FString& Folder, const FString& Name, USkeleton* Skeleton,
		TConstArrayView<TMap<FName, FTransform>> Frames, FString& OutError, FFrameRate Rate = FrameRate(),
		TConstArrayView<TMap<FName, float>> Curves = {});
}
