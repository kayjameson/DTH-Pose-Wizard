#include "Library/DTHPoseLibraryBuild.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DTHPoseLog.h"
#include "Library/DTHPoseLibrary.h"
#include "DTHPoseSamples.h"
#include "DTHFaceFitter.h"
#include "DTHPoseCharacter.h"
#include "Engine/SkeletalMesh.h"
#include "Settings/DTHPoseSettings.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#define LOCTEXT_NAMESPACE "DTHPoseLibraryBuild"

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

	void Fill(FDTHPoseData& Out, const TArray<FName>& Bones, const FDTHDazState& State)
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
			return TEXT("a face shape, not fitted (the samples have no face shapes, or no character was set)");
		}
		return TEXT("a shape DTH's ROM does not carry");
	}

	UDTHPoseLibrary* Build(const FString& SamplesFile, const FString& LibraryPath, TArray<FDTHPoseMessage>& OutReport)
	{
		USkeletalMesh* Character = Cast<USkeletalMesh>(GetDefault<UDTHPoseSettings>()->Character.TryLoad());
		return Build(SamplesFile, LibraryPath, Character, OutReport);
	}

	UDTHPoseLibrary* Build(const FString& SamplesFile, const FString& LibraryPath, USkeletalMesh* FaceCharacter, TArray<FDTHPoseMessage>& OutReport)
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
		// Built apart from the asset, which only takes the result: a failed or cancelled build leaves the library as it was.
		UDTHPoseLibrary* Built = NewObject<UDTHPoseLibrary>(GetTransientPackage());
		TStrongObjectPtr<UDTHPoseLibrary> Keep(Built);

		// Bones in a fixed order (sorted by name), so a rebuild from the same samples is identical.
		const FDTHDazState& UnrealPose = Samples.RomFrames[1];
		TArray<FName> Bones;
		UnrealPose.Bones.GetKeys(Bones);
		Bones.Sort(FNameLexicalLess());
		Built->BeginBuild();
		Built->Bones = Bones;
		Built->Parents.Reset();
		for (const FName& B : Bones) Built->Parents.Add(UnrealPose.Bones[B].Parent);
		Built->Rest = FDTHPose();
		Built->Rest.Label = TEXT("UnrealPose");
		Built->Rest.bLoaded = true;
		FDTHPoseData RestData;
		Fill(RestData, Bones, UnrealPose);
		Built->RestRotations = RestData.Rotations;
		Built->RestPositions = RestData.Positions;

		// The presets' faces: fitted on the character, as its face rig plays them (gaze included: a pose's eyes are its own).
		TUniquePtr<FDTHFaceFitter> Faces;

		if (Samples.HasFace())
		{
			if (!FaceCharacter)
			{
				Say(OutReport, ES::Warning, TEXT("The samples have the presets' faces, but no character is set (Settings, Character): the poses are built without their faces."));
			}
			else
			{
				Faces = MakeUnique<FDTHFaceFitter>();
				TArray<FString> FaceReport;
				if (!Faces->Init(FaceCharacter, Samples.Face, FaceReport, Error))
				{
					Say(OutReport, ES::Error, TEXT("Face fit: ") + Error);
					return nullptr;
				}
				for (const FString& Line : FaceReport) Say(OutReport, ES::Info, TEXT("Face fit: ") + Line);
				Built->FaceCharacter = FaceCharacter->GetPathName();

			}
		}
		else
		{
			Say(OutReport, ES::Warning, TEXT("The samples have no face shapes (sampled before the Pose Wizard 0.4.0 sampler): the poses are built without their faces. Sample again to carry eyes, lids, jaw, lips and tongue."));
		}
		double FaceSeconds = 0.0, FaceMatchSum = 0.0;
		int32 FaceCount = 0;
		TArray<FString> FaceLow;

		// The samples come one file at a time and go straight into the library's pose data.
		FScopedSlowTask Task(float(FMath::Max(Samples.NumPoses, 1)), FText::Format(LOCTEXT("Reading", "Building the pose library from {0} presets..."), FText::AsNumber(Samples.NumPoses)));
		Task.MakeDialog(true);
		bool bCancelled = false;
		TArray<FString> NotLoaded;
		const bool bRead = Samples.ReadPoses([&](FDTHDazPose&& P)
		{
			Task.EnterProgressFrame(1.f);
			if (Task.ShouldCancel())
			{
				bCancelled = true;
				return false;
			}
			FDTHPose Pose;
			Pose.Pack = MoveTemp(P.Pack);
			Pose.Label = MoveTemp(P.Label);
			Pose.SourceFile = MoveTemp(P.File);
			Pose.IconFile = MoveTemp(P.Icon);
			Pose.Type = MoveTemp(P.Type);
			Pose.bLoaded = P.bLoaded;
			Pose.Reason = MoveTemp(P.Reason);
			if (!P.bLoaded)
			{
				NotLoaded.Add(FString::Printf(TEXT("%s: %s."), *Pose.Label, *Pose.Reason));
				Built->AddPose(MoveTemp(Pose), nullptr);
				return true;
			}
			FDTHPoseData Data;
			Fill(Data, Bones, P.State);
			Data.Moved = MovedBones(Bones, P.State, Samples.Rest);
			Pose.bPartial = Pose.Type == TEXT("preset_hierarchical_pose") || !MovesTrunk(Bones, Data.Moved);
			// the face: fitted from the face Daz shows, so its morphs and face bones are carried by the curves
			bool bFaceFitted = false;
			if (Faces && !P.Face.IsEmpty())
			{
				FDTHFaceFitter::FResult Face;
				FDTHShapeFit::FOptions Options;
				if (Faces->Fit(P.Face, Options, Face))
				{
					bFaceFitted = true;
					if (!Face.Curves.IsEmpty() || !Face.Bones.IsEmpty())
					{
						TArray<FName> Names;
						Face.Curves.GetKeys(Names);
						Names.Sort(FNameLexicalLess());
						for (const FName& N : Names) { Pose.FaceCurves.Add(N); Pose.FaceValues.Add(Face.Curves[N]); }
						TArray<FName> BoneNames;
						Face.Bones.GetKeys(BoneNames);
						BoneNames.Sort(FNameLexicalLess());
						for (const FName& N : BoneNames) { Pose.FaceBoneNames.Add(N); Pose.FaceBoneTurns.Add(Face.Bones[N]); }
						Pose.FaceMatch = static_cast<float>(Face.Match);
						FaceSeconds += Face.Seconds;
						FaceMatchSum += Face.Match;
						++FaceCount;
						if (Face.Match < 0.9) FaceLow.Add(FString::Printf(TEXT("%s: face %.0f%% (it moves up to %.1f mm)"), *Pose.Label, 100.0 * Face.Match, 10.0 * Face.Largest));
					}
				}
			}
			TArray<FName> Morphs;
			P.State.Morphs.GetKeys(Morphs);
			Morphs.Sort(FNameLexicalLess());
			for (const FName& M : Morphs)
			{
				const float Changed = P.State.Morphs[M] - Samples.Rest.Morphs.FindRef(M);
				FString Reason = FMath::Abs(Changed) > 0.01f ? MissingReason(M.ToString()) : FString();
				const bool bFaceMorph = M.ToString().StartsWith(TEXT("facs_")) || M.ToString().StartsWith(TEXT("head_"));
				if (bFaceMorph && bFaceFitted) Reason.Reset();   // carried by the face curves
				if (!Reason.IsEmpty()) Pose.Missing.Add(FString::Printf(TEXT("%s %.2f: %s"), *M.ToString(), P.State.Morphs[M], *Reason));
			}
			Built->AddPose(MoveTemp(Pose), &Data);
			return true;
		}, Error);
		Built->EndBuild();
		if (!bRead || bCancelled)
		{
			Say(OutReport, bCancelled ? ES::Warning : ES::Error, bCancelled ? FString(TEXT("Build cancelled: the library is as it was.")) : Error);
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
			// Not transactional: an undo record of a library of hundreds of thousands of poses would hold all of it.
			Library = NewObject<UDTHPoseLibrary>(Package, *AssetName, RF_Public | RF_Standalone);
		}
		Library->TakeBuild(*Built);
		Library->Skipped = Samples.Skipped;
		Library->SamplesFile = SamplesFile;
		Library->SampledAt = Samples.SampledAt;
		Library->DazVersion = Samples.DazVersion;

		if (bNew) FAssetRegistryModule::AssetCreated(Library);
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		// a failed save is reported, not fatal (the default error device stops the editor)
		Args.Error = GWarn;
		const FString File = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		// a library loaded from this file has a loader that still holds it open: let go of it before writing
		ResetLoadersForSave(Package, *File);
		if (!UPackage::SavePackage(Package, Library, *File, Args))
		{
			// The build took a long time: it is not thrown away when the library's file cannot be written (another program
			// holding it, a read-only file). The library moves into a new package beside it.
			const FString Recovered = PackageName + TEXT("_Recovered_") + FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
			UPackage* RecoveredPackage = CreatePackage(*Recovered);
			Library->Rename(*FPackageName::GetShortName(Recovered), RecoveredPackage, REN_DontCreateRedirectors | REN_NonTransactional);
			FAssetRegistryModule::AssetCreated(Library);
			RecoveredPackage->MarkPackageDirty();
			if (!UPackage::SavePackage(RecoveredPackage, Library, *FPackageName::LongPackageNameToFilename(Recovered, FPackageName::GetAssetPackageExtension()), Args))
			{
				Say(OutReport, ES::Error, FString::Printf(TEXT("Could not save %s, nor %s beside it."), *PackageName, *Recovered));
				return nullptr;
			}
			Say(OutReport, ES::Error, FString::Printf(TEXT("Could not save %s (is the file open in another program?). The built library was saved as %s instead: use that one, or rename it."),
				*PackageName, *Recovered));
		}
		Say(OutReport, ES::Info, FString::Printf(TEXT("Library %s: %d poses (%d partial, laid over a base pose), %d not loaded in Daz, %d folders or files skipped. Sampled %s, Daz %s."),
			*PackageName, Library->NumLoaded, Library->NumPartial, NotLoaded.Num(), Library->Skipped.Num(), *Library->SampledAt, *Library->DazVersion));
		if (FaceCount > 0)
		{
			Say(OutReport, ES::Info, FString::Printf(TEXT("%d poses set a face (eyes, lids, jaw, lips, tongue, brows), fitted on %s: %.1f%% of the Daz face's movement on average, %.1f s in all."),
				FaceCount, *FPaths::GetBaseFilename(Library->FaceCharacter), 100.0 * FaceMatchSum / FaceCount, FaceSeconds));
			for (const FString& Line : FaceLow) OutReport.Add({ES::Info, Line});
		}
		if (Library->NumWithMissing > 0)
		{
			Say(OutReport, ES::Warning, FString::Printf(TEXT("%d poses also set shapes a DTH character does not take from a body pose (listed per pose)."), Library->NumWithMissing));
		}
		for (const FString& Line : NotLoaded) OutReport.Add({ES::Warning, Line});
		return Library;
	}
}

#undef LOCTEXT_NAMESPACE
