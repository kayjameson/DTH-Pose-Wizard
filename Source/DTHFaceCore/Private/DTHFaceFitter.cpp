#include "DTHFaceFitter.h"

#include "DTHFaceRegion.h"
#include "DTHFaceRigProbe.h"
#include "DTHRigKeying.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/ScopeExit.h"

bool FDTHFaceFitter::Init(USkeletalMesh* Character, const TArray<FDTHFaceFigure>& Face, TArray<FString>& OutReport, FString& OutError)
{
	if (!Character)
	{
		OutError = TEXT("No character to fit faces on.");
		return false;
	}
	bool bMeasured = false;
	if (!FDTHFaceRigProbe::GetOrMeasure(Character, FaceRig, bMeasured, OutError)) return false;
	OutReport.Add(FString::Printf(TEXT("%s the face rig of %s."), bMeasured ? TEXT("Measured") : TEXT("Using the measurement of"), *Character->GetName()));
	TArray<FDTHShapeFit::FBoardCurve> Board;
	for (const DTHRigKeying::FBoardSlot& S : DTHRigKeying::BoardMap(DTHRigKeying::DefaultRig(Character))) Board.Add({S.Curve, S.Control, S.Axis, S.Sign});
	return FaceEvaluator.Init(Character, OutError) && Target.Build(Face, FaceEvaluator, OutReport, OutError)
		&& ShapeFit.Init(FaceEvaluator, Board, FaceRig, OutReport, OutError, FaceEvaluator.FaceMorphCurves(), TurnableBones(*Character));
}

TArray<FName> FDTHFaceFitter::TurnableBones(const USkeletalMesh& Character)
{
	TArray<FName> Out;
	const FReferenceSkeleton& Ref = Character.GetRefSkeleton();
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const FString Name = Ref.GetBoneName(I).ToString();
		if (Name == TEXT("eye_l") || Name == TEXT("eye_r") || Name == TEXT("lower_jaw") || Name.StartsWith(TEXT("tongue_"))) Out.Add(Ref.GetBoneName(I));
	}
	return Out;
}

TSet<FName> FDTHFaceFitter::GazeCurves() const
{
	TSet<FName> Out;
	for (const FName& C : ShapeFit.Curves())
	{
		const FString Name = C.ToString();
		if ((Name.Contains(TEXT("_eyeLook")) && !Name.Contains(TEXT("Parallel"))) || Name.StartsWith(TEXT("bone|eye_"))) Out.Add(C);
	}
	return Out;
}

bool FDTHFaceFitter::Fit(const TArray<TArray<FVector3f>>& Displacement, const FDTHShapeFit::FOptions& InOptions, FResult& Out,
	const TMap<FName, float>* Start, const TMap<FName, FTransform>* BaseBones)
{
	Out = FResult();
	TArray<FVector3f> T;
	if (!Target.Target(Displacement, T)) return false;
	ShapeFit.SetExternalBones(BaseBones ? *BaseBones : TMap<FName, FTransform>());
	ON_SCOPE_EXIT { ShapeFit.SetExternalBones({}); FaceEvaluator.SetBaseBones({}); };
	for (const FVector3f& P : T) Out.Largest = FMath::Max(Out.Largest, static_cast<double>(P.Size()));
	if (Out.Largest < RestTolerance)
	{
		Out.Target = MoveTemp(T);
		if (BaseBones && !BaseBones->IsEmpty()) ShapeFit.Displacement({}, Out.Got);
		else Out.Got.Init(FVector3f::ZeroVector, Out.Target.Num());
		return true;
	}
	FDTHShapeFit::FOptions Options = InOptions;
	if (Start && !Start->IsEmpty())
	{
		Options.Start.Init(0.0, ShapeFit.Curves().Num());
		for (int32 K = 0; K < ShapeFit.Curves().Num(); ++K) Options.Start[K] = Start->FindRef(ShapeFit.Curves()[K]);
	}
	const FDTHShapeFit::FResult R = ShapeFit.Fit(T, Options);
	for (int32 K = 0; K < R.Values.Num(); ++K)
	{
		if (R.Values[K] != 0.0f && !ShapeFit.IsBone(K)) Out.Curves.Add(ShapeFit.Curves()[K], R.Values[K]);
	}
	Out.Bones = ShapeFit.BoneTurns(R.Values);
	for (int32 K = 0; K < R.Values.Num(); ++K) if (R.Values[K] != 0.0f) Out.Values.Add(ShapeFit.Curves()[K], R.Values[K]);
	Out.Match = R.Match;
	Out.Seconds = R.Seconds;
	Out.Target = MoveTemp(T);
	ShapeFit.Displacement(R.Values, Out.Got);
	return true;
}

