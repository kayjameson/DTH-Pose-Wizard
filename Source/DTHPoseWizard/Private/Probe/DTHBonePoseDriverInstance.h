#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "DTHBonePoseDriverInstance.generated.h"

class UAnimSequence;

/**
 * Outputs either a given local bone pose (bones not given stay at the mesh's reference pose) or one frame of a
 * sequence, so the character's post-process blueprint sees it as animation.
 */
struct FDTHBonePoseDriverProxy : public FAnimInstanceProxy
{
	FDTHBonePoseDriverProxy() = default;
	explicit FDTHBonePoseDriverProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** Skeleton bone index and the local transform it gets. */
	TArray<TPair<int32, FTransform>> Bones;
	TArray<TPair<FName, float>> Curves;
	const UAnimSequence* Sequence = nullptr;
	double Time = 0.0;
};

/**
 * The main anim instance of the Pose Wizard's measurements. Two modes:
 * - SetPose: plays these local bone transforms exactly, as the mesh evaluates them (after any retargeting), and
 *   these curves.
 * - SetSequence: plays one frame of a sequence the way a sequence player does, retargeting included.
 */
UCLASS(Transient, NotBlueprintable)
class UDTHBonePoseDriverInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** Bones not named are played at the mesh's reference pose. Curves not named are played at 0. */
	void SetPose(const TMap<FName, FTransform>& InBones, const TMap<FName, float>& InCurves)
	{
		Sequence = nullptr;
		Bones = InBones;
		for (TPair<FName, float>& V : Curves) V.Value = 0.0f;
		for (const TPair<FName, float>& V : InCurves) Curves.Add(V.Key, V.Value);
	}

	void SetSequence(const UAnimSequence* InSequence, double InTime)
	{
		Bones.Reset();
		for (TPair<FName, float>& V : Curves) V.Value = 0.0f;
		Sequence = const_cast<UAnimSequence*>(InSequence);
		Time = InTime;
	}

	const TMap<FName, FTransform>& GetBones() const { return Bones; }
	const TMap<FName, float>& GetCurves() const { return Curves; }
	const UAnimSequence* GetSequence() const { return Sequence; }
	double GetTime() const { return Time; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FDTHBonePoseDriverProxy(this); }

private:
	TMap<FName, FTransform> Bones;
	TMap<FName, float> Curves;
	UPROPERTY()
	TObjectPtr<UAnimSequence> Sequence;
	double Time = 0.0;
};
