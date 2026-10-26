#include "Build/DTHPoseSolve.h"

#include "Build/DTHBoneMap.h"
#include "ReferenceSkeleton.h"

namespace
{
	using FMat3 = double[3][3];

	/** The rotation matrix of a unit quaternion (column vectors). */
	void QuatToMat(const FQuat& Q, FMat3& R)
	{
		const double X = Q.X, Y = Q.Y, Z = Q.Z, W = Q.W;
		R[0][0] = 1 - 2 * (Y * Y + Z * Z); R[0][1] = 2 * (X * Y - Z * W);     R[0][2] = 2 * (X * Z + Y * W);
		R[1][0] = 2 * (X * Y + Z * W);     R[1][1] = 1 - 2 * (X * X + Z * Z); R[1][2] = 2 * (Y * Z - X * W);
		R[2][0] = 2 * (X * Z - Y * W);     R[2][1] = 2 * (Y * Z + X * W);     R[2][2] = 1 - 2 * (X * X + Y * Y);
	}

	FQuat MatToQuat(const FMat3& R)
	{
		const double Trace = R[0][0] + R[1][1] + R[2][2];
		FQuat Q;
		if (Trace > 0)
		{
			const double S = FMath::Sqrt(Trace + 1.0) * 2;
			Q = FQuat((R[2][1] - R[1][2]) / S, (R[0][2] - R[2][0]) / S, (R[1][0] - R[0][1]) / S, 0.25 * S);
		}
		else if (R[0][0] > R[1][1] && R[0][0] > R[2][2])
		{
			const double S = FMath::Sqrt(1.0 + R[0][0] - R[1][1] - R[2][2]) * 2;
			Q = FQuat(0.25 * S, (R[0][1] + R[1][0]) / S, (R[0][2] + R[2][0]) / S, (R[2][1] - R[1][2]) / S);
		}
		else if (R[1][1] > R[2][2])
		{
			const double S = FMath::Sqrt(1.0 + R[1][1] - R[0][0] - R[2][2]) * 2;
			Q = FQuat((R[0][1] + R[1][0]) / S, 0.25 * S, (R[1][2] + R[2][1]) / S, (R[0][2] - R[2][0]) / S);
		}
		else
		{
			const double S = FMath::Sqrt(1.0 + R[2][2] - R[0][0] - R[1][1]) * 2;
			Q = FQuat((R[0][2] + R[2][0]) / S, (R[1][2] + R[2][1]) / S, 0.25 * S, (R[1][0] - R[0][1]) / S);
		}
		return Q.GetNormalized();
	}

	FVector Apply(const FMat3& M, const FVector& V)
	{
		return FVector(M[0][0] * V.X + M[0][1] * V.Y + M[0][2] * V.Z,
			M[1][0] * V.X + M[1][1] * V.Y + M[1][2] * V.Z,
			M[2][0] * V.X + M[2][1] * V.Y + M[2][2] * V.Z);
	}

	/** Every UE joint (component space) the axis fit and the mapping consider. */
	void Joints(const FReferenceSkeleton& Ref, const TArray<FTransform>& MeshRest, TArray<int32>& OutIndices)
	{
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			if (!DTHPoseSolve::IsSkipped(Ref.GetBoneName(I))) OutIndices.Add(I);
		}
	}

	/** -1 left, 1 right, 0 centre: Daz names a side with an l_/r_ prefix, UE with an _l/_r suffix. */
	int32 DazSide(FName Bone)
	{
		const FString S = Bone.ToString();
		return S.StartsWith(TEXT("l_")) ? -1 : S.StartsWith(TEXT("r_")) ? 1 : 0;
	}
	int32 UESide(FName Bone)
	{
		const FString S = Bone.ToString();
		return S.EndsWith(TEXT("_l")) ? -1 : S.EndsWith(TEXT("_r")) ? 1 : 0;
	}

	/** Nearest and second-nearest Daz bone on the same side to a UE joint (a body is nearly symmetric, so a mirrored
	 * axis conversion would otherwise fit almost as well). */
	void Nearest(const DTHPoseSolve::FAxes& Axes, const FDTHDazState& Daz, const FVector& P, int32 Side, FName& Best,
		double& BestCm, FName& Second, double& SecondCm)
	{
		Best = Second = NAME_None;
		BestCm = SecondCm = TNumericLimits<double>::Max();
		for (const TPair<FName, FDTHDazBone>& B : Daz.Bones)
		{
			if (DazSide(B.Key) != Side) continue;
			const double D = FVector::Dist(Axes.Position(B.Value.Pos), P);
			if (D < BestCm) { Second = Best; SecondCm = BestCm; Best = B.Key; BestCm = D; }
			else if (D < SecondCm) { Second = B.Key; SecondCm = D; }
		}
	}
}

