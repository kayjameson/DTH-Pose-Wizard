#pragma once

#include "CoreMinimal.h"

class FDTHFaceEvaluator;
class FDTHSubdivision;
struct FDTHFaceFigure;

/**
 * Lays a Daz expression's face shape onto the character in Unreal. The Daz face (the sampled region, subdivided once as
 * the DTH export does) is paired point for point with the character's render vertices by rest position; a Daz point
 * with no Unreal vertex within MatchDistance stays out. An expression's target is then each pair's Daz displacement, in
 * Unreal's axes. Exact when the character is the figure the expressions were sampled on (a base Genesis 9 export).
 */
class DTHFACECORE_API FDTHFaceTarget
{
public:
	static constexpr float MatchDistance = 0.05f;

	FDTHFaceTarget();
	~FDTHFaceTarget();
	FDTHFaceTarget(const FDTHFaceTarget&) = delete;
	FDTHFaceTarget& operator=(const FDTHFaceTarget&) = delete;

	/**
	 * Pairs the sampled face region with Evaluator's character at rest, and points Evaluator at the paired vertices.
	 * CharacterRest (optional, by face figure: the figure's whole base mesh in Daz) is the character's own rest face, for
	 * a character that is not the sampled figure: the pairing then uses its shape while the targets stay the sampled ones.
	 */
	bool Build(const TArray<FDTHFaceFigure>& Face, FDTHFaceEvaluator& Evaluator, TArray<FString>& OutReport, FString& OutError,
		const TArray<TArray<FVector3f>>* CharacterRest = nullptr);

	/**
	 * Displacement (Unreal axes, cm) of each paired vertex (Evaluator's order) for an expression's sampled face displacement
	 * (per face figure, per region vertex, as DTHFaceRegion::ReadGeometry reads it); false if it was not sampled.
	 */
	bool Target(const TArray<TArray<FVector3f>>& Displacement, TArray<FVector3f>& Out) const;

	/** The same for any face shape given on the face figures' whole base meshes (by figure: displacement per Daz vertex). */
	bool TargetFromMeshes(const TArray<FDTHFaceFigure>& Face, const TArray<TArray<FVector3f>>& Displacement, TArray<FVector3f>& Out) const;

	int32 NumPairs() const { return PairFigure.Num(); }

private:
	FVector3f ToUnreal(const FVector3f& Daz) const { return FVector3f(Daz.X * SignX, Daz.Z, Daz.Y); }

	TArray<TUniquePtr<FDTHSubdivision>> Subdivisions;
	TArray<int32> PairFigure;
	TArray<int32> PairPoint;
	float SignX = 1.0f;
};
