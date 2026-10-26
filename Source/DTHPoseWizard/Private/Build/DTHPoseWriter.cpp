#include "Build/DTHPoseWriter.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DTHPoseLog.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

#define LOCTEXT_NAMESPACE "DTHPoseWriter"

namespace DTHPoseWriter
{
	FFrameRate FrameRate()
	{
		return FFrameRate(30, 1);
	}

	UAnimSequence* Create(const FString& Folder, const FString& Name, USkeleton* Skeleton,
		TConstArrayView<TMap<FName, FTransform>> Frames, FString& OutError)
	{
		if (!Skeleton || Frames.Num() == 0)
		{
			OutError = TEXT("No skeleton or no frames to write.");
			return nullptr;
		}
		const FString PackageName = Folder / Name;
		if (!FPackageName::IsValidLongPackageName(PackageName, false) || FPackageName::DoesPackageExist(PackageName))
		{
			OutError = FString::Printf(TEXT("Cannot create %s (not a valid content path, or it already exists)."), *PackageName);
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UAnimSequence* Sequence = NewObject<UAnimSequence>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
		Sequence->SetSkeleton(Skeleton);
		// Lossless keys (the engine's recorder settings: full-float rotations, raw translations). The default compression
		// moves a pose by up to 0.03 deg (DTH.Pose.P1.WriteAndPlay); a pose is a frame or a few, so size does not matter.
		if (UAnimBoneCompressionSettings* Lossless = LoadObject<UAnimBoneCompressionSettings>(nullptr, TEXT("/Engine/Animation/DefaultRecorderBoneCompression.DefaultRecorderBoneCompression")))
		{
			Sequence->BoneCompressionSettings = Lossless;
		}

		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		const int32 Keys = Frames.Num();
		IAnimationDataController& Controller = Sequence->GetController();
		Controller.InitializeModel();
		Controller.OpenBracket(LOCTEXT("CreatePose", "Create pose sequence"), false);
		Controller.SetFrameRate(FrameRate(), false);
		// A sequence of N keys spans N - 1 frames; a single pose is one key on frame 0.
		Controller.SetNumberOfFrames(FFrameNumber(FMath::Max(Keys - 1, 1)), false);
		const int32 ModelKeys = Sequence->GetDataModel()->GetNumberOfKeys();
		for (int32 Bone = 0; Bone < Ref.GetNum(); ++Bone)
		{
			const FName BoneName = Ref.GetBoneName(Bone);
			const FTransform& RefPose = Ref.GetRefBonePose()[Bone];
			TArray<FVector> Positions; Positions.SetNum(ModelKeys);
			TArray<FQuat> Rotations; Rotations.SetNum(ModelKeys);
			TArray<FVector> Scales; Scales.SetNum(ModelKeys);
			for (int32 K = 0; K < ModelKeys; ++K)
			{
				const FTransform* Given = Frames[FMath::Min(K, Keys - 1)].Find(BoneName);
				const FTransform& T = Given ? *Given : RefPose;
				Positions[K] = T.GetTranslation();
				Rotations[K] = T.GetRotation();
				Scales[K] = T.GetScale3D();
			}
			Controller.AddBoneCurve(BoneName, false);
			Controller.SetBoneTrackKeys(BoneName, Positions, Rotations, Scales, false);
		}
		// UE 5.8 keeps a sequence's bone rotations as float Euler angles (UAnimSequencerController::SetBoneCurveKeys,
		// FQuat4f::Euler), which loses up to ~0.03 deg near +-90 deg pitch (DTH's pec and IK bones rest there). Every
		// sequence in the engine is stored this way; measured in DTH.Pose.P1.WriteAndPlay.
		Controller.CloseBracket(false);
		Controller.NotifyPopulated();

		FAssetRegistryModule::AssetCreated(Sequence);
		Package->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		UPackage::SavePackage(Package, Sequence, *FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), SaveArgs);
		return Sequence;
	}
}

#undef LOCTEXT_NAMESPACE
