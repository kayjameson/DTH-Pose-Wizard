#include "Library/DTHPoseLibrary.h"

FDTHDazState UDTHPoseLibrary::State(const FDTHPose& Pose) const
{
	FDTHDazState Out;
	for (int32 I = 0; I < Bones.Num() && I < Pose.Rotations.Num(); ++I)
	{
		FDTHDazBone& B = Out.Bones.Add(Bones[I]);
		B.Parent = Parents[I];
		B.Rot = Pose.Rotations[I];
		B.Pos = Pose.Positions[I];
	}
	return Out;
}

FDTHDazState UDTHPoseLibrary::Compose(const FDTHPose& Base, const FDTHPose& Overlay) const
{
	TArray<bool> bMoved;
	bMoved.Init(false, Bones.Num());
	for (const int32 M : Overlay.Moved) if (bMoved.IsValidIndex(M)) bMoved[M] = true;
	TMap<FName, int32> Index;
	for (int32 I = 0; I < Bones.Num(); ++I) Index.Add(Bones[I], I);

	// Parents first: each bone keeps its pose relative to its parent from whichever preset sets it.
	TArray<FQuat> Rot;
	TArray<FVector> Pos;
	Rot.SetNum(Bones.Num());
	Pos.SetNum(Bones.Num());
	TArray<uint8> Done;
	Done.Init(0, Bones.Num());
	TFunction<void(int32)> Place = [&](int32 I)
	{
		if (Done[I]) return;
		const FDTHPose& From = bMoved[I] ? Overlay : Base;
		const int32* P = Parents[I].IsNone() ? nullptr : Index.Find(Parents[I]);
		if (!P)
		{
			Rot[I] = From.Rotations[I];
			Pos[I] = From.Positions[I];
		}
		else
		{
			Place(*P);
			const FQuat LocalRot = From.Rotations[*P].Inverse() * From.Rotations[I];
			const FVector LocalPos = From.Rotations[*P].UnrotateVector(From.Positions[I] - From.Positions[*P]);
			Rot[I] = (Rot[*P] * LocalRot).GetNormalized();
			Pos[I] = Pos[*P] + Rot[*P].RotateVector(LocalPos);
		}
		Done[I] = 1;
	};
	FDTHDazState Out;
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		Place(I);
		FDTHDazBone& B = Out.Bones.Add(Bones[I]);
		B.Parent = Parents[I];
		B.Rot = Rot[I];
		B.Pos = Pos[I];
	}
	return Out;
}

int32 UDTHPoseLibrary::FindPose(const FString& Label) const
{
	return Poses.IndexOfByPredicate([&Label](const FDTHPose& P) { return P.Label == Label; });
}
