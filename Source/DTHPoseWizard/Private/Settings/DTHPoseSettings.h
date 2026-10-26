#pragma once

#include "Engine/DeveloperSettings.h"
#include "Engine/EngineTypes.h"
#include "DTHPoseSettings.generated.h"

/**
 * Where the Pose Wizard reads and writes. Shown under Project Settings > Plugins > DTH Pose Wizard. Stored per user,
 * per project. The Phase 0 measurements (automation tests DTH.Pose.P0.*) read their inputs from here.
 */
UCLASS(config = EditorPerProjectUserSettings, BlueprintType, meta = (DisplayName = "DTH Pose Wizard"))
class UDTHPoseSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDTHPoseSettings();

	/** DTH_PoseSamples.json, written by the Daz Studio sampler script. */
	UPROPERTY(config, EditAnywhere, Category = "Library", meta = (DisplayName = "Daz Samples File", FilePathFilter = "json"))
	FFilePath SamplesFile;

	/** A DTH Genesis 9 character: a skeletal mesh with DTH's post-process blueprint, as the DTH import sets it up. */
	UPROPERTY(config, EditAnywhere, Category = "Character", meta = (DisplayName = "Character", AllowedClasses = "/Script/Engine.SkeletalMesh"))
	FSoftObjectPath Character;

	/**
	 * The character's DTH export folder (the one holding "Pose Assets" and "Skeletal Meshes"). The measurements read the
	 * JCM and RET pose-asset FBX files there as ground truth.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Character", meta = (DisplayName = "DTH Export Folder"))
	FDirectoryPath ExportFolder;

	/** New pose sequences are created in this folder. */
	UPROPERTY(config, EditAnywhere, Category = "Output", meta = (ContentDir, DisplayName = "Sequence Folder"))
	FDirectoryPath SequenceFolder;

	/** Measurement and test assets go here, and nowhere else. */
	UPROPERTY(config, EditAnywhere, Category = "Testing", meta = (ContentDir, DisplayName = "Test Folder"))
	FDirectoryPath TestFolder;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Saved/DTHPoseWizard, where per-character measurements and measurement reports are written. */
	static FString SavedDir();
};
