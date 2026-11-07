#include "DTHPoseSamples.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace DTHDazSamples
{
	bool ReadState(const TSharedPtr<FJsonObject>& Object, FDTHDazState& Out)
	{
		Out = FDTHDazState();
		const TSharedPtr<FJsonObject>* Bones = nullptr;
		if (!Object.IsValid() || !Object->TryGetObjectField(TEXT("bones"), Bones))
		{
			return false;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& B : (*Bones)->Values)
		{
			const TSharedPtr<FJsonObject> Bone = B.Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* Pos = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Rot = nullptr;
			if (!Bone.IsValid() || !Bone->TryGetArrayField(TEXT("pos"), Pos) || !Bone->TryGetArrayField(TEXT("rot"), Rot)
				|| Pos->Num() != 3 || Rot->Num() != 4)
			{
				return false;
			}
			FDTHDazBone& D = Out.Bones.Add(FName(*B.Key));
			D.Parent = FName(*Bone->GetStringField(TEXT("parent")));
			D.Pos = FVector((*Pos)[0]->AsNumber(), (*Pos)[1]->AsNumber(), (*Pos)[2]->AsNumber());
			// DzNode::getWSRot() gives the inverse of the active rotation: its conjugate is what carries the child joints
			// (measured on ROM frames 3, 8, 10, 36: the conjugate moves them exactly, the quaternion as given does not;
			// docs/pose-wizard-research/phase0/p0_daz_results.md).
			D.Rot = FQuat(-(*Rot)[0]->AsNumber(), -(*Rot)[1]->AsNumber(), -(*Rot)[2]->AsNumber(), (*Rot)[3]->AsNumber()).GetNormalized();
			const TArray<TSharedPtr<FJsonValue>>* LocalChannels = nullptr;
			if (Bone->TryGetArrayField(TEXT("local"), LocalChannels))
			{
				for (int32 C = 0; C < 6 && C < LocalChannels->Num(); ++C) D.Local[C] = float((*LocalChannels)[C]->AsNumber());
			}
		}
		const TSharedPtr<FJsonObject>* Morphs = nullptr;
		if (Object->TryGetObjectField(TEXT("morphs"), Morphs))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& M : (*Morphs)->Values)
			{
				Out.Morphs.Add(FName(*M.Key), float(M.Value->AsNumber()));
			}
		}
		return true;
	}
}

namespace
{
	using DTHDazSamples::ReadState;

	bool ReadJson(const FString& File, TSharedPtr<FJsonValue>& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *File))
		{
			OutError = FString::Printf(TEXT("Could not read %s."), *File);
			return false;
		}
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) || !Out.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not valid JSON."), *File);
			return false;
		}
		return true;
	}

	bool ReadPose(const TSharedPtr<FJsonValue>& V, FDTHDazPose& Pose)
	{
		const TSharedPtr<FJsonObject> P = V.IsValid() ? V->AsObject() : nullptr;
		if (!P.IsValid()) return false;
		Pose.Pack = P->GetStringField(TEXT("pack"));
		Pose.Label = P->GetStringField(TEXT("label"));
		Pose.File = P->GetStringField(TEXT("file"));
		P->TryGetStringField(TEXT("icon"), Pose.Icon);
		P->TryGetStringField(TEXT("type"), Pose.Type);
		P->TryGetStringField(TEXT("reason"), Pose.Reason);
		Pose.bLoaded = P->GetBoolField(TEXT("loaded"));
		const TSharedPtr<FJsonObject>* State = nullptr;
		if (Pose.bLoaded && P->TryGetObjectField(TEXT("state"), State))
		{
			Pose.bLoaded = ReadState(*State, Pose.State);
		}
		return true;
	}

	/** Version 1 keeps the poses in the samples file itself; version 2 in its samples files, one array each. */
	bool VisitArray(const TArray<TSharedPtr<FJsonValue>>& Poses, TFunctionRef<bool(FDTHDazPose&&)> Visit)
	{
		for (const TSharedPtr<FJsonValue>& V : Poses)
		{
			FDTHDazPose Pose;
			if (ReadPose(V, Pose) && !Visit(MoveTemp(Pose))) return false;
		}
		return true;
	}
}

