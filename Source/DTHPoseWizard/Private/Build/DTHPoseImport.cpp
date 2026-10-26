#include "Build/DTHPoseImport.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "InterchangeGenericAnimationPipeline.h"
#include "InterchangeGenericAssetsPipeline.h"
#include "InterchangeGenericAssetsPipelineSharedSettings.h"
#include "InterchangeGenericMaterialPipeline.h"
#include "InterchangeGenericMeshPipeline.h"
#include "InterchangeManager.h"
#include "InterchangeSourceData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace DTHPoseImport
{
	UAnimSequence* ImportAnimation(const FString& File, const FString& Folder, USkeleton* Skeleton, FString& OutError)
	{
		if (!FPaths::FileExists(File))
		{
			OutError = FString::Printf(TEXT("%s does not exist."), *File);
			return nullptr;
		}
		// The settings of DTH's pose-asset pipeline (DazToHuePoseAssetPipeline): animations only, bone tracks, the
		// file's own timeline, no resampling, onto the target skeleton [imp import_pose_asset_animation]. Curve
		// metadata is not added: these imports are measurements and must leave the shared skeleton as it is.
		TStrongObjectPtr<UInterchangeGenericAssetsPipeline> Pipeline(NewObject<UInterchangeGenericAssetsPipeline>(
			GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UInterchangeGenericAssetsPipeline::StaticClass(), TEXT("DTHPoseImportPipeline")),
			RF_Transient));
		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->Skeleton = Skeleton;
		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bImportOnlyAnimations = true;
		Pipeline->CommonSkeletalMeshesAndAnimationsProperties->bAddCurveMetadataToSkeleton = false;
		Pipeline->AnimationPipeline->bImportAnimations = true;
		Pipeline->AnimationPipeline->bImportBoneTracks = true;
		Pipeline->AnimationPipeline->AnimationRange = EInterchangeAnimationRange::Timeline;
		Pipeline->AnimationPipeline->bUse30HzToBakeBoneAnimation = false;
		Pipeline->MeshPipeline->bImportSkeletalMeshes = false;
		Pipeline->MeshPipeline->bImportStaticMeshes = false;
		Pipeline->MeshPipeline->bCreatePhysicsAsset = false;
		Pipeline->MaterialPipeline->bImportMaterials = false;

		FImportAssetParameters Parameters;
		Parameters.bIsAutomated = true;
		Parameters.bReplaceExisting = true;
		Parameters.OverridePipelines.Add(FSoftObjectPath(Pipeline.Get()));

		TArray<UObject*> Imported;
		UInterchangeManager::GetInterchangeManager().ImportAsset(Folder, UInterchangeManager::CreateSourceData(File), Parameters, Imported);
		for (UObject* Object : Imported)
		{
			if (UAnimSequence* Sequence = Cast<UAnimSequence>(Object))
			{
				return Sequence;
			}
		}
		OutError = FString::Printf(TEXT("%s did not import as an animation."), *FPaths::GetCleanFilename(File));
		return nullptr;
	}

	bool LoadPoseNames(const FString& File, TArray<FString>& OutNames, FString& OutError)
	{
		OutNames.Reset();
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *File))
		{
			OutError = FString::Printf(TEXT("Could not read %s."), *File);
			return false;
		}
		for (FString& Line : Lines)
		{
			Line.TrimStartAndEndInline();
			if (!Line.IsEmpty())
			{
				OutNames.Add(Line);
			}
		}
		return OutNames.Num() > 0;
	}
}
