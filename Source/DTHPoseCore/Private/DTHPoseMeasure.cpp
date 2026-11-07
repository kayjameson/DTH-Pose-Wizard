#include "DTHPoseMeasure.h"

#include "DTHBodyHarness.h"
#include "ReferenceSkeleton.h"

namespace DTHPoseMeasure
{
	FString FError::Describe() const
	{
		return FString::Printf(TEXT("joints max %.4f cm (%s); bones max %.4f deg (%s); twist bones max %.4f deg (%s); %d bones"),
			MaxCm, *CmBone.ToString(), MaxDeg, *DegBone.ToString(), MaxTwistDeg, *TwistBone.ToString(), Bones);
	}

	FError Compare(const DTHPoseSolve::FRig& Rig, const FReferenceSkeleton& Ref, const FDTHBoneReadback& Played,
		const FDTHDazState& OwnRest, const FDTHDazState& OwnNow, TArray<FString>* Lines, const FString& Label)
	{
		FError E;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const FName Bone = Ref.GetBoneName(I);
			const FName DazName = Rig.Map.Daz.IsValidIndex(I) ? Rig.Map.Daz[I] : NAME_None;
			if (DTHPoseSolve::IsSkipped(Bone) || DazName.IsNone()) continue;
			const FDTHDazBone* Rest = OwnRest.Bones.Find(DazName);
			const FName Carrier = Rest && !Rest->Parent.IsNone() ? Rest->Parent : DazName;
			const FDTHDazBone* CarrierNow = OwnNow.Bones.Find(Carrier);
			const FDTHDazBone* CarrierRest = OwnRest.Bones.Find(Carrier);
			const FDTHDazBone* Now = OwnNow.Bones.Find(DazName);
			const int32 P = Played.Find(Bone);
			if (!Now || !Rest || !CarrierNow || !CarrierRest || P == INDEX_NONE) continue;
			const FQuat CarrierDelta = Rig.Axes.Rotation(CarrierNow->Rot) * Rig.Axes.Rotation(CarrierRest->Rot).Inverse();
			const FVector DazPos = Rig.Axes.Position(CarrierNow->Pos)
				+ CarrierDelta.RotateVector(Rig.MeshRest[I].GetTranslation() - Rig.Axes.Position(CarrierRest->Pos));
			const FQuat DazRot = Rig.Axes.Rotation(Now->Rot) * Rig.Axes.Rotation(Rest->Rot).Inverse() * Rig.MeshRest[I].GetRotation();
			const double Cm = FVector::Dist(Played.Component[P].GetTranslation(), DazPos);
			const double Deg = FMath::RadiansToDegrees(Played.Component[P].GetRotation().AngularDistance(DazRot));
			if (Lines) Lines->Add(FString::Printf(TEXT("%s\t%s\t%s\t%.4f\t%.4f"), *Label, *Bone.ToString(), *DazName.ToString(), Cm, Deg));
			++E.Bones;
			if (Cm > E.MaxCm) { E.MaxCm = Cm; E.CmBone = Bone; }
			if (Bone.ToString().Contains(TEXT("twist"))) { if (Deg > E.MaxTwistDeg) { E.MaxTwistDeg = Deg; E.TwistBone = Bone; } }
			else if (Deg > E.MaxDeg) { E.MaxDeg = Deg; E.DegBone = Bone; }
		}
		return E;
	}
}
