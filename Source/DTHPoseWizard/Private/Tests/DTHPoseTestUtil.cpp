#include "Tests/DTHPoseTestUtil.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Build/DTHPoseImport.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "Settings/DTHPoseSettings.h"

bool FDTHPoseTestContext::Load(FAutomationTestBase& Test)
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	Mesh = Cast<USkeletalMesh>(Settings->Character.TryLoad());
	if (!Mesh || !Mesh->GetSkeleton())
	{
		Test.AddError(TEXT("Set Character (Project Settings > Plugins > DTH Pose Wizard) to a DTH character's skeletal mesh."));
		return false;
	}
	Skeleton = Mesh->GetSkeleton();
	PoseAssetsDir = Settings->ExportFolder.Path / TEXT("Pose Assets");
	if (!IFileManager::Get().DirectoryExists(*PoseAssetsDir))
	{
		Test.AddError(FString::Printf(TEXT("Set DTH Export Folder to the character's export folder (no %s)."), *PoseAssetsDir));
		return false;
	}
	TestFolder = Settings->TestFolder.Path;
	if (!TestFolder.StartsWith(TEXT("/Game/")))
	{
		Test.AddError(TEXT("Set Test Folder to a content folder."));
		return false;
	}
	return true;
}

UAnimSequence* FDTHPoseTestContext::GroundTruth(FAutomationTestBase& Test, const FString& Kind, TArray<FString>& OutNames) const
{
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(PoseAssetsDir / (TEXT("*") + Kind + TEXT("PoseAsset.fbx"))), true, false);
	if (Files.Num() != 1)
	{
		Test.AddError(FString::Printf(TEXT("Expected one *%sPoseAsset.fbx in %s, found %d."), *Kind, *PoseAssetsDir, Files.Num()));
		return nullptr;
	}
	FString Error;
	if (!DTHPoseImport::LoadPoseNames(PoseAssetsDir / (Kind + TEXT("PoseAssetNames.txt")), OutNames, Error))
	{
		Test.AddError(Error);
		return nullptr;
	}
	const FString Folder = TestFolder / TEXT("Import");
	const FString PackageName = Folder / FPaths::GetBaseFilename(Files[0]);
	UAnimSequence* Sequence = nullptr;
	if (FPackageName::DoesPackageExist(PackageName))
	{
		Sequence = LoadObject<UAnimSequence>(nullptr, *(PackageName + TEXT(".") + FPaths::GetBaseFilename(Files[0])));
	}
	if (!Sequence)
	{
		Sequence = DTHPoseImport::ImportAnimation(PoseAssetsDir / Files[0], Folder, Skeleton, Error);
	}
	if (!Sequence)
	{
		Test.AddError(Error);
		return nullptr;
	}
	if (Sequence->GetDataModel()->GetNumberOfKeys() != OutNames.Num())
	{
		Test.AddWarning(FString::Printf(TEXT("%s has %d keys for %d pose names."), *Sequence->GetName(),
			Sequence->GetDataModel()->GetNumberOfKeys(), OutNames.Num()));
	}
	return Sequence;
}

namespace DTHPoseTest
{
	double AngleDeg(const FQuat& A, const FQuat& B)
	{
		return FMath::RadiansToDegrees(A.GetNormalized().AngularDistance(B.GetNormalized()));
	}

	TMap<FName, FTransform> TrackPose(const UAnimSequence& Sequence, int32 Key)
	{
		const IAnimationDataModel* Model = Sequence.GetDataModel();
		TArray<FName> Names;
		Model->GetBoneTrackNames(Names);
		TMap<FName, FTransform> Out;
		for (const FName& Name : Names)
		{
			Out.Add(Name, Model->GetBoneTrackTransform(Name, FFrameNumber(Key)));
		}
		return Out;
	}

	FString WriteReport(const FString& Name, const TArray<FString>& Lines)
	{
		const FString File = UDTHPoseSettings::SavedDir() / TEXT("phase0") / Name;
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
		FFileHelper::SaveStringArrayToFile(Lines, *File);
		return File;
	}

	void DeleteIfExists(const FString& PackageName)
	{
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			return;
		}
		if (UObject* Asset = LoadObject<UObject>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName))))
		{
			ObjectTools::ForceDeleteObjects({Asset}, false);
		}
	}
}

#endif
