#pragma once

#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "DTHCurveDriverInstance.generated.h"

/** Outputs the reference pose with a set of curves, so the character's post-process blueprint sees them as animation. */
struct DTHFACECORE_API FDTHCurveDriverProxy : public FAnimInstanceProxy
{
	FDTHCurveDriverProxy() = default;
	explicit FDTHCurveDriverProxy(UAnimInstance* Instance) : FAnimInstanceProxy(Instance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	TArray<TPair<FName, float>> Values;
	/** Compact pose index -> local transform, over the reference pose. */
	TArray<TPair<int32, FTransform>> Bones;
};

/**
 * The main anim instance of the Mixer preview and of the face rig measurement: plays nothing but the face-board curve
 * values it is given, exactly as a face sequence would carry them.
 */
UCLASS(Transient, NotBlueprintable)
class DTHFACECORE_API UDTHCurveDriverInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/**
	 * Plays these values. A curve played before but missing now is played at 0: the post-process blueprint keeps the
	 * last value of a curve that stops arriving, which would leave it stuck.
	 */
	void SetCurves(const TMap<FName, float>& InValues)
	{
		for (TPair<FName, float>& V : Values) V.Value = 0.0f;
		for (const TPair<FName, float>& V : InValues) Values.Add(V.Key, V.Value);
	}
	const TMap<FName, float>& GetCurves() const { return Values; }

	/** Local transforms for some bones (by name), played over the reference pose with the curves; empty: the reference pose. */
	void SetBones(const TMap<FName, FTransform>& InBones) { Bones = InBones; }
	const TMap<FName, FTransform>& GetBones() const { return Bones; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return new FDTHCurveDriverProxy(this); }

private:
	TMap<FName, float> Values;
	TMap<FName, FTransform> Bones;
};
