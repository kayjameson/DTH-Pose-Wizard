#include "Library/DTHPoseLibraryBuild.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DTHPoseLog.h"
#include "Library/DTHPoseLibrary.h"
#include "Library/DTHPoseSamples.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	void Say(TArray<FDTHPoseMessage>& Report, FDTHPoseMessage::ESeverity Severity, const FString& Text)
	{
		Report.Add({Severity, Text});
		UE_LOG(LogDTHPose, Log, TEXT("%s"), *Text);
	}

	/** The bones a sampled state's local channels set away from the rest pose's (0.01 deg / 0.01 cm). */
	TArray<int32> MovedBones(const TArray<FName>& Bones, const FDTHDazState& State, const FDTHDazState& Rest)
	{
		TArray<int32> Out;
		for (int32 I = 0; I < Bones.Num(); ++I)
		{
			const FDTHDazBone* A = State.Bones.Find(Bones[I]);
			const FDTHDazBone* B = Rest.Bones.Find(Bones[I]);
			if (!A || !B) continue;
			for (int32 C = 0; C < 6; ++C)
			{
				if (FMath::Abs(A->Local[C] - B->Local[C]) > 0.01f) { Out.Add(I); break; }
			}
		}
		return Out;
	}

	/** Bones of the trunk and the limb roots: a preset that moves none of them is a partial (hand, foot, face) pose. */
	bool MovesTrunk(const TArray<FName>& Bones, const TArray<int32>& Moved)
	{
		static const TSet<FName> Trunk = {TEXT("hip"), TEXT("pelvis"), TEXT("spine1"), TEXT("spine2"), TEXT("spine3"), TEXT("spine4"),
			TEXT("neck1"), TEXT("l_thigh"), TEXT("r_thigh"), TEXT("l_upperarm"), TEXT("r_upperarm"), TEXT("l_shoulder"), TEXT("r_shoulder")};
		for (const int32 M : Moved) if (Trunk.Contains(Bones[M])) return true;
		return false;
	}

	void Fill(FDTHPose& Out, const TArray<FName>& Bones, const FDTHDazState& State)
	{
		Out.Rotations.SetNum(Bones.Num());
		Out.Positions.SetNum(Bones.Num());
		for (int32 I = 0; I < Bones.Num(); ++I)
		{
			const FDTHDazBone* B = State.Bones.Find(Bones[I]);
			Out.Rotations[I] = B ? B->Rot : FQuat::Identity;
			Out.Positions[I] = B ? B->Pos : FVector::ZeroVector;
		}
	}
}

namespace DTHPoseLibraryBuild
{
	FString MissingReason(const FString& Morph)
	{
		// Pose controls drive bones (Daz ERC): the bone rotations carry them.
		if (Morph.Contains(TEXT("_ctrl_"))) return FString();
		// Correctives Daz drives from the bones (its own and add-ons'): DTH drives its correctives from the same bones in
		// the post-process blueprint (pose drivers, DTH.Pose.P0.JcmFrames).
		if (Morph.Contains(TEXT("_cbs_"))) return FString();
		if (Morph.StartsWith(TEXT("facs_")) || Morph.StartsWith(TEXT("head_")))
		{
			return TEXT("a face shape: key the face with the DTH Expression Wizard");
		}
		return TEXT("a shape DTH's ROM does not carry");
	}

