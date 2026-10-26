#include "Build/DTHPoseRig.h"

#include "Animation/Skeleton.h"
#include "Build/DTHPoseWriter.h"
#include "Build/DTHRetarget.h"
#include "Engine/SkeletalMesh.h"
#include "Library/DTHPoseLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace DTHPoseRig
{
	DTHPoseSolve::FRig Make(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library)
	{
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		DTHPoseSolve::FRig Rig;
		Rig.DazRest = Library.State(Library.Rest);
		Rig.MeshRest = DTHPoseSolve::ComponentSpace(Ref, [&Ref](int32 I) { return Ref.GetRefBonePose()[I]; });
		Rig.Axes = DTHPoseSolve::DazToUE();
		Rig.Map = DTHPoseSolve::MapFromTable(Ref);
		// The library figure on the character: its proportions differ, so it is matched loosely (3 cm) and scaled. It only
		// places the few Daz joints DTH has no UE joint for.
		Rig.LibraryAxes = DTHPoseSolve::FitAxes(Rig.DazRest, Ref, Rig.MeshRest, 3.0);
		return Rig;
	}

	FDTHDazState Resolve(const UDTHPoseLibrary& Library, int32 Pose, int32 Base)
	{
		if (!Library.Poses.IsValidIndex(Pose) || !Library.Poses[Pose].bLoaded)
		{
			return Library.State(Library.Rest);
		}
		const FDTHPose& P = Library.Poses[Pose];
		if (!P.bPartial)
		{
			return Library.State(P);
		}
		const FDTHPose& Under = Library.Poses.IsValidIndex(Base) && Library.Poses[Base].bLoaded && Base != Pose ? Library.Poses[Base] : Library.Rest;
		return Library.Compose(Under, P);
	}

	TMap<FName, FTransform> LocalPose(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State)
	{
		// What the written track plays back as: the solve through the engine's retarget rule. It differs from the solve only
		// where DTH's retarget mode cannot carry a value (the pec clavicles' Skeleton mode keeps the bind translation).
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		TMap<FName, FTransform> Out = DTHPoseSolve::Solve(Rig, Ref, State);
		const TArray<DTHRetarget::FBone> Retarget = DTHRetarget::Gather(*Mesh.GetSkeleton(), Mesh);
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			FTransform& T = Out.FindChecked(Ref.GetBoneName(I));
			T = DTHRetarget::Forward(Retarget[I], DTHRetarget::Inverse(Retarget[I], T));
		}
		return Out;
	}

	TMap<FName, FTransform> Track(const DTHPoseSolve::FRig& Rig, const USkeletalMesh& Mesh, const FDTHDazState& State)
	{
		const FReferenceSkeleton& Ref = Mesh.GetRefSkeleton();
		const TMap<FName, FTransform> Wanted = DTHPoseSolve::Solve(Rig, Ref, State);
		const TArray<DTHRetarget::FBone> Retarget = DTHRetarget::Gather(*Mesh.GetSkeleton(), Mesh);
		TMap<FName, FTransform> Out;
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			Out.Add(Ref.GetBoneName(I), DTHRetarget::Inverse(Retarget[I], Wanted.FindChecked(Ref.GetBoneName(I))));
		}
		return Out;
	}

	UAnimSequence* Write(const USkeletalMesh& Mesh, const UDTHPoseLibrary& Library, TConstArrayView<FDTHDazState> Frames,
		const FString& Folder, const FString& Name, FString& OutError)
	{
		if (!Mesh.GetSkeleton() || Frames.Num() == 0)
		{
			OutError = TEXT("No character, or nothing to write.");
			return nullptr;
		}
		const DTHPoseSolve::FRig Rig = Make(Mesh, Library);
		TArray<TMap<FName, FTransform>> Tracks;
		for (const FDTHDazState& S : Frames) Tracks.Add(Track(Rig, Mesh, S));
		FString Unique = Name;
		for (int32 N = 2; FPackageName::DoesPackageExist(Folder / Unique); ++N) Unique = FString::Printf(TEXT("%s_%d"), *Name, N);
		// The sequence references the skeleton; the mesh is only read.
		return DTHPoseWriter::Create(Folder, Unique, const_cast<USkeleton*>(Mesh.GetSkeleton()), Tracks, OutError);
	}

	FString AssetName(const FString& Label)
	{
		// Letters, digits and underscores only: content paths reject . , & ! ~ @ # and more (labels like "L.H. Sword Pose"
		// or "!Reset Eyes" occur).
		FString Out;
		for (const TCHAR Ch : Label) Out.AppendChar(FChar::IsAlnum(Ch) || Ch == TEXT('_') || Ch == TEXT('-') ? Ch : TEXT('_'));
		while (Out.ReplaceInline(TEXT("__"), TEXT("_")) > 0) {}
		Out.TrimCharInline(TEXT('_'), nullptr);
		return Out.IsEmpty() ? FString(TEXT("Pose")) : Out.Left(100);
	}
}
