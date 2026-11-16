#pragma once

#include "CoreMinimal.h"

struct FReferenceSkeleton;

/**
 * Face bones turned past DTH's face rig, carried as channels beside the face curves ("bone|<bone>|<axis>", in units of
 * FDTHShapeFit::BoneUnitDeg), so they mix linearly with them (the Expression Wizard's mixer). What plays them (a preview,
 * a sequence key, a control rig key) splits them off and turns them into the bones' local transforms.
 */
namespace DTHFaceBones
{
	DTHFACECORE_API bool IsChannel(FName Name);

	/**
	 * Splits Values into curves and bone turns (a local turn on top of each bone's reference pose). Every bone a channel
	 * names gets a turn, the identity when its channels are 0, so a key sets it back.
	 */
	DTHFACECORE_API void Split(const TMap<FName, float>& Values, TMap<FName, float>& OutCurves, TMap<FName, FQuat>& OutTurns);

	/** The bones' local transforms: each one's reference pose (from Ref) turned. Bones Ref does not have are left out. */
	DTHFACECORE_API TMap<FName, FTransform> Local(const FReferenceSkeleton& Ref, const TMap<FName, FQuat>& Turns);
}
