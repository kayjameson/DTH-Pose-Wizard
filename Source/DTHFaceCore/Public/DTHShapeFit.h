#pragma once

#include "CoreMinimal.h"

class FDTHFaceEvaluator;
struct FDTHFaceRig;

/**
 * Finds face-board values that reproduce a face shape on the character, measured on the real rig.
 *
 * DTH's face board is used as a face-board key can hold it: each control axis on one side (a slider cannot be at both
 * ends at once), 0..1. On top of it, DTH's own per-pose morph curves (fac*) can carry what the board cannot: a shape past
 * the board's range, or a pose the face rig never drives. The post-process blueprint adds them to what the face rig
 * writes (measured, DTH.ExpressionWizard.CurveReach). They cost a little, so the board does what it can.
 *
 * Bone shapes (the eyes' turn, the jaw, the tongue) cannot be pushed past DTH's face rig with curves: it clamps every
 * face-pose weight to 0..1 (CRFL_DTH_Common, Animated Map Driver). A sequence's own bone tracks pass through it, with the
 * face rig's poses added on top (DTH.Pose.Face.BoneTracks), so the fit may also turn chosen face bones (Init's Bones):
 * three rotation channels each, BoneUnitDeg per unit, measured like the curves and costing a little, so the curves
 * come first. The board's response is measured once (every curve alone, and the pairs
 * that act together: the face rig's own pairs, eyeFaceScrunch and jawOpen with every curve, the tongue curves with each
 * other) and gives the starting values. Then every step is measured: the slopes of the curves in play and of the ones
 * that would reduce the remaining error most, a bounded least-squares step, and a line search, each evaluation exact.
 */
class DTHFACECORE_API FDTHShapeFit
{
public:
	/** A face-board curve's control axis and side, from the face board (DTHRigKeying::BoardMap). */
	struct FBoardCurve
	{
		FName Curve;
		FName Control;
		int32 Axis = 0;
		float Sign = 1.0f;
	};

	struct FOptions
	{
		/** Wall-clock budget per shape, seconds. */
		double TimeBudget = 3.0;
		/** Curves kept at 0 (e.g. the eye-look curves when gaze is stripped). */
		TSet<FName> Fixed;
		/** Largest curve value. 1 is the face board's range; more is only for finding what limits a shape. */
		double MaxValue = 1.0;
		/** Each control axis on one side, as a face-board key holds it. Off only for finding what limits a shape. */
		bool bOneSide = true;
		/** Range of the morph curves (Init's Morphs): a pose's morph past the board, or against it, as Daz's own values go. */
		double MorphMin = -1.0;
		double MorphMax = 1.0;
		/**
		 * What a morph curve costs, as a share of its own effect: one is used only where it gains more than this. 0 lets the
		 * morphs compete with the board on equal terms.
		 */
		double MorphCost = 0.05;
		/** Range of the bone channels (Init's Bones), in units of BoneUnitDeg, and what one costs (as MorphCost). */
		double BoneMax = 4.0;
		double BoneCost = 0.05;
		/** Optional start values (one per curve), measured from directly instead of the model start; for checking the fit. */
		TArray<double> Start;
	};

	struct FResult
	{
		/** One value per curve (Curves()). */
		TArray<float> Values;
		/** 1 - |residual| / |shape| over the face points, measured. */
		double Match = 0.0;
		double ModelMatch = 0.0;
		int32 Evaluations = 0;
		int32 Iterations = 0;
		/** The largest movement of any face point in the shape (cm). */
		double Largest = 0.0;
		/** Time in the model start, and in the measured steps. */
		double ModelSeconds = 0.0;
		double Seconds = 0.0;
	};

	/**
	 * Measures the board on Evaluator's character (its chosen vertices). Rig gives the curve exponents and known pairs.
	 * Morphs: morph curves the fit may add on top of the board (DTH's fac*); the ones that move no face point are left out.
	 */
	bool Init(FDTHFaceEvaluator& Evaluator, TConstArrayView<FBoardCurve> Board, const FDTHFaceRig& Rig, TArray<FString>& OutReport, FString& OutError,
		TConstArrayView<FName> Morphs = {}, TConstArrayView<FName> Bones = {});

