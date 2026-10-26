#pragma once

#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class UDebugSkelMeshComponent;
class USkeletalMesh;

/**
 * The Poses page's live preview: the character in its own preview scene (never the level or the editor camera), playing
 * a local bone pose through the character's own post-process blueprint, as a written sequence plays.
 */
class SDTHBodyPreview : public SEditorViewport, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SDTHBodyPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SDTHBodyPreview() override;

	/** Shows Mesh (null clears it) and frames the whole body. */
	void SetCharacter(USkeletalMesh* Mesh);
	/** Plays this local pose (mesh space); bones not named stay at the reference pose. */
	void SetPose(const TMap<FName, FTransform>& Pose);
	/** Frames a view: body (front), side, threequarter, face or hands. False for an unknown name or no character. */
	bool SetView(const FString& View);
	/** Frames View, renders the preview and saves it as a PNG. */
	bool Capture(const FString& File, const FString& View, FString& OutError);

	USkeletalMesh* GetCharacter() const;
	UDebugSkelMeshComponent* GetComponent() const { return Component; }

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SDTHBodyPreview"); }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;

private:
	TSharedPtr<FAdvancedPreviewScene> Scene;
	TSharedPtr<FEditorViewportClient> Client;
	TObjectPtr<UDebugSkelMeshComponent> Component = nullptr;
	FString View = TEXT("body");
	/** Ticks to wait before framing (the bones are posed after the first ticks). */
	int32 FrameAfterTicks = 0;
};