const TCHAR* FDTHFaceFitter::RegionOf(FName Bone)
{
	const FString B = Bone.ToString();
	if (B.StartsWith(TEXT("brow"))) return TEXT("brows");
	if (B.StartsWith(TEXT("eyelid_upper"))) return TEXT("upper lids");
	if (B.StartsWith(TEXT("eyelid_lower")) || B.StartsWith(TEXT("squint"))) return TEXT("lower lids");
	if (B.StartsWith(TEXT("eye_")) || B == TEXT("eye_l") || B == TEXT("eye_r")) return TEXT("eyes");
	if (B.StartsWith(TEXT("nose")) || B.StartsWith(TEXT("nostril"))) return TEXT("nose");
	if (B.StartsWith(TEXT("cheek"))) return TEXT("cheeks");
	if (B.StartsWith(TEXT("lip_corner"))) return TEXT("lip corners");
	if (B.StartsWith(TEXT("lip"))) return TEXT("lips");
	if (B == TEXT("lower_jaw") || B == TEXT("chin") || B.StartsWith(TEXT("jaw"))) return TEXT("jaw and chin");
	if (B.Contains(TEXT("teeth"))) return TEXT("teeth and gums");
	if (B.StartsWith(TEXT("tongue"))) return TEXT("tongue");
	return TEXT("rest of the head");
}

FString FDTHFaceFitter::RegionReport(const FResult& Result, double& OutWorstMm, FString& OutWorstRegion) const
{
	struct FStats { TArray<float> Errors; float Moves = 0.0f; };
	TMap<FString, FStats> ByRegion;
	const int32 N = FMath::Min(Result.Target.Num(), Result.Got.Num());
	for (int32 P = 0; P < N; ++P)
	{
		FStats& S = ByRegion.FindOrAdd(RegionOf(FaceEvaluator.DominantBone(P)));
		S.Errors.Add(10.0f * FVector3f::Dist(Result.Got[P], Result.Target[P]));
		S.Moves = FMath::Max(S.Moves, 10.0f * Result.Target[P].Size());
	}
	ByRegion.KeySort(TLess<FString>());
	FString Out;
	OutWorstMm = 0.0;
	OutWorstRegion.Reset();
	for (TPair<FString, FStats>& R : ByRegion)
	{
		if (R.Value.Moves < 0.5f) continue;
		R.Value.Errors.Sort();
		const float Largest = R.Value.Errors.Last();
		const float P95 = R.Value.Errors[FMath::Min(R.Value.Errors.Num() - 1, R.Value.Errors.Num() * 95 / 100)];
		int32 Over = 0;
		for (const float E : R.Value.Errors) Over += E > 1.0f ? 1 : 0;
		Out += FString::Printf(TEXT(" %s %.1f/%.1f mm (Daz moves %.1f%s);"), *R.Key, Largest, P95, R.Value.Moves,
			Over ? *FString::Printf(TEXT(", %d of %d points over 1 mm"), Over, R.Value.Errors.Num()) : TEXT(""));
		if (Largest > OutWorstMm) { OutWorstMm = Largest; OutWorstRegion = R.Key; }
	}
	return Out;
}

TMap<FName, FTransform> FDTHFaceFitter::FaceBones(const TMap<FName, FTransform>& LocalPose) const
{
	TMap<FName, FTransform> Out;
	const USkeletalMesh* Mesh = FaceEvaluator.GetMesh();
	if (!Mesh) return Out;
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	// The eyeballs: a sequence keys them itself (any gaze, where DTH's eye-look poses stop at 20 deg up or down); the
	// rest of the face is the curves' (its bones at the reference pose: DTHPoseCharacter::IsCurveFaceBone).
	for (const TCHAR* Eye : {TEXT("eye_l"), TEXT("eye_r")})
	{
		if (const FTransform* T = LocalPose.Find(Eye)) Out.Add(Eye, *T);
	}
	return Out;
}
