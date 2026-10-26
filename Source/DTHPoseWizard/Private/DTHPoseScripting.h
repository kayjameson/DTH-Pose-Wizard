#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "DTHPoseScripting.generated.h"

/**
 * The Pose Wizard's buttons for Python and Blueprints (unreal.DTHPoseScripting). Each returns text lines; failures
 * start with "ERROR:", cautions with "WARNING:".
 */
UCLASS()
class UDTHPoseScripting : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Opens the wizard tab on a page: Settings, Build or Poses. */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static FString OpenWizard(const FString& Page);

	/** Builds the pose library from the settings' Daz Samples File into the settings' Pose Library. The build report. */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static TArray<FString> BuildLibrary();

	/** The library's poses whose label or pack contains Filter (all for ""): "label | pack | partial | missing". */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static TArray<FString> ListPoses(const FString& Filter);

	/** Previews a character (a DTH Genesis 9 skeletal mesh path) in the open wizard. */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static FString SelectCharacter(const FString& MeshPath);

	/** Previews a pose in the open wizard; a partial pose over BaseLabel ("" for the rest pose). */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static FString SelectPose(const FString& Label, const FString& BaseLabel);

	/** Saves the open wizard's preview, framed as View (body, side, threequarter, face, hands), to File (.png). */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static FString CapturePreview(const FString& File, const FString& View);

	/**
	 * Writes the poses (labels, one frame each) as a sequence for the character in the settings' Sequence Folder;
	 * partial poses over BaseLabel ("" for the rest pose). Name "" makes one from the first pose.
	 */
	UFUNCTION(BlueprintCallable, Category = "DTH Pose Wizard")
	static TArray<FString> WriteSequence(const FString& MeshPath, const TArray<FString>& Labels, const FString& BaseLabel, const FString& Name);
};
