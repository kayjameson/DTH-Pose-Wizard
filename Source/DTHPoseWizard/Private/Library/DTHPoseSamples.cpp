#include "Library/DTHPoseSamples.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
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

bool FDTHPoseSamples::Load(const FString& File, FString& OutError)
{
	*this = FDTHPoseSamples();
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *File))
	{
		OutError = FString::Printf(TEXT("Could not read %s."), *File);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid()
		|| Root->GetStringField(TEXT("format")) != TEXT("DTH Pose Samples"))
	{
		OutError = FString::Printf(TEXT("%s is not a DTH Pose Samples file."), *File);
		return false;
	}
	DazVersion = Root->GetStringField(TEXT("dazVersion"));
	SampledAt = Root->GetStringField(TEXT("sampledAt"));
	if (!ReadState(Root->GetObjectField(TEXT("rest")), Rest))
	{
		OutError = TEXT("The samples file has no rest state.");
		return false;
	}
	const TSharedPtr<FJsonObject>* Rom = nullptr;
	const TSharedPtr<FJsonObject>* Frames = nullptr;
	if (Root->TryGetObjectField(TEXT("rom"), Rom) && (*Rom)->TryGetObjectField(TEXT("frames"), Frames))
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
	const TArray<TSharedPtr<FJsonValue>>* PoseArray = nullptr;
	if (Root->TryGetArrayField(TEXT("poses"), PoseArray))
	{
		for (const TSharedPtr<FJsonValue>& V : *PoseArray)
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			FDTHDazPose& Pose = Poses.AddDefaulted_GetRef();
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
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* SkippedArray = nullptr;
	if (Root->TryGetArrayField(TEXT("skipped"), SkippedArray))
	{
		for (const TSharedPtr<FJsonValue>& V : *SkippedArray)
		{
			const TSharedPtr<FJsonObject> S = V->AsObject();
			Skipped.Add(S->GetStringField(TEXT("file")) + TEXT(": ") + S->GetStringField(TEXT("reason")));
		}
	}
	return true;
}