bool FDTHPoseSamples::Load(const FString& InFile, FString& OutError)
{
	*this = FDTHPoseSamples();
	File = InFile;
	TSharedPtr<FJsonValue> Value;
	if (!ReadJson(File, Value, OutError)) return false;
	const TSharedPtr<FJsonObject> Root = Value->AsObject();
	if (!Root.IsValid() || Root->GetStringField(TEXT("format")) != TEXT("DTH Pose Samples"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH Pose Samples file."), *File);
		return false;
	}
	const FString Dir = FPaths::GetPath(File);
	DazVersion = Root->GetStringField(TEXT("dazVersion"));
	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	if (!ReadState(Root->GetObjectField(TEXT("rest")), Rest))
	{
		OutError = TEXT("The samples file has no rest state.");
		return false;
	}

	// The ROM: inline (version 1) or a file next to this one (version 2).
	TSharedPtr<FJsonObject> Rom;
	FString RomFile;
	if (Root->TryGetStringField(TEXT("rom"), RomFile))
	{
		TSharedPtr<FJsonValue> RomValue;
		if (!RomFile.IsEmpty())
		{
			if (!ReadJson(Dir / RomFile, RomValue, OutError)) return false;
			Rom = RomValue->AsObject();
		}
	}
	else
	{
		const TSharedPtr<FJsonObject>* Inline = nullptr;
		if (Root->TryGetObjectField(TEXT("rom"), Inline)) Rom = *Inline;
	}
	const TSharedPtr<FJsonObject>* Frames = nullptr;
	if (Rom.IsValid() && Rom->TryGetObjectField(TEXT("frames"), Frames))
	{
		for (int32 F = 0; (*Frames)->HasField(FString::FromInt(F)); ++F)
		{
			if (!ReadState((*Frames)->GetObjectField(FString::FromInt(F)), RomFrames.AddDefaulted_GetRef()))
			{
				OutError = FString::Printf(TEXT("ROM frame %d is malformed."), F);
				return false;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ChunkArray = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* PoseArray = nullptr;
	if (Root->TryGetArrayField(TEXT("chunks"), ChunkArray))
	{
		for (const TSharedPtr<FJsonValue>& V : *ChunkArray) Chunks.Add(Dir / V->AsString());
		NumPoses = int32(Root->GetNumberField(TEXT("presets")));
	}
	else if (Root->TryGetArrayField(TEXT("poses"), PoseArray))
	{
		NumPoses = PoseArray->Num();
	}

	// Skipped files: inline (version 1) or a file next to this one (version 2).
	TSharedPtr<FJsonValue> SkippedValue;
	FString SkippedFile;
	const TArray<TSharedPtr<FJsonValue>>* SkippedArray = nullptr;
	if (Root->TryGetStringField(TEXT("skipped"), SkippedFile))
	{
		if (!ReadJson(Dir / SkippedFile, SkippedValue, OutError)) return false;
		SkippedArray = &SkippedValue->AsArray();
	}
	else
	{
		Root->TryGetArrayField(TEXT("skipped"), SkippedArray);
	}
	for (int32 I = 0; SkippedArray && I < SkippedArray->Num(); ++I)
	{
		const TSharedPtr<FJsonObject> S = (*SkippedArray)[I]->AsObject();
		if (S.IsValid()) Skipped.Add(S->GetStringField(TEXT("file")) + TEXT(": ") + S->GetStringField(TEXT("reason")));
	}
	return true;
}

bool FDTHPoseSamples::ReadPoses(TFunctionRef<bool(FDTHDazPose&&)> Visit, FString& OutError) const
{
	if (Chunks.IsEmpty())
	{
		TSharedPtr<FJsonValue> Value;
		if (!ReadJson(File, Value, OutError)) return false;
		const TArray<TSharedPtr<FJsonValue>>* Poses = nullptr;
		if (Value->AsObject().IsValid() && Value->AsObject()->TryGetArrayField(TEXT("poses"), Poses)) VisitArray(*Poses, Visit);
		return true;
	}
	for (const FString& Chunk : Chunks)
	{
		TSharedPtr<FJsonValue> Value;
		if (!ReadJson(Chunk, Value, OutError)) return false;
		const TArray<TSharedPtr<FJsonValue>>* Poses = nullptr;
		if (!Value->TryGetArray(Poses))
		{
			OutError = FString::Printf(TEXT("%s is not a list of samples."), *Chunk);
			return false;
		}
		if (!VisitArray(*Poses, Visit)) return true;
	}
	return true;
}

bool FDTHPoseSamples::ReadPoses(TArray<FDTHDazPose>& Out, TFunctionRef<bool(const FString& Label)> Wanted, FString& OutError) const
{
	Out.Reset();
	return ReadPoses([&Out, &Wanted](FDTHDazPose&& Pose)
	{
		if (Wanted(Pose.Label)) Out.Add(MoveTemp(Pose));
		return true;
	}, OutError);
}
