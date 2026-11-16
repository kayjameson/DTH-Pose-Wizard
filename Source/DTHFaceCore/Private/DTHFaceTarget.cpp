#include "DTHFaceTarget.h"

#include "DTHFaceRegion.h"
#include "DTHFaceEvaluator.h"
#include "DTHSubdivision.h"

namespace
{
	/** Points hashed into cubic cells, for nearest-point queries within one cell's reach. */
	struct FGrid
	{
		float Cell = 1.0f;
		TMap<FIntVector, TArray<int32>> Cells;
		const TArray<FVector3f>* Points = nullptr;

		FIntVector Key(const FVector3f& P) const { return FIntVector(FMath::FloorToInt(P.X / Cell), FMath::FloorToInt(P.Y / Cell), FMath::FloorToInt(P.Z / Cell)); }

		void Build(const TArray<FVector3f>& InPoints, TConstArrayView<int32> Use, float InCell)
		{
			Cell = InCell;
			Points = &InPoints;
			Cells.Reset();
			for (const int32 I : Use) Cells.FindOrAdd(Key(InPoints[I])).Add(I);
		}

		/** Every point within Radius (at most one cell) of P. */
		void Within(const FVector3f& P, float Radius, TArray<int32>& Out) const
		{
			Out.Reset();
			const FIntVector K = Key(P);
			for (int32 X = -1; X <= 1; ++X)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 Z = -1; Z <= 1; ++Z)
						if (const TArray<int32>* C = Cells.Find(K + FIntVector(X, Y, Z)))
						{
							for (const int32 I : *C) if (FVector3f::DistSquared((*Points)[I], P) <= Radius * Radius) Out.Add(I);
						}
		}

		/** Nearest point within one cell of P, or INDEX_NONE. */
		int32 Nearest(const FVector3f& P, float& OutDistance) const
		{
			const FIntVector K = Key(P);
			int32 Best = INDEX_NONE;
			float BestSq = TNumericLimits<float>::Max();
			for (int32 X = -1; X <= 1; ++X)
				for (int32 Y = -1; Y <= 1; ++Y)
					for (int32 Z = -1; Z <= 1; ++Z)
						if (const TArray<int32>* C = Cells.Find(K + FIntVector(X, Y, Z)))
						{
							for (const int32 I : *C)
							{
								const float D = FVector3f::DistSquared((*Points)[I], P);
								if (D < BestSq) { BestSq = D; Best = I; }
							}
						}
			OutDistance = Best == INDEX_NONE ? TNumericLimits<float>::Max() : FMath::Sqrt(BestSq);
			return Best;
		}
	};

	/**
	 * Which material slots a face figure's points may pair with: any surface but the eyes and what sits on them (moisture,
	 * tear, occlusion, lashes). Slot names differ between exports (Mouth / Teeth / MouthCavity), so the figures are told
	 * apart by rest position alone, which is exact.
	 */
	bool SlotBelongsTo(const FString& Figure, const FString& Slot)
	{
		// what sits on the eyes (wet layer, tear, occlusion, lashes) never takes part
		if (Slot.Contains(TEXT("Tear")) || Slot.Contains(TEXT("Occlusion")) || Slot.Contains(TEXT("Lash")) || Slot.Contains(TEXT("Moisture"))) return false;
		const bool bEyeSurface = Slot.Contains(TEXT("Eye")) || Slot.Contains(TEXT("Iris")) || Slot.Contains(TEXT("Sclera"))
			|| Slot.Contains(TEXT("Pupil")) || Slot.Contains(TEXT("Cornea"));
		// the eyeballs pair only with the Eyes figure (sampled for the gaze), everything else only with the other figures
		return Figure.Contains(TEXT("Eyes")) ? bEyeSurface : !bEyeSurface;
	}

	float Median(TArray<float>& V)
	{
		if (V.IsEmpty()) return 0.0f;
		V.Sort();
		return V[V.Num() / 2];
	}
}

FDTHFaceTarget::FDTHFaceTarget() = default;
FDTHFaceTarget::~FDTHFaceTarget() = default;