namespace DTHPoseSolve
{
	FVector FAxes::Position(const FVector& Daz) const
	{
		return Apply(M, Daz) * Scale + Offset;
	}

	FQuat FAxes::Rotation(const FQuat& Daz) const
	{
		// R_ue = M R_daz M^T: the same rotation expressed in UE's axes (a proper rotation even when M mirrors).
		FMat3 R, T, Out;
		QuatToMat(Daz, R);
		for (int32 I = 0; I < 3; ++I)
			for (int32 J = 0; J < 3; ++J)
			{
				T[I][J] = 0;
				for (int32 K = 0; K < 3; ++K) T[I][J] += M[I][K] * R[K][J];
			}
		for (int32 I = 0; I < 3; ++I)
			for (int32 J = 0; J < 3; ++J)
			{
				Out[I][J] = 0;
				for (int32 K = 0; K < 3; ++K) Out[I][J] += T[I][K] * M[J][K];
			}
		return MatToQuat(Out);
	}

	FString FAxes::Describe() const
	{
		auto Row = [this](int32 I)
		{
			const TCHAR* Names[3] = {TEXT("x"), TEXT("y"), TEXT("z")};
			for (int32 J = 0; J < 3; ++J)
			{
				if (M[I][J] != 0) return FString::Printf(TEXT("%sdaz.%s"), M[I][J] < 0 ? TEXT("-") : TEXT(""), Names[J]);
			}
			return FString(TEXT("0"));
		};
		return FString::Printf(TEXT("ue.x = %s, ue.y = %s, ue.z = %s, scale %.5f, offset (%.3f, %.3f, %.3f) cm, %d joints matched, median distance %.4f cm"),
			*Row(0), *Row(1), *Row(2), Scale, Offset.X, Offset.Y, Offset.Z, Inliers, MedianCm);
	}