	UDTHPoseLibrary* Build(const FString& SamplesFile, const FString& LibraryPath, TArray<FDTHPoseMessage>& OutReport)
	{
		using ES = FDTHPoseMessage::ESeverity;
		FDTHPoseSamples Samples;
		FString Error;
		if (!Samples.Load(SamplesFile, Error))
		{
			Say(OutReport, ES::Error, Error);
			return nullptr;
		}
		if (Samples.RomFrames.Num() < 2)
		{
			Say(OutReport, ES::Error, TEXT("The samples have no DTH ROM (its frame 1, UnrealPose, is the rest pose): run the sampler's rom step."));
			return nullptr;
		}
		const FString PackageName = FPackageName::ObjectPathToPackageName(LibraryPath);
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			Say(OutReport, ES::Error, FString::Printf(TEXT("%s is not a content path for the library."), *LibraryPath));
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		const FString AssetName = FPackageName::GetShortName(PackageName);
		UDTHPoseLibrary* Library = FindObject<UDTHPoseLibrary>(Package, *AssetName);
		if (!Library && FPackageName::DoesPackageExist(PackageName))
		{
			Library = LoadObject<UDTHPoseLibrary>(nullptr, *(PackageName + TEXT(".") + AssetName));
		}
		const bool bNew = !Library;
		if (bNew)
		{
			Library = NewObject<UDTHPoseLibrary>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		}
		Library->Modify();

		// Bones in a fixed order (sorted by name), so a rebuild from the same samples is identical.
		const FDTHDazState& UnrealPose = Samples.RomFrames[1];
		TArray<FName> Bones;
		UnrealPose.Bones.GetKeys(Bones);
		Bones.Sort(FNameLexicalLess());
		Library->Bones = Bones;
		Library->Parents.Reset();
		for (const FName& B : Bones) Library->Parents.Add(UnrealPose.Bones[B].Parent);
		Library->Rest = FDTHPose();
		Library->Rest.Label = TEXT("UnrealPose");
		Library->Rest.bLoaded = true;
		Fill(Library->Rest, Bones, UnrealPose);

		Library->Poses.Reset();
		int32 Partial = 0, WithMissing = 0, NotLoaded = 0;
		for (const FDTHDazPose& P : Samples.Poses)
		{
			FDTHPose& Pose = Library->Poses.AddDefaulted_GetRef();
			Pose.Pack = P.Pack;
			Pose.Label = P.Label;
			Pose.SourceFile = P.File;
			Pose.IconFile = P.Icon;
			Pose.Type = P.Type;
			Pose.bLoaded = P.bLoaded;
			Pose.Reason = P.Reason;
			if (!P.bLoaded)
			{
				++NotLoaded;
				continue;
			}
			Fill(Pose, Bones, P.State);
			Pose.Moved = MovedBones(Bones, P.State, Samples.Rest);
			Pose.bPartial = P.Type == TEXT("preset_hierarchical_pose") || !MovesTrunk(Bones, Pose.Moved);
			Partial += Pose.bPartial ? 1 : 0;
			TArray<FName> Morphs;
			P.State.Morphs.GetKeys(Morphs);
			Morphs.Sort(FNameLexicalLess());
			for (const FName& M : Morphs)
			{
				const float Changed = P.State.Morphs[M] - Samples.Rest.Morphs.FindRef(M);
				const FString Reason = FMath::Abs(Changed) > 0.01f ? MissingReason(M.ToString()) : FString();
				if (!Reason.IsEmpty()) Pose.Missing.Add(FString::Printf(TEXT("%s %.2f: %s"), *M.ToString(), P.State.Morphs[M], *Reason));
			}
			WithMissing += Pose.Missing.IsEmpty() ? 0 : 1;
		}
		Library->Skipped = Samples.Skipped;
		Library->SamplesFile = SamplesFile;
		Library->SampledAt = Samples.SampledAt;
		Library->DazVersion = Samples.DazVersion;

		if (bNew) FAssetRegistryModule::AssetCreated(Library);
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Library, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Args))
		{
			Say(OutReport, ES::Error, FString::Printf(TEXT("Could not save %s."), *PackageName));
			return nullptr;
		}
		Say(OutReport, ES::Info, FString::Printf(TEXT("Library %s: %d poses (%d partial, laid over a base pose), %d not loaded in Daz, %d folders or files skipped. Sampled %s, Daz %s."),
			*PackageName, Library->Poses.Num() - NotLoaded, Partial, NotLoaded, Library->Skipped.Num(), *Library->SampledAt, *Library->DazVersion));
		if (WithMissing > 0)
		{
			Say(OutReport, ES::Warning, FString::Printf(TEXT("%d poses also set shapes a DTH character does not take from a body pose (listed per pose)."), WithMissing));
		}
		for (const FDTHPose& Pose : Library->Poses)
		{
			if (!Pose.bLoaded) Say(OutReport, ES::Warning, FString::Printf(TEXT("%s: %s."), *Pose.Label, *Pose.Reason));
		}
		return Library;
	}
}
