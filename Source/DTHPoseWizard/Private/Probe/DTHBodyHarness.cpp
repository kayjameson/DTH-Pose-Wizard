#include "Probe/DTHBodyHarness.h"

#include "Animation/AnimInstance.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "ContentStreaming.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Probe/DTHBonePoseDriverInstance.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "TextureResource.h"

FDTHBodyHarness::FDTHBodyHarness()
	: Scene(FPreviewScene::ConstructionValues())
{
}

FDTHBodyHarness::~FDTHBodyHarness()
{
	if (Component)
	{
		Scene.RemoveComponent(Component);
	}
	if (Camera)
	{
		Scene.RemoveComponent(Camera);
	}
	if (RenderTarget)
	{
		RenderTarget->RemoveFromRoot();
	}
}

bool FDTHBodyHarness::Init(USkeletalMesh* Mesh, FString& OutError)
{
	if (!Mesh)
	{
		OutError = TEXT("No character mesh.");
		return false;
	}
	Component = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	Component->SetSkeletalMesh(Mesh);
	Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Component->bEnableUpdateRateOptimizations = false;
	Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
	Component->SetAnimInstanceClass(UDTHBonePoseDriverInstance::StaticClass());
	Scene.AddComponent(Component, FTransform::Identity);
	Driver = Cast<UDTHBonePoseDriverInstance>(Component->GetAnimInstance());
	if (!Driver || !Component->GetPostProcessInstance())
	{
		OutError = TEXT("The character has no post-process blueprint running (it needs DTH's, as the DTH import sets up).");
		return false;
	}
	// The post-process blueprint settles over its first evaluations (its pose drivers and curve smoothing).
	FDTHBoneReadback Settle;
	EvalPose({}, {}, Settle);
	EvalPose({}, {}, Settle);
	return true;
}

void FDTHBodyHarness::EvalPose(const TMap<FName, FTransform>& Bones, const TMap<FName, float>& Curves, FDTHBoneReadback& Out)
{
	Driver->SetPose(Bones, Curves);
	Tick(Out);
}

void FDTHBodyHarness::EvalSequence(const UAnimSequence* Sequence, double Time, FDTHBoneReadback& Out)
{
	Driver->SetSequence(Sequence, Time);
	Tick(Out);
}

void FDTHBodyHarness::Tick(FDTHBoneReadback& Out)
{
	Component->TickAnimation(1.0f / 30.0f, false);
	Component->RefreshBoneTransforms();
	Component->HandleExistingParallelEvaluationTask(true, true);

	const FReferenceSkeleton& Ref = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
	const TArray<FTransform>& Space = Component->GetComponentSpaceTransforms();
	const TArray<FTransform>& Local = Component->GetBoneSpaceTransforms();
	const int32 Num = FMath::Min3(Ref.GetNum(), Space.Num(), Local.Num());
	Out.Bones.SetNum(Num);
	Out.Component.SetNum(Num);
	Out.Local.SetNum(Num);
	for (int32 I = 0; I < Num; ++I)
	{
		Out.Bones[I] = Ref.GetBoneName(I);
		Out.Component[I] = Space[I];
		Out.Local[I] = Local[I];
	}
	Out.Curves.Reset();
	if (const UAnimInstance* Post = Component->GetPostProcessInstance())
	{
		Post->AppendAnimationCurveList(EAnimCurveType::AttributeCurve, Out.Curves);
	}
}

bool FDTHBodyHarness::Capture(const FString& File, const FVector& Eye, const FVector& Target, float FovDegrees, int32 Width,
	int32 Height, FString& OutError)
{
	if (!Component)
	{
		OutError = TEXT("No character.");
		return false;
	}
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage());
		RenderTarget->AddToRoot();
		Camera = NewObject<USceneCaptureComponent2D>(GetTransientPackage());
		Camera->bCaptureEveryFrame = false;
		Camera->bCaptureOnMovement = false;
		Camera->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
		// One still frame: nothing that needs a history of frames.
		Camera->ShowFlags.SetTemporalAA(false);
		Camera->ShowFlags.SetMotionBlur(false);
		Camera->ShowFlags.SetEyeAdaptation(false);
		Scene.AddComponent(Camera, FTransform::Identity);
	}
	if (RenderTarget->SizeX != Width || RenderTarget->SizeY != Height)
	{
		RenderTarget->InitCustomFormat(Width, Height, PF_B8G8R8A8, false);
		RenderTarget->UpdateResourceImmediate(true);
	}
	Camera->TextureTarget = RenderTarget;
	Camera->FOVAngle = FovDegrees;
	Camera->SetWorldLocationAndRotation(Eye, (Target - Eye).Rotation());

	// Everything the frame needs: compiled shaders, full-resolution textures, the posed bones on the render thread.
	Component->bForceMipStreaming = true;
	Component->MarkRenderStateDirty();
	if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
	IStreamingManager::Get().StreamAllResources(10.0f);
	for (int32 Warm = 0; Warm < 3; ++Warm)
	{
		Scene.GetWorld()->SendAllEndOfFrameUpdates();
		Camera->CaptureScene();
		FlushRenderingCommands();
	}

	TArray<FColor> Pixels;
	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource || !Resource->ReadPixels(Pixels) || Pixels.Num() != Width * Height)
	{
		OutError = TEXT("Could not read the capture's pixels.");
		return false;
	}
	for (FColor& P : Pixels) P.A = 255;
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
	if (!FImageUtils::SaveImageByExtension(*File, FImageView(Pixels.GetData(), Width, Height)))
	{
		OutError = FString::Printf(TEXT("Could not write %s."), *File);
		return false;
	}
	return true;
}
