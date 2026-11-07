#include "Library/DTHPoseLibrary.h"

namespace
{
	/** Blocks of pose data kept in memory at once (each holds 256 poses, about 1 MB). */
	constexpr int32 CachedBlocks = 8;
	/** Per bone: rotation x, y, z, w and position x, y, z. */
	constexpr int32 FloatsPerBone = 7;
}

int32 UDTHPoseLibrary::RecordBytes() const
{
	// The bones' floats, then one bit per bone for the moved ones (padded so the next record's floats stay aligned).
	return Bones.Num() * FloatsPerBone * int32(sizeof(float)) + Align((Bones.Num() + 7) / 8, int32(sizeof(float)));
}

void UDTHPoseLibrary::BeginBuild()
{
	Poses.Reset();
	Slots.Reset();
	Blocks.Empty();
	Writing.Reset();
	NumRecords = 0;
	NumLoaded = NumPartial = NumWithMissing = 0;
	Cache.Reset();
	LabelIndex.Reset();
	Format = CurrentFormat;
}

void UDTHPoseLibrary::AddPose(FDTHPose&& Pose, const FDTHPoseData* PoseData)
{
	NumLoaded += Pose.bLoaded ? 1 : 0;
	NumPartial += Pose.bLoaded && Pose.bPartial ? 1 : 0;
	NumWithMissing += Pose.Missing.IsEmpty() ? 0 : 1;
	Poses.Add(MoveTemp(Pose));
	if (!PoseData)
	{
		Slots.Add(INDEX_NONE);
		return;
	}
	Slots.Add(NumRecords++);
	const int32 Start = Writing.AddZeroed(RecordBytes());
	float* F = reinterpret_cast<float*>(Writing.GetData() + Start);
	for (int32 I = 0; I < Bones.Num(); ++I, F += FloatsPerBone)
	{
		const FQuat Q = PoseData->Rotations.IsValidIndex(I) ? PoseData->Rotations[I] : FQuat::Identity;
		const FVector P = PoseData->Positions.IsValidIndex(I) ? PoseData->Positions[I] : FVector::ZeroVector;
		F[0] = float(Q.X); F[1] = float(Q.Y); F[2] = float(Q.Z); F[3] = float(Q.W);
		F[4] = float(P.X); F[5] = float(P.Y); F[6] = float(P.Z);
	}
	uint8* Bits = reinterpret_cast<uint8*>(F);
	for (const int32 M : PoseData->Moved) if (M >= 0 && M < Bones.Num()) Bits[M / 8] |= uint8(1 << (M % 8));
	if (NumRecords % PosesPerBlock == 0) FlushBlock();
}

void UDTHPoseLibrary::FlushBlock()
{
	if (Writing.IsEmpty()) return;
	FByteBulkData* Block = new FByteBulkData();
	Block->Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Block->Realloc(Writing.Num()), Writing.GetData(), Writing.Num());
	Block->Unlock();
	Blocks.Add(Block);
	Writing.Reset();
}

void UDTHPoseLibrary::EndBuild()
{
	FlushBlock();
	Writing.Empty();
}

const uint8* UDTHPoseLibrary::Record(int32 Pose) const
{
	const int32 Slot = Slots.IsValidIndex(Pose) ? Slots[Pose] : INDEX_NONE;
	if (Slot == INDEX_NONE) return nullptr;
	const int32 Block = Slot / PosesPerBlock;
	const int32 Offset = (Slot % PosesPerBlock) * RecordBytes();
	if (Block == Blocks.Num())
	{
		// still being written (during a build)
		return Writing.IsValidIndex(Offset) ? Writing.GetData() + Offset : nullptr;
	}
	if (!Blocks.IsValidIndex(Block)) return nullptr;
	for (int32 I = 0; I < Cache.Num(); ++I)
	{
		if (Cache[I].Key != Block) continue;
		if (I != Cache.Num() - 1)
		{
			TPair<int32, TArray<uint8>> Hit = MoveTemp(Cache[I]);
			Cache.RemoveAt(I);
			Cache.Add(MoveTemp(Hit));
		}
		const TArray<uint8>& Bytes = Cache.Last().Value;
		return Bytes.IsValidIndex(Offset + RecordBytes() - 1) ? Bytes.GetData() + Offset : nullptr;
	}
	if (Cache.Num() >= CachedBlocks) Cache.RemoveAt(0);
	// GetCopy lets go of the bulk data's own copy once it can be read from the asset again.
	FByteBulkData& Bulk = const_cast<FByteBulkData&>(Blocks[Block]);
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(int32(Bulk.GetBulkDataSize()));
	void* Dest = Bytes.GetData();
	Bulk.GetCopy(&Dest, true);
	Cache.Add(TPair<int32, TArray<uint8>>(Block, MoveTemp(Bytes)));
	const TArray<uint8>& Loaded = Cache.Last().Value;
	return Loaded.IsValidIndex(Offset + RecordBytes() - 1) ? Loaded.GetData() + Offset : nullptr;
}