	/** Whether curve K (Curves()) is a morph curve rather than a board curve. */
	bool IsMorph(int32 K) const { return IsMorphCurve.IsValidIndex(K) && IsMorphCurve[K]; }

	/** Degrees one unit of a bone channel turns. */
	static constexpr double BoneUnitDeg = 10.0;

	/** Whether variable K (Curves()) is a bone channel ("bone|<bone>|<axis>") rather than a curve; its bone and axis. */
	bool IsBone(int32 K) const { return BoneVar.IsValidIndex(K) && BoneVar[K] != INDEX_NONE; }
	FName BoneOf(int32 K) const { return IsBone(K) ? BoneNames[BoneVar[K]] : NAME_None; }
	int32 AxisOf(int32 K) const { return IsBone(K) ? BoneAxis[K] : INDEX_NONE; }

	/** The turn (local, on top of the bone's reference pose) of each bone the values set, by name. */
	TMap<FName, FQuat> BoneTurns(TConstArrayView<float> Values) const;

	/** Bones held under every measurement (local transforms by name; the fit's own bone channels turn on top of them). */
	void SetExternalBones(const TMap<FName, FTransform>& Bones) { External = Bones; }

	/** Fits Target (displacement of each chosen vertex from rest, Unreal axes, cm). */
	FResult Fit(TConstArrayView<FVector3f> Target, const FOptions& Options);

	const TArray<FName>& Curves() const { return CurveNames; }

	/** The face's displacement from rest on the real rig for these values (one per curve). */
	void Displacement(TConstArrayView<float> Values, TArray<FVector3f>& Out)
	{
		TArray<double> C;
		for (const float V : Values) C.Add(V);
		Measure(C, Out);
	}

private:
	struct FPairTerm { int32 A = 0; int32 B = 0; };

	double F(int32 Curve, double X) const { return Exponent[Curve] == 2 ? X * X : X; }
	double DF(int32 Curve, double X) const { return Exponent[Curve] == 2 ? 2.0 * X : 1.0; }
	void Features(TConstArrayView<double> C, TArray<double>& Out) const;
	void Measure(TConstArrayView<double> C, TArray<FVector3f>& OutDisplacement);
	/** Keeps each two-sided control axis on one side (the side with the larger value). */
	void OneSide(TArray<double>& C) const;
	void ModelFit(const TArray<double>& BT, double TT, const TArray<bool>& Free, TArray<double>& C) const;
	/** Per curve, for the current fit: its range and what it costs (added to its curvature). */
	void Bounds(const FOptions& Options);
	TArray<FName> BoneNames;
	TArray<FTransform> BoneRef;
	/** Per variable: its bone (index into BoneNames) and axis, or INDEX_NONE for a curve. */
	TArray<int32> BoneVar;
	TArray<int32> BoneAxis;
	TMap<FName, FTransform> External;
	TArray<double> Lower;
	TArray<double> Upper;
	TArray<double> Cost;
	TArray<bool> IsMorphCurve;

	FDTHFaceEvaluator* Evaluator = nullptr;
	TArray<FName> CurveNames;
	TArray<int32> Exponent;
	/** Per curve: the curves on the other side of its control axis. */
	TArray<TArray<int32>> Opposite;
	/** Per curve: the curves one board slider drives with it (itself included); they always take the same value. */
	TArray<TArray<int32>> Tied;
	/** Sets each tied group to its mean. */
	void Tie(TArray<double>& C) const;
	TArray<FVector3f> Rest;
	int32 NumPoints = 0;
	/** Measured basis: one column per feature (curves alone, then the pair interactions), 3 * NumPoints rows. */
	TArray<TArray<float>> Basis;
	TArray<FPairTerm> Pairs;
	/** Gram matrix of the basis (features x features). */
	TArray<double> Gram;
	/** The current fit's options (see FOptions). */
	double MaxValue = 1.0;
	bool bOneSideRule = true;
};
