#include "Build/DTHPoseRig.h"

#include "DTHPoseCharacter.h"
#include "DTHRigKeying.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHPoseLibrary.h"

namespace DTHPoseRig
{
	DTHPoseSolve::FRig Make(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library)
	{
		return DTHPoseCharacter::Make(Mesh, Library.State(INDEX_NONE));
	}

	FDTHDazState Resolve(const UDTHPoseLibrary& Library, int32 Pose, int32 Base)
	{
		if (!Library.Poses.IsValidIndex(Pose) || !Library.Poses[Pose].bLoaded)
		{
			return Library.State(INDEX_NONE);
		}
		if (!Library.Poses[Pose].bPartial)
		{
			return Library.State(Pose);
		}
		const int32 Under = Library.Poses.IsValidIndex(Base) && Library.Poses[Base].bLoaded && Base != Pose ? Base : INDEX_NONE;
		return Library.Compose(Under, Pose);
	}

	TMap<FName, FTransform> LocalPose(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State)
	{
		return DTHPoseCharacter::LocalPose(Rig, Mesh, State);
	}

	TMap<FName, FTransform> Track(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State)
	{
		return DTHPoseCharacter::Track(Rig, Mesh, State);
	}

	UAnimSequence* Write(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library, TConstArrayView<FDTHDazState> Frames,
		const FString& Folder, const FString& Name, FString& OutError)
	{
		return DTHPoseCharacter::Write(Mesh, Library.State(INDEX_NONE), Frames, Folder, Name, OutError);
	}

	bool KeyOntoRig(USkeletalMesh& Mesh, const UDTHPoseLibrary& Library, TConstArrayView<FDTHDazState> States, ULevelSequence* Sequence,
		const FString& BindingName, int32 Frame, bool bStepped, TArray<FString>& OutLines)
	{
		FString Error;
		DTHRigKeying::FTarget Target;
		if (!DTHRigKeying::Resolve(Sequence, BindingName, &Mesh, Target, Error))
		{
			OutLines.Add(TEXT("ERROR: ") + Error);
			return false;
		}
		const DTHPoseSolve::FRig Rig = Make(Mesh, Library);
		for (int32 I = 0; I < States.Num(); ++I)
		{
			DTHRigKeying::FBodyFit Fit;
			if (!DTHRigKeying::FitBody(Target.RigClass, &Mesh, LocalPose(Rig, Mesh, States[I]), Fit, Error)
				|| !DTHRigKeying::Key(Target, Target.Track, Frame + I, Fit.Values, {}, bStepped, Error)
				|| !DTHRigKeying::Key(Target, Target.Layer, Frame + I, {}, Fit.Layer, bStepped, Error))
			{
				OutLines.Add(FString::Printf(TEXT("ERROR: frame %d: %s"), Frame + I, *Error));
				return false;
			}
			// The layer makes every body bone exact; the rig's own reach is reported for information.
			OutLines.Add(FString::Printf(TEXT("Frame %d: keyed %d body controls of %s and %d bones on the FK layer; the layer corrects %d bones the rig leaves off (largest %.2f deg at %s)."),
				Frame + I, Fit.Values.Num(), *Target.RigClass->GetName(), Fit.Layer.Num(), Fit.Unreached.Num(), Fit.WorstDeg, *Fit.WorstBone.ToString()));
		}
		return true;
	}

	FString AssetName(const FString& Label)
	{
		return DTHPoseCharacter::AssetName(Label);
	}
}
