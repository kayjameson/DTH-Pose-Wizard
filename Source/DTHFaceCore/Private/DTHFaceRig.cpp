#include "DTHFaceRig.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	constexpr int32 FormatVersion = 2;

	double Power(double X, int32 Exponent) { return Exponent == 2 ? X * X : X; }

	TArray<TSharedPtr<FJsonValue>> Strings(const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& S : In) Out.Add(MakeShared<FJsonValueString>(S));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> Names(const TArray<FName>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FName& N : In) Out.Add(MakeShared<FJsonValueString>(N.ToString()));
		return Out;
	}
}

bool FDTHFaceRig::Save(const FString& File) const
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("version"), FormatVersion);
	Root->SetStringField(TEXT("character"), Character);
	Root->SetStringField(TEXT("postProcessBlueprint"), PostProcessBlueprint);
	Root->SetStringField(TEXT("poseAsset"), PoseAsset);
	Root->SetArrayField(TEXT("fingerprint"), Strings(Fingerprint));
	Root->SetArrayField(TEXT("notes"), Strings(Notes));
	Root->SetArrayField(TEXT("curves"), Names(Curves));
	Root->SetArrayField(TEXT("poses"), Names(Poses));
	TArray<TSharedPtr<FJsonValue>> Exp, Lin, Pai;
	for (const int32 E : Exponent) Exp.Add(MakeShared<FJsonValueNumber>(E));
	for (const FLink& L : Linear)
	{
		Lin.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
			MakeShared<FJsonValueNumber>(L.Curve), MakeShared<FJsonValueNumber>(L.Pose), MakeShared<FJsonValueNumber>(L.Weight)}));
	}
	for (const FPair& P : Pairs)
	{
		Pai.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{
			MakeShared<FJsonValueNumber>(P.A), MakeShared<FJsonValueNumber>(P.B), MakeShared<FJsonValueNumber>(P.Pose), MakeShared<FJsonValueNumber>(P.Weight)}));
	}
	Root->SetArrayField(TEXT("exponent"), Exp);
	Root->SetArrayField(TEXT("linear"), Lin);
	Root->SetArrayField(TEXT("pairs"), Pai);

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	return FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(Text, *File);
}

bool FDTHFaceRig::Load(const FString& File, FString& OutError)
{
	*this = FDTHFaceRig();
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root)
		|| !Root.IsValid() || Root->GetIntegerField(TEXT("version")) != FormatVersion)
	{
		OutError = FString::Printf(TEXT("No usable face rig measurement in %s."), *File);
		return false;
	}
	Character = Root->GetStringField(TEXT("character"));
	PostProcessBlueprint = Root->GetStringField(TEXT("postProcessBlueprint"));
	PoseAsset = Root->GetStringField(TEXT("poseAsset"));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("fingerprint"))) Fingerprint.Add(V->AsString());
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("notes"))) Notes.Add(V->AsString());
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("curves"))) Curves.Add(FName(*V->AsString()));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("poses"))) Poses.Add(FName(*V->AsString()));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("exponent"))) Exponent.Add(static_cast<int32>(V->AsNumber()));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("linear")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		Linear.Add({static_cast<int32>(A[0]->AsNumber()), static_cast<int32>(A[1]->AsNumber()), A[2]->AsNumber()});
	}
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("pairs")))
	{
		const TArray<TSharedPtr<FJsonValue>>& A = V->AsArray();
		Pairs.Add({static_cast<int32>(A[0]->AsNumber()), static_cast<int32>(A[1]->AsNumber()), static_cast<int32>(A[2]->AsNumber()), A[3]->AsNumber()});
	}
	if (Curves.IsEmpty() || Poses.IsEmpty() || Exponent.Num() != Curves.Num())
	{
		OutError = FString::Printf(TEXT("The face rig measurement %s is incomplete."), *File);
		return false;
	}
	UpdateUndriven();
	return true;
}

void FDTHFaceRig::UpdateUndriven()
{
	TArray<bool> Driven;
	Driven.Init(false, Poses.Num());
	for (const FLink& L : Linear) Driven[L.Pose] = true;
	for (const FPair& P : Pairs) Driven[P.Pose] = true;
	Undriven.Reset();
	for (int32 P = 0; P < Poses.Num(); ++P)
	{
		if (!Driven[P]) Undriven.Add(P);
	}
}

void FDTHFaceRig::Evaluate(TConstArrayView<double> C, TArray<double>& OutWeights) const
{
	OutWeights.Init(0.0, Poses.Num());
	for (const FLink& L : Linear)
	{
		OutWeights[L.Pose] += L.Weight * Power(C[L.Curve], Exponent[L.Curve]);
	}
	for (const FPair& P : Pairs)
	{
		OutWeights[P.Pose] += P.Weight * Power(C[P.A], Exponent[P.A]) * Power(C[P.B], Exponent[P.B]);
	}
}
