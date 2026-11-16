#include "DTHFaceBones.h"

#include "DTHShapeFit.h"
#include "ReferenceSkeleton.h"

namespace DTHFaceBones
{
	bool IsChannel(FName Name)
	{
		return Name.ToString().StartsWith(TEXT("bone|"));
	}

	void Split(const TMap<FName, float>& Values, TMap<FName, float>& OutCurves, TMap<FName, FQuat>& OutTurns)
	{
		OutCurves.Reset();
		OutTurns.Reset();
		TMap<FName, FVector> Turn;
		for (const TPair<FName, float>& V : Values)
		{
			if (!IsChannel(V.Key)) { OutCurves.Add(V.Key, V.Value); continue; }
			TArray<FString> Parts;
			V.Key.ToString().ParseIntoArray(Parts, TEXT("|"));
			if (Parts.Num() != 3) continue;
			const int32 Axis = FCString::Atoi(*Parts[2]);
			if (Axis < 0 || Axis > 2) continue;
			Turn.FindOrAdd(FName(*Parts[1]))[Axis] = V.Value;
		}
		for (const TPair<FName, FVector>& T : Turn)
		{
			OutTurns.Add(T.Key, FQuat::MakeFromRotationVector(T.Value * FMath::DegreesToRadians(FDTHShapeFit::BoneUnitDeg)));
		}
	}

	TMap<FName, FTransform> Local(const FReferenceSkeleton& Ref, const TMap<FName, FQuat>& Turns)
	{
		TMap<FName, FTransform> Out;
		for (const TPair<FName, FQuat>& T : Turns)
		{
			const int32 I = Ref.FindBoneIndex(T.Key);
			if (I == INDEX_NONE) continue;
			FTransform L = Ref.GetRefBonePose()[I];
			L.SetRotation(L.GetRotation() * T.Value);
			Out.Add(T.Key, L);
		}
		return Out;
	}
}