bool FDTHFaceTarget::Build(const TArray<FDTHFaceFigure>& Face, FDTHFaceEvaluator& Evaluator, TArray<FString>& OutReport, FString& OutError,
	const TArray<TArray<FVector3f>>* CharacterRest)
{
	if (!!Face.IsEmpty())
	{
		OutError = TEXT("The samples have no face shape: sample them again with the current Daz script (DTH_SampleExpressions.dsa).");
		return false;
	}

	// The Daz face at rest, subdivided as the export does.
	Subdivisions.Reset();
	TArray<TArray<FVector3f>> DazPoints;
	for (int32 Fi = 0; Fi < Face.Num(); ++Fi)
	{
		const FDTHFaceFigure& F = Face[Fi];
		Subdivisions.Add(MakeUnique<FDTHSubdivision>(F.Vertices.Num(), F.Facets));
		TArray<FVector3f> Rest = F.Rest;
		if (CharacterRest)
		{
			if (!CharacterRest->IsValidIndex(Fi))
			{
				OutError = FString::Printf(TEXT("The character's rest face has no %s."), *F.Name);
				return false;
			}
			for (int32 I = 0; I < F.Vertices.Num(); ++I)
			{
				if ((*CharacterRest)[Fi].IsValidIndex(F.Vertices[I])) Rest[I] = (*CharacterRest)[Fi][F.Vertices[I]];
			}
		}
		Subdivisions.Last()->Apply(Rest, DazPoints.AddDefaulted_GetRef());
	}

	// The character at rest, every render vertex, and which figure each one can belong to.
	TArray<int32> All;
	All.SetNumUninitialized(Evaluator.NumVertices());
	for (int32 I = 0; I < All.Num(); ++I) All[I] = I;
	Evaluator.SetVertices(All);
	TArray<FVector3f> UnrealRest;
	Evaluator.Evaluate({}, UnrealRest);
	TArray<TArray<int32>> Candidates;
	Candidates.SetNum(Face.Num());
	TMap<FName, TArray<bool>> SlotUse;
	for (int32 V = 0; V < All.Num(); ++V)
	{
		const FName Slot = Evaluator.MaterialSlot(V);
		TArray<bool>* Use = SlotUse.Find(Slot);
		if (!Use)
		{
			Use = &SlotUse.Add(Slot);
			for (const FDTHFaceFigure& F : Face) Use->Add(SlotBelongsTo(F.Name, Slot.ToString()));
		}
		for (int32 F = 0; F < Face.Num(); ++F) if ((*Use)[F]) Candidates[F].Add(V);
	}

	// Axes: Daz is Y up, Unreal Z up; the X sign and any offset are found from the data (an offset of a few cm at most).
	float BestScore = TNumericLimits<float>::Max(), BestSign = 1.0f;
	FVector3f BestOffset = FVector3f::ZeroVector;
	for (const float Sign : {1.0f, -1.0f})
	{
		SignX = Sign;
		FVector3f Offset = FVector3f::ZeroVector;
		float Score = TNumericLimits<float>::Max();
		for (const float Cell : {4.0f, 1.0f, 0.25f})
		{
			FGrid Grid;
			Grid.Build(UnrealRest, Candidates[0], Cell);
			TArray<float> DX, DY, DZ, Dist;
			for (int32 P = 0; P < DazPoints[0].Num(); P += 25)
			{
				const FVector3f Q = ToUnreal(DazPoints[0][P]) + Offset;
				float D = 0.0f;
				const int32 N = Grid.Nearest(Q, D);
				if (N == INDEX_NONE) continue;
				DX.Add(UnrealRest[N].X - Q.X); DY.Add(UnrealRest[N].Y - Q.Y); DZ.Add(UnrealRest[N].Z - Q.Z); Dist.Add(D);
			}
			if (Dist.Num() < DazPoints[0].Num() / 50) { Score = TNumericLimits<float>::Max(); break; }
			Offset += FVector3f(Median(DX), Median(DY), Median(DZ));
			Score = Median(Dist);
		}
		// A DTH export is never mirrored: the mirrored axes win only when they are clearly better. A face is nearly
		// symmetric, so a Daz figure whose rest differs from the character's a little can line up almost as well mirrored,
		// and a mirrored pairing turns every one-sided shape (a gaze to one side) to the wrong side.
		const float Needed = Sign < 0.0f ? 0.25f * BestScore : BestScore;
		if (Score < Needed) { BestScore = Score; BestOffset = Offset; BestSign = Sign; }
	}
	SignX = BestSign;
	if (BestScore > 1.0f)
	{
		OutError = TEXT("The character's face does not line up with the sampled Genesis 9 face (is it a Genesis 9 exported through DTH?).");
		return false;
	}

	// Pairs: every Daz point with an Unreal vertex of its own surface at the same rest position.
	PairFigure.Reset();
	PairPoint.Reset();
	TArray<int32> PairVertex;
	for (int32 F = 0; F < Face.Num(); ++F)
	{
		FGrid Grid;
		Grid.Build(UnrealRest, Candidates[F], 0.1f);
		int32 Paired = 0, Ambiguous = 0;
		TArray<int32> Near;
		for (int32 P = 0; P < DazPoints[F].Num(); ++P)
		{
			float D = 0.0f;
			const FVector3f Q = ToUnreal(DazPoints[F][P]) + BestOffset;
			const int32 N = Grid.Nearest(Q, D);
			if (N != INDEX_NONE && D < MatchDistance)
			{
				// Surfaces that touch at rest (the upper and lower gums with the mouth closed) put vertices of different bones
				// within reach of one point: the pair could be the wrong one, so the point stays out.
				Grid.Within(Q, MatchDistance, Near);
				const FName Bone = Evaluator.DominantBone(N);
				bool bAmbiguous = false;
				for (const int32 O : Near) bAmbiguous |= O != N && Evaluator.DominantBone(O) != Bone;
				if (bAmbiguous) { ++Ambiguous; continue; }
				PairFigure.Add(F);
				PairPoint.Add(P);
				PairVertex.Add(N);
				++Paired;
			}
		}
		OutReport.Add(FString::Printf(TEXT("Face %s: %d of %d points paired with the character's vertices (within %.2f mm); %d left out where two bones' surfaces touch."),
			*Face[F].Name, Paired, DazPoints[F].Num(), MatchDistance * 10.0f, Ambiguous));
	}
	OutReport.Add(FString::Printf(TEXT("Face alignment: X sign %+.0f, offset (%.3f, %.3f, %.3f) cm, median distance %.4f cm."),
		SignX, BestOffset.X, BestOffset.Y, BestOffset.Z, BestScore));
	if (PairVertex.IsEmpty())
	{
		OutError = TEXT("No point of the sampled face lines up with the character.");
		return false;
	}
	Evaluator.SetVertices(MoveTemp(PairVertex));
	return true;
}