	TArray<FTransform> ComponentSpace(const FReferenceSkeleton& Ref, TFunctionRef<FTransform(int32)> Local)
	{
		TArray<FTransform> Out;
		Out.SetNum(Ref.GetNum());
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			const int32 Parent = Ref.GetParentIndex(I);
			Out[I] = Parent == INDEX_NONE ? Local(I) : Local(I) * Out[Parent];
		}
		return Out;
	}

	bool IsSkipped(FName UEBone)
	{
		const FString S = UEBone.ToString();
		return S == TEXT("root") || S.StartsWith(TEXT("ik_"));
	}

	FAxes FitAxes(const FDTHDazState& DazRest, const FReferenceSkeleton& Ref, const TArray<FTransform>& MeshRest, double MatchCm)
	{
		TArray<int32> Indices;
		Joints(Ref, MeshRest, Indices);
		FAxes Best;
		Best.MedianCm = TNumericLimits<double>::Max();
		Best.Inliers = -1;
		const int32 Perms[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
		for (const auto& P : Perms)
		{
			for (int32 Signs = 0; Signs < 8; ++Signs)
			{
				FAxes A;
				for (int32 I = 0; I < 3; ++I)
					for (int32 J = 0; J < 3; ++J) A.M[I][J] = 0;
				for (int32 I = 0; I < 3; ++I) A.M[I][P[I]] = (Signs & (1 << I)) ? -1.0 : 1.0;
				// Many UE bones have no Daz joint (face rig, helper bones), so the fit scores and refines on inliers only:
				// UE joints with a Daz joint within InlierCm. Each pass refits scale and offset on the inliers by least
				// squares; the last pass only scores.
				constexpr int32 Passes = 6;
				for (int32 Pass = 0; Pass < Passes; ++Pass)
				{
					TArray<double> Distances;
					TArray<TPair<FVector, FVector>> Pairs;
					for (const int32 I : Indices)
					{
						FName N, N2; double D, D2;
						const FVector UE = MeshRest[I].GetTranslation();
						Nearest(A, DazRest, UE, UESide(Ref.GetBoneName(I)), N, D, N2, D2);
						if (N.IsNone() || D > MatchCm) continue;
						Distances.Add(D);
						Pairs.Emplace(UE, Apply(A.M, DazRest.Bones[N].Pos));
					}
					A.Inliers = Distances.Num();
					Distances.Sort();
					A.MedianCm = Distances.Num() ? Distances[Distances.Num() / 2] : TNumericLimits<double>::Max();
					if (Pass == Passes - 1 || Pairs.Num() < 10) break;
					FVector CU = FVector::ZeroVector, CD = FVector::ZeroVector;
					for (const TPair<FVector, FVector>& P2 : Pairs) { CU += P2.Key; CD += P2.Value; }
					CU /= Pairs.Num(); CD /= Pairs.Num();
					double Num = 0.0, Den = 0.0;
					for (const TPair<FVector, FVector>& P2 : Pairs)
					{
						Num += FVector::DotProduct(P2.Key - CU, P2.Value - CD);
						Den += (P2.Value - CD).SizeSquared();
					}
					A.Scale = Den > UE_SMALL_NUMBER && Num > 0.0 ? Num / Den : 1.0;
					A.Offset = CU - CD * A.Scale;
				}
				if (A.Inliers > Best.Inliers || (A.Inliers == Best.Inliers && A.MedianCm < Best.MedianCm)) Best = A;
			}
		}
		return Best;
	}

	FMapping MapBones(const FAxes& Axes, const FDTHDazState& DazRest, const FReferenceSkeleton& Ref,
		const TArray<FTransform>& MeshRest, double MaxCm)
	{
		FMapping Map;
		const int32 Num = Ref.GetNum();
		Map.Daz.Init(NAME_None, Num);
		Map.DistanceCm.Init(-1.0, Num);
		Map.SecondDaz.Init(NAME_None, Num);
		Map.SecondCm.Init(-1.0, Num);
		for (int32 I = 0; I < Num; ++I)
		{
			if (IsSkipped(Ref.GetBoneName(I))) continue;
			FName N, N2; double D, D2;
			Nearest(Axes, DazRest, MeshRest[I].GetTranslation(), UESide(Ref.GetBoneName(I)), N, D, N2, D2);
			Map.DistanceCm[I] = D;
			Map.SecondDaz[I] = N2;
			Map.SecondCm[I] = D2;
			if (D <= MaxCm) Map.Daz[I] = N;
		}
		// A UE bone with no Daz joint at its position (DTH's skeleton conversion places a few itself) takes the Daz bone
		// that is the parent of what all its mapped children map to. Distance -1 marks a mapping by hierarchy.
		for (int32 I = 0; I < Num; ++I)
		{
			if (!Map.Daz[I].IsNone() || IsSkipped(Ref.GetBoneName(I))) continue;
			FName Parent = NAME_None;
			bool bAgree = true;
			for (int32 Child = 0; Child < Num && bAgree; ++Child)
			{
				if (Ref.GetParentIndex(Child) != I || Map.Daz[Child].IsNone() || Map.DistanceCm[Child] < 0.0) continue;
				const FDTHDazBone* D = DazRest.Bones.Find(Map.Daz[Child]);
				const FName P = D ? D->Parent : NAME_None;
				if (P.IsNone()) bAgree = false;
				else if (Parent.IsNone()) Parent = P;
				else bAgree = Parent == P;
			}
			if (bAgree && !Parent.IsNone())
			{
				Map.Daz[I] = Parent;
				Map.DistanceCm[I] = -1.0;
			}
		}
		return Map;
	}

	TMap<FName, FTransform> Solve(const FRig& Rig, const FReferenceSkeleton& Ref, const FDTHDazState& Pose,
		TArray<FTransform>* OutComponent)
	{
		const TArray<FTransform>& RefLocal = Ref.GetRefBonePose();
		const int32 Num = Ref.GetNum();

		// The Daz bone's world rotation change since UnrealPose, per mapped UE bone.
		auto DeltaOf = [&](FName DazName, FQuat& OutDelta)
		{
			const FDTHDazBone* Now = DazName.IsNone() ? nullptr : Pose.Bones.Find(DazName);
			const FDTHDazBone* Rest = DazName.IsNone() ? nullptr : Rig.DazRest.Bones.Find(DazName);
			if (!Now || !Rest) return false;
			OutDelta = Rig.Axes.Rotation(Now->Rot) * Rig.Axes.Rotation(Rest->Rot).Inverse();
			return true;
		};
		// The UE bone standing for each Daz bone (a joint match before a match by hierarchy).
		TMap<FName, int32> UEOf;
		for (int32 I = 0; I < Num; ++I)
		{
			const FName D = Rig.Map.Daz.IsValidIndex(I) ? Rig.Map.Daz[I] : NAME_None;
			if (D.IsNone()) continue;
			const int32* Have = UEOf.Find(D);
			auto Rank = [&Rig](int32 B) { return Rig.Map.DistanceCm[B] < 0.0 ? TNumericLimits<double>::Max() : Rig.Map.DistanceCm[B]; };
			if (!Have || Rank(I) < Rank(*Have)) UEOf.Add(D, I);
		}

		// Rotations, parents first: mapped bones take the Daz change on their bind pose, the others follow their parent.
		TArray<FQuat> Rot;
		Rot.SetNum(Num);
		TArray<bool> bMapped;
		bMapped.Init(false, Num);
		for (int32 I = 0; I < Num; ++I)
		{
			const int32 Parent = Ref.GetParentIndex(I);
			FQuat Delta;
			if (Parent != INDEX_NONE && Rig.Map.Daz.IsValidIndex(I) && DeltaOf(Rig.Map.Daz[I], Delta))
			{
				Rot[I] = (Delta * Rig.MeshRest[I].GetRotation()).GetNormalized();
				bMapped[I] = true;
			}
			else
			{
				Rot[I] = Parent == INDEX_NONE ? RefLocal[I].GetRotation() : (Rot[Parent] * RefLocal[I].GetRotation()).GetNormalized();
			}
		}

		// Positions follow Daz's own hierarchy (which differs from the Mannequin's around the hip). First every Daz joint
		// on the character: at rest where its UE joint is (or, for the few joints DTH moves, where the library joint lands
		// on the character), then carried by its Daz parent's rotation change; the root Daz bone (hip) moves by its own
		// translation. Only rotations and the hip translation come from the library, never its proportions.
		TMap<FName, FVector> DazRestPos, DazNowPos;
		TFunction<bool(FName)> PlaceDaz = [&](FName D) -> bool
		{
			if (DazNowPos.Contains(D)) return true;
			const FDTHDazBone* Rest = Rig.DazRest.Bones.Find(D);
			const FDTHDazBone* Now = Pose.Bones.Find(D);
			if (!Rest || !Now) return false;
			const int32* UE = UEOf.Find(D);
			FVector AtRest;
			if (UE && Rig.Map.DistanceCm[*UE] >= 0.0)
			{
				AtRest = Rig.MeshRest[*UE].GetTranslation();
			}
			else
			{
				// No UE joint here: anchor at the nearest Daz joint that has one, plus the library's (short) offset.
				const FDTHDazBone* Anchor = nullptr;
				int32 AnchorUE = INDEX_NONE;
				double Best = TNumericLimits<double>::Max();
				for (const TPair<FName, int32>& A : UEOf)
				{
					const FDTHDazBone* B = Rig.Map.DistanceCm[A.Value] >= 0.0 ? Rig.DazRest.Bones.Find(A.Key) : nullptr;
					const double Dist = B ? FVector::Dist(B->Pos, Rest->Pos) : Best;
					if (Dist < Best) { Best = Dist; Anchor = B; AnchorUE = A.Value; }
				}
				AtRest = Anchor ? Rig.MeshRest[AnchorUE].GetTranslation() + Rig.LibraryAxes.Position(Rest->Pos) - Rig.LibraryAxes.Position(Anchor->Pos)
					: Rig.LibraryAxes.Position(Rest->Pos);
			}
			FQuat ParentDelta;
			FVector AtNow;
			if (!Rest->Parent.IsNone() && PlaceDaz(Rest->Parent) && DeltaOf(Rest->Parent, ParentDelta))
			{
				AtNow = DazNowPos[Rest->Parent] + ParentDelta.RotateVector(AtRest - DazRestPos[Rest->Parent]);
			}
			else
			{
				AtNow = AtRest + Rig.Axes.Position(Now->Pos) - Rig.Axes.Position(Rest->Pos);
			}
			DazRestPos.Add(D, AtRest);
			DazNowPos.Add(D, AtNow);
			return true;
		};
		// Then each mapped UE joint, as a point fixed to the frame its Daz bone hangs from (the root Daz bone: its own).
		// Unmapped joints follow their UE parent.
		TArray<FVector> Pos;
		Pos.SetNum(Num);
		for (int32 I = 0; I < Num; ++I)
		{
			const int32 Parent = Ref.GetParentIndex(I);
			const FDTHDazBone* Rest = bMapped[I] ? Rig.DazRest.Bones.Find(Rig.Map.Daz[I]) : nullptr;
			const FName Carrier = Rest ? (Rest->Parent.IsNone() ? Rig.Map.Daz[I] : Rest->Parent) : NAME_None;
			FQuat CarrierDelta;
			if (Parent == INDEX_NONE)
			{
				Pos[I] = RefLocal[I].GetTranslation();
			}
			else if (!Carrier.IsNone() && PlaceDaz(Carrier) && DeltaOf(Carrier, CarrierDelta))
			{
				Pos[I] = DazNowPos[Carrier] + CarrierDelta.RotateVector(Rig.MeshRest[I].GetTranslation() - DazRestPos[Carrier]);
			}
			else
			{
				Pos[I] = Pos[Parent] + Rot[Parent].RotateVector(RefLocal[I].GetTranslation());
			}
		}

		TArray<FTransform> Comp;
		Comp.SetNum(Num);
		TMap<FName, FTransform> Local;
		for (int32 I = 0; I < Num; ++I)
		{
			Comp[I] = FTransform(Rot[I], Pos[I], RefLocal[I].GetScale3D());
			const int32 Parent = Ref.GetParentIndex(I);
			Local.Add(Ref.GetBoneName(I), Parent == INDEX_NONE ? RefLocal[I] : Comp[I].GetRelativeTransform(Comp[Parent]));
		}
		if (OutComponent) *OutComponent = MoveTemp(Comp);
		return Local;
	}
}

namespace DTHPoseSolve
{
	FAxes DazToUE()
	{
		FAxes A;
		for (int32 I = 0; I < 3; ++I)
			for (int32 J = 0; J < 3; ++J) A.M[I][J] = 0;
		A.M[0][0] = 1; A.M[1][2] = 1; A.M[2][1] = 1;
		return A;
	}

	FMapping MapFromTable(const FReferenceSkeleton& Ref)
	{
		FMapping Map;
		const int32 Num = Ref.GetNum();
		Map.Daz.Init(NAME_None, Num);
		Map.DistanceCm.Init(-1.0, Num);
		Map.SecondDaz.Init(NAME_None, Num);
		Map.SecondCm.Init(-1.0, Num);
		for (const DTHBoneMap::FEntry& E : DTHBoneMap::Table())
		{
			const int32 I = Ref.FindBoneIndex(E.UE);
			if (I == INDEX_NONE) continue;
			Map.Daz[I] = E.Daz;
			// A joint match counts as on the Daz joint; -1 marks a mapping by hierarchy, as MapBones does.
			Map.DistanceCm[I] = E.bJoint ? 0.0 : -1.0;
		}
		return Map;
	}
}
