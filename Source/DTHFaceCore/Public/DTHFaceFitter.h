#pragma once

#include "CoreMinimal.h"
#include "DTHFaceEvaluator.h"
#include "DTHFaceRig.h"
#include "DTHFaceTarget.h"
#include "DTHShapeFit.h"

class USkeletalMesh;
struct FDTHFaceFigure;

/**
 * A sampled Daz face onto a DTH character, as curves its face rig plays: everything the shape fit needs, set up once per
 * character and face region (the character's face rig measured, the Daz face paired with its vertices, the board and
 * morph curves measured), then one Fit per sampled face. Shared by the Expression, Pose and Animation Wizards.
 */
class DTHFACECORE_API FDTHFaceFitter
{
public:
	FDTHFaceFitter() = default;
	FDTHFaceFitter(const FDTHFaceFitter&) = delete;
	FDTHFaceFitter& operator=(const FDTHFaceFitter&) = delete;

	/** Sets up for Character (a DTH Genesis 9 skeletal mesh) and the sampler's face region. Report lines say what was measured. */
	bool Init(USkeletalMesh* Character, const TArray<FDTHFaceFigure>& Face, TArray<FString>& OutReport, FString& OutError);

	/** A face's displacement smaller than this (cm, at every point) is the face at rest: no curves. */
	static constexpr double RestTolerance = 0.02;

	struct FResult
	{
		/** The curves to play (face-board CTRL_expressions_* and DTH's morph curves fac*), zeros left out. */
		TMap<FName, float> Curves;
		/** Face bones to turn past DTH's face rig (eyes, jaw, tongue): a local turn on top of each bone's reference pose. */
		TMap<FName, FQuat> Bones;
		/** Every fitted value by variable name (curves and bone channels), to start the next fit from (a clip's next frame). */
		TMap<FName, float> Values;
		/** 1 - |residual| / |face movement| over the paired points; 1 for a face at rest. */
		double Match = 1.0;
		/** The face's largest movement (cm). */
		double Largest = 0.0;
		double Seconds = 0.0;
		/** The sampled face and the fitted one on the paired points (Unreal axes, cm, Evaluator order), for checks. */
		TArray<FVector3f> Target;
		TArray<FVector3f> Got;
	};

	/**
	 * Per face region (lips, lip corners, upper and lower lids, brows, eyes, nose, cheeks, jaw and chin, teeth, tongue,
	 * the rest of the head, by the bone that moves each point most): how far Daz moves it at most, and the largest and
	 * 95th-percentile error, in mm, for every region Daz moves more than 0.5 mm. Worst: the largest error of any region.
	 */
	FString RegionReport(const FResult& Result, double& OutWorstMm, FString& OutWorstRegion) const;

	/** The face region a bone belongs to (see RegionReport). */
	static const TCHAR* RegionOf(FName Bone);

	/**
	 * Fits a sampled face: per face figure, how far each region vertex moved (Daz space, cm). Start (optional): curves to
	 * start from instead of the board model (the previous frame of a clip, for one that moves little).
	 */
	bool Fit(const TArray<TArray<FVector3f>>& Displacement, const FDTHShapeFit::FOptions& Options, FResult& Out,
		const TMap<FName, float>* Start = nullptr, const TMap<FName, FTransform>* BaseBones = nullptr);

	/**
	 * The face bones a pose sequence keys itself, from a full local pose: the eyeballs (any gaze; DTH's eye-look poses
	 * stop at 20 deg up or down). Fit takes them as BaseBones, so the curves carry the rest of the face (DTH's
	 * post-process keeps a sequence's eye bones and adds the face rig's poses on top: DTH.Pose.Face.BoneTracks).
	 */
	TMap<FName, FTransform> FaceBones(const TMap<FName, FTransform>& LocalPose) const;

	/** The face bones the fit may turn (on the character): the eyes, the jaw and the tongue. */
	static TArray<FName> TurnableBones(const USkeletalMesh& Character);

	/** The eye-look curves and the eyes' bone channels (fixed in Options to leave the gaze out). */
	TSet<FName> GazeCurves() const;

	FDTHShapeFit& Shape() { return ShapeFit; }
	FDTHFaceEvaluator& Evaluator() { return FaceEvaluator; }
	const FDTHFaceRig& Rig() const { return FaceRig; }

private:
	FDTHFaceEvaluator FaceEvaluator;
	FDTHFaceTarget Target;
	FDTHShapeFit ShapeFit;
	FDTHFaceRig FaceRig;
};
