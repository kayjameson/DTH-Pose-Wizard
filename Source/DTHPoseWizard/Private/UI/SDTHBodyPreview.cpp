#include "UI/SDTHBodyPreview.h"

#include "AdvancedPreviewScene.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "EditorViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "ImageUtils.h"
#include "Probe/DTHBonePoseDriverInstance.h"
#include "RenderingThread.h"

namespace
{
	/** Ticks the preview world so the pose and the post-process blueprint run. */
	class FBodyPreviewClient : public FEditorViewportClient
	{
	public:
		FBodyPreviewClient(FPreviewScene* InScene, const TWeakPtr<SEditorViewport>& InViewport)
			: FEditorViewportClient(nullptr, InScene, InViewport)
		{
			SetRealtime(true);
			EngineShowFlags.SetGrid(false);
			ViewFOV = 30.0f;
		}

		virtual void Tick(float DeltaSeconds) override
		{
			FEditorViewportClient::Tick(DeltaSeconds);
			if (PreviewScene)
			{
				PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
			}
		}
	};
}

void SDTHBodyPreview::Construct(const FArguments& InArgs)
{
	Scene = MakeShared<FAdvancedPreviewScene>(FPreviewScene::ConstructionValues());
	Scene->SetFloorVisibility(true);
	SEditorViewport::Construct(SEditorViewport::FArguments());
}

SDTHBodyPreview::~SDTHBodyPreview()
{
	if (Client.IsValid())
	{
		Client->Viewport = nullptr;
	}
}

TSharedRef<FEditorViewportClient> SDTHBodyPreview::MakeEditorViewportClient()
{
	Client = MakeShared<FBodyPreviewClient>(Scene.Get(), SharedThis(this));
	Client->SetViewLocation(FVector(0.0, 400.0, 100.0));
	Client->SetViewRotation(FRotator(0.0, -90.0, 0.0));
	return Client.ToSharedRef();
}

USkeletalMesh* SDTHBodyPreview::GetCharacter() const
{
	return Component ? Component->GetSkeletalMeshAsset() : nullptr;
}

void SDTHBodyPreview::SetCharacter(USkeletalMesh* Mesh)
{
	if (Component)
	{
		Scene->RemoveComponent(Component);
		Component = nullptr;
	}
	if (!Mesh || !Mesh->GetSkeleton())
	{
		return;
	}
	Component = NewObject<UDebugSkelMeshComponent>(GetTransientPackage());
	Component->SetSkeletalMesh(Mesh);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UDTHBonePoseDriverInstance::StaticClass());
	Scene->AddComponent(Component, FTransform::Identity);
	View = TEXT("body");
	FrameAfterTicks = 3;
}

void SDTHBodyPreview::SetPose(const TMap<FName, FTransform>& Pose)
{
	if (UDTHBonePoseDriverInstance* Driver = Component ? Cast<UDTHBonePoseDriverInstance>(Component->GetAnimInstance()) : nullptr)
	{
		Driver->SetPose(Pose, {});
		FrameAfterTicks = 3;
	}
}

void SDTHBodyPreview::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SEditorViewport::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (FrameAfterTicks > 0 && --FrameAfterTicks == 0)
	{
		SetView(View);
	}
}

bool SDTHBodyPreview::SetView(const FString& InView)
{
	if (!Component || !Client.IsValid())
	{
		return false;
	}
	// DTH characters face +Y on the UE5 Mannequin skeleton; +X is the character's left (DTH.Pose.P0.DazMapping).
	Component->UpdateBounds();
	// The distance that fits a sphere of this radius in the view, by the narrower of its two fields of view.
	const FIntPoint Size = Client->Viewport ? Client->Viewport->GetSizeXY() : FIntPoint(1, 1);
	const double Aspect = Size.Y > 0 ? double(Size.X) / Size.Y : 1.0;
	const double HalfH = FMath::DegreesToRadians(Client->ViewFOV * 0.5);
	const double HalfV = FMath::Atan(FMath::Tan(HalfH) / FMath::Max(Aspect, 0.01));
	auto Fit = [&](double Radius) { return Radius / FMath::Sin(FMath::Min(HalfH, HalfV)); };
	const FVector Center = Component->Bounds.Origin;
	const double Distance = Fit(Component->Bounds.SphereRadius * 0.75);
	const double Q = FMath::DegreesToRadians(45.0);
	FVector Target = Center, Eye;
	if (InView.Equals(TEXT("body"), ESearchCase::IgnoreCase)) Eye = Center + FVector(0.0, Distance, 0.0);
	else if (InView.Equals(TEXT("side"), ESearchCase::IgnoreCase)) Eye = Center + FVector(Distance, 0.0, 0.0);
	else if (InView.Equals(TEXT("threequarter"), ESearchCase::IgnoreCase)) Eye = Center + FVector(Distance * FMath::Sin(Q), Distance * FMath::Cos(Q), 0.0);
	else if (InView.Equals(TEXT("face"), ESearchCase::IgnoreCase))
	{
		// Follow the head: its turn since the bind pose turns the bind-pose view direction.
		const int32 Head = Component->GetBoneIndex(TEXT("head"));
		if (Head == INDEX_NONE) return false;
		const FReferenceSkeleton& Ref = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
		FTransform Rest = FTransform::Identity;
		for (int32 B = Head; B != INDEX_NONE; B = Ref.GetParentIndex(B)) Rest = Rest * Ref.GetRefBonePose()[B];
		const FQuat Turn = Component->GetBoneQuaternion(TEXT("head"), EBoneSpaces::ComponentSpace) * Rest.GetRotation().Inverse();
		Target = Component->GetSocketLocation(TEXT("head")) + Turn.RotateVector(FVector(0.0, 2.0, 5.0));
		Eye = Target + Turn.RotateVector(FVector(0.0, Fit(14.0), 0.0));
	}
	else if (InView.Equals(TEXT("hands"), ESearchCase::IgnoreCase))
	{
		// Both hands, fingertips included (about 20 cm past the wrist), seen from the front and a little above.
		const FVector L = Component->GetSocketLocation(TEXT("hand_l")), R = Component->GetSocketLocation(TEXT("hand_r"));
		Target = (L + R) * 0.5;
		Eye = Target + FVector(0.0, 1.0, 0.25).GetSafeNormal() * Fit(FVector::Dist(L, R) * 0.5 + 20.0);
	}
	else return false;
	View = InView;
	Client->SetViewLocation(Eye);
	Client->SetViewRotation((Target - Eye).Rotation());
	Client->Invalidate();
	return true;
}

bool SDTHBodyPreview::Capture(const FString& File, const FString& InView, FString& OutError)
{
	if (!SetView(InView))
	{
		OutError = FString::Printf(TEXT("Cannot frame \"%s\" (no character, or not body/side/threequarter/face/hands)."), *InView);
		return false;
	}
	FViewport* Viewport = Client->Viewport;
	if (!Viewport || Viewport->GetSizeXY().X <= 0)
	{
		OutError = TEXT("The preview is not on screen (open the Poses page).");
		return false;
	}
	Viewport->Draw(false);
	FlushRenderingCommands();
	TArray<FColor> Pixels;
	if (!Viewport->ReadPixels(Pixels))
	{
		OutError = TEXT("Could not read the preview's pixels.");
		return false;
	}
	for (FColor& P : Pixels) P.A = 255;
	if (!FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Viewport->GetSizeXY().X, Viewport->GetSizeXY().Y)))
	{
		OutError = FString::Printf(TEXT("Could not write %s."), *File);
		return false;
	}
	return true;
}

void SDTHBodyPreview::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Component);
}