bool UDTHPoseLibrary::Data(int32 Pose, FDTHPoseData& Out) const
{
	Out = FDTHPoseData();
	if (Pose == INDEX_NONE)
	{
		Out.Rotations = RestRotations;
		Out.Positions = RestPositions;
		return Out.Rotations.Num() == Bones.Num();
	}
	if (!Poses.IsValidIndex(Pose) || !Poses[Pose].bLoaded) return false;
	const uint8* R = Record(Pose);
	if (!R) return false;
	const float* F = reinterpret_cast<const float*>(R);
	Out.Rotations.SetNum(Bones.Num());
	Out.Positions.SetNum(Bones.Num());
	for (int32 I = 0; I < Bones.Num(); ++I, F += FloatsPerBone)
	{
		Out.Rotations[I] = FQuat(F[0], F[1], F[2], F[3]).GetNormalized();
		Out.Positions[I] = FVector(F[4], F[5], F[6]);
	}
	const uint8* Bits = reinterpret_cast<const uint8*>(F);
	for (int32 I = 0; I < Bones.Num(); ++I) if (Bits[I / 8] & (1 << (I % 8))) Out.Moved.Add(I);
	return true;
}

FDTHDazState UDTHPoseLibrary::State(int32 Pose) const
{
	FDTHDazState Out;
	FDTHPoseData D;
	if (!Data(Pose, D)) return Out;
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		FDTHDazBone& B = Out.Bones.Add(Bones[I]);
		B.Parent = Parents[I];
		B.Rot = D.Rotations[I];
		B.Pos = D.Positions[I];
	}
	return Out;
}

FDTHDazState UDTHPoseLibrary::Compose(int32 BasePose, int32 OverlayPose) const
{
	FDTHPoseData Base, Overlay;
	if (!Data(BasePose, Base) || !Data(OverlayPose, Overlay)) return FDTHDazState();
	TArray<bool> bMoved;
	bMoved.Init(false, Bones.Num());
	for (const int32 M : Overlay.Moved) if (bMoved.IsValidIndex(M)) bMoved[M] = true;
	TMap<FName, int32> Index;
	for (int32 I = 0; I < Bones.Num(); ++I) Index.Add(Bones[I], I);

	// Parents first: each bone keeps its pose relative to its parent from whichever preset sets it.
	TArray<FQuat> Rot;
	TArray<FVector> Pos;
	Rot.SetNum(Bones.Num());
	Pos.SetNum(Bones.Num());
	TArray<uint8> Done;
	Done.Init(0, Bones.Num());
	TFunction<void(int32)> Place = [&](int32 I)
	{
		if (Done[I]) return;
		const FDTHPoseData& From = bMoved[I] ? Overlay : Base;
		const int32* P = Parents[I].IsNone() ? nullptr : Index.Find(Parents[I]);
		if (!P)
		{
			Rot[I] = From.Rotations[I];
			Pos[I] = From.Positions[I];
		}
		else
		{
			Place(*P);
			const FQuat LocalRot = From.Rotations[*P].Inverse() * From.Rotations[I];
			const FVector LocalPos = From.Rotations[*P].UnrotateVector(From.Positions[I] - From.Positions[*P]);
			Rot[I] = (Rot[*P] * LocalRot).GetNormalized();
			Pos[I] = Pos[*P] + Rot[*P].RotateVector(LocalPos);
		}
		Done[I] = 1;
	};
	FDTHDazState Out;
	for (int32 I = 0; I < Bones.Num(); ++I)
	{
		Place(I);
		FDTHDazBone& B = Out.Bones.Add(Bones[I]);
		B.Parent = Parents[I];
		B.Rot = Rot[I];
		B.Pos = Pos[I];
	}
	return Out;
}

int32 UDTHPoseLibrary::FindPose(const FString& Label) const
{
	if (LabelIndex.Num() == 0 && Poses.Num() > 0)
	{
		LabelIndex.Reserve(Poses.Num());
		// The first pose with a label wins, as a search from the start would find it.
		for (int32 I = Poses.Num() - 1; I >= 0; --I) LabelIndex.Add(Poses[I].Label, I);
	}
	const int32* Found = LabelIndex.Find(Label);
	return Found ? *Found : INDEX_NONE;
}

void UDTHPoseLibrary::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	// The pose data goes with the asset on disk only (no undo, no reference walks); libraries from before it have none.
	if (Format != CurrentFormat || !Ar.IsPersistent() || Ar.IsTransacting() || Ar.IsObjectReferenceCollector() || Ar.IsCountingMemory())
	{
		return;
	}
	int32 Num = Blocks.Num();
	Ar << Num;
	if (Ar.IsLoading())
	{
		Blocks.Empty(Num);
		for (int32 I = 0; I < Num; ++I) Blocks.Add(new FByteBulkData());
		Writing.Reset();
		Cache.Reset();
		LabelIndex.Reset();
	}
	for (int32 I = 0; I < Num; ++I) Blocks[I].Serialize(Ar, this, I);
}

void UDTHPoseLibrary::TakeBuild(UDTHPoseLibrary& Built)
{
	Format = Built.Format;
	Bones = MoveTemp(Built.Bones);
	Parents = MoveTemp(Built.Parents);
	Rest = MoveTemp(Built.Rest);
	RestRotations = MoveTemp(Built.RestRotations);
	RestPositions = MoveTemp(Built.RestPositions);
	Poses = MoveTemp(Built.Poses);
	NumLoaded = Built.NumLoaded;
	NumPartial = Built.NumPartial;
	NumWithMissing = Built.NumWithMissing;
	Slots = MoveTemp(Built.Slots);
	NumRecords = Built.NumRecords;
	Blocks = MoveTemp(Built.Blocks);
	Writing.Reset();
	Cache.Reset();
	LabelIndex.Reset();
	Built.BeginBuild();
	Built.Format = 0;
}