bool FDTHFaceTarget::TargetFromMeshes(const TArray<FDTHFaceFigure>& Face, const TArray<TArray<FVector3f>>& Displacement, TArray<FVector3f>& Out) const
{
	if (Displacement.Num() < Face.Num()) return false;
	TArray<TArray<FVector3f>> Sub;
	Sub.SetNum(Subdivisions.Num());
	for (int32 F = 0; F < Subdivisions.Num(); ++F)
	{
		const FDTHFaceFigure& Figure = Face[F];
		TArray<FVector3f> Region;
		Region.SetNumUninitialized(Figure.Vertices.Num());
		for (int32 I = 0; I < Figure.Vertices.Num(); ++I)
		{
			Region[I] = Displacement[F].IsValidIndex(Figure.Vertices[I]) ? Displacement[F][Figure.Vertices[I]] : FVector3f::ZeroVector;
		}
		Subdivisions[F]->Apply(Region, Sub[F]);
	}
	Out.SetNumUninitialized(PairFigure.Num());
	for (int32 I = 0; I < PairFigure.Num(); ++I) Out[I] = ToUnreal(Sub[PairFigure[I]][PairPoint[I]]);
	return true;
}

bool FDTHFaceTarget::Target(const TArray<TArray<FVector3f>>& Displacement, TArray<FVector3f>& Out) const
{
	if (Displacement.Num() != Subdivisions.Num()) return false;
	TArray<TArray<FVector3f>> Sub;
	Sub.SetNum(Subdivisions.Num());
	for (int32 F = 0; F < Subdivisions.Num(); ++F)
	{
		if (Displacement[F].IsEmpty()) return false;
		Subdivisions[F]->Apply(Displacement[F], Sub[F]);
	}
	Out.SetNumUninitialized(PairFigure.Num());
	for (int32 I = 0; I < PairFigure.Num(); ++I) Out[I] = ToUnreal(Sub[PairFigure[I]][PairPoint[I]]);
	return true;
}
