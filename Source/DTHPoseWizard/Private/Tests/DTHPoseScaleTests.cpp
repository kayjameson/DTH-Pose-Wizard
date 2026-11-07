// Scale: a library of 100,000 presets, as the sampler writes one (version 2, one samples file per 100 presets), made from
// the settings' Daz samples. The build, the asset on disk and reading poses back from it are measured. Everything it
// writes (the samples under Saved, the library under the test folder) is deleted at the end.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Library/DTHPoseLibrary.h"
#include "Library/DTHPoseLibraryBuild.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Settings/DTHPoseSettings.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	constexpr int32 ScalePoses = 100000;
	constexpr int32 PerFile = 100;

	bool ReadJson(const FString& File, TSharedPtr<FJsonValue>& Out)
	{
		FString Text;
		return FFileHelper::LoadFileToString(Text, *File) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out) && Out.IsValid();
	}

	bool WriteJson(const FString& File, const TSharedRef<FJsonValue>& Value)
	{
		FString Text;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		return FJsonSerializer::Serialize(Value, TEXT(""), Writer) && FFileHelper::SaveStringToFile(Text, *File);
	}

	double MB(uint64 Bytes) { return double(Bytes) / (1024.0 * 1024.0); }

	uint32 Hash(const FDTHPoseData& D)
	{
		uint32 H = 0;
		for (const FQuat& Q : D.Rotations) H = HashCombine(H, GetTypeHash(Q.ToString()));
		for (const FVector& P : D.Positions) H = HashCombine(H, GetTypeHash(P.ToString()));
		for (const int32 M : D.Moved) H = HashCombine(H, GetTypeHash(M));
		return H;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1LibraryScale, "DTH.Pose.P1.LibraryScale", Flags)
bool FDTHPoseP1LibraryScale::RunTest(const FString&)
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	const FString Source = Settings->SamplesFile.FilePath;
	TSharedPtr<FJsonValue> SourceValue;
	if (!ReadJson(Source, SourceValue) || !SourceValue->AsObject().IsValid())
	{
		AddError(TEXT("Set Daz Samples File to the sampler's DTH_PoseSamples.json."));
		return false;
	}
	const TSharedPtr<FJsonObject> Root = SourceValue->AsObject();
	const FString SourceDir = FPaths::GetPath(Source);

	// The presets to copy (the loaded ones) and the ROM, from either layout.
	TArray<TSharedPtr<FJsonValue>> Templates;
	const TArray<TSharedPtr<FJsonValue>>* Inline = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* Chunks = nullptr;
	TSharedPtr<FJsonValue> FirstChunk;
	if (Root->TryGetArrayField(TEXT("poses"), Inline)) {}
	else if (Root->TryGetArrayField(TEXT("chunks"), Chunks) && Chunks->Num() > 0 && ReadJson(SourceDir / (*Chunks)[0]->AsString(), FirstChunk)) Inline = &FirstChunk->AsArray();
	for (int32 I = 0; Inline && I < Inline->Num(); ++I)
	{
		const TSharedPtr<FJsonObject> P = (*Inline)[I]->AsObject();
		if (P.IsValid() && P->GetBoolField(TEXT("loaded"))) Templates.Add((*Inline)[I]);
	}
	TSharedPtr<FJsonValue> Rom = Root->TryGetField(TEXT("rom"));
	FString RomFile;
	if (Rom.IsValid() && Rom->TryGetString(RomFile) && !ReadJson(SourceDir / RomFile, Rom)) Rom.Reset();
	if (Templates.IsEmpty() || !Rom.IsValid() || !Root->HasField(TEXT("rest")))
	{
		AddError(TEXT("The samples have no loaded presets, no ROM or no rest state."));
		return false;
	}

	// The sampler's layout: a manifest and one samples file per 100 presets, each preset a copy with its own label.
	const FString Dir = UDTHPoseSettings::SavedDir() / TEXT("scale");
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	IFileManager::Get().MakeDirectory(*(Dir / TEXT("poses")), true);
	double Start = FPlatformTime::Seconds();
	TArray<TSharedPtr<FJsonValue>> ChunkNames;
	for (int32 First = 0; First < ScalePoses; First += PerFile)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 I = First; I < FMath::Min(ScalePoses, First + PerFile); ++I)
		{
			const TSharedPtr<FJsonObject> Copy = MakeShared<FJsonObject>(*Templates[I % Templates.Num()]->AsObject());
			Copy->SetStringField(TEXT("label"), FString::Printf(TEXT("%s #%d"), *Copy->GetStringField(TEXT("label")), I));
			Out.Add(MakeShared<FJsonValueObject>(Copy));
		}
		const FString Name = FString::Printf(TEXT("poses/poses_%06d.json"), First / PerFile);
		if (!WriteJson(Dir / Name, MakeShared<FJsonValueArray>(Out))) { AddError(TEXT("Could not write the scale samples.")); return false; }
		ChunkNames.Add(MakeShared<FJsonValueString>(Name));
	}
	const TSharedRef<FJsonObject> Manifest = MakeShared<FJsonObject>();
	Manifest->SetStringField(TEXT("format"), TEXT("DTH Pose Samples"));
	Manifest->SetNumberField(TEXT("version"), 2);
	Manifest->SetStringField(TEXT("sampledAt"), TEXT("scale test"));
	Manifest->SetStringField(TEXT("dazVersion"), Root->GetStringField(TEXT("dazVersion")));
	Manifest->SetNumberField(TEXT("presets"), ScalePoses);
	Manifest->SetField(TEXT("rest"), Root->TryGetField(TEXT("rest")));
	Manifest->SetField(TEXT("rom"), Rom);
	Manifest->SetArrayField(TEXT("chunks"), ChunkNames);
	Manifest->SetArrayField(TEXT("skipped"), TArray<TSharedPtr<FJsonValue>>());
	const FString ManifestFile = Dir / TEXT("DTH_PoseSamples.json");
	WriteJson(ManifestFile, MakeShared<FJsonValueObject>(Manifest));
	int64 SamplesBytes = 0;
	IFileManager::Get().IterateDirectoryStatRecursively(*Dir, [&SamplesBytes](const TCHAR*, const FFileStatData& Stat) { SamplesBytes += Stat.bIsDirectory ? 0 : Stat.FileSize; return true; });
	AddInfo(FString::Printf(TEXT("Samples: %d presets in %d files, %.0f MB, written in %.1f s."), ScalePoses, ChunkNames.Num(), MB(SamplesBytes), FPlatformTime::Seconds() - Start));

	// The build.
	const FString LibraryPath = Settings->TestFolder.Path / TEXT("DA_Test_PoseLibraryScale");
	const FString PackageName = FPackageName::ObjectPathToPackageName(LibraryPath);
	const FString PackageFile = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	const uint64 MemBefore = FPlatformMemory::GetStats().UsedPhysical;
	Start = FPlatformTime::Seconds();
	TArray<FDTHPoseMessage> Report;
	UDTHPoseLibrary* Library = DTHPoseLibraryBuild::Build(ManifestFile, LibraryPath, Report);
	const double BuildSeconds = FPlatformTime::Seconds() - Start;
	for (const FDTHPoseMessage& M : Report) if (M.Severity == FDTHPoseMessage::ESeverity::Error) AddError(M.Text);
	if (!Library) return false;
	const uint64 MemBuilt = FPlatformMemory::GetStats().UsedPhysical;
	AddInfo(FString::Printf(TEXT("Build: %d poses in %.1f s; editor memory +%.0f MB; asset %.0f MB."), Library->Poses.Num(), BuildSeconds,
		MB(MemBuilt - FMath::Min(MemBuilt, MemBefore)), MB(IFileManager::Get().FileSize(*PackageFile))));
	if (Library->Poses.Num() != ScalePoses || Library->NumLoaded != ScalePoses) AddError(FString::Printf(TEXT("%d poses built, %d loaded."), Library->Poses.Num(), Library->NumLoaded));

	// Poses spread over the whole library, before and after the asset is read again from disk.
	TArray<int32> Picks;
	for (int32 I = 0; I < 200; ++I) Picks.Add(int32((int64(I) * 7919 * 104729) % ScalePoses));
	TArray<uint32> Before;
	for (const int32 P : Picks) { FDTHPoseData D; if (!Library->Data(P, D)) AddError(FString::Printf(TEXT("Pose %d has no data after the build."), P)); Before.Add(Hash(D)); }

	UPackage* Package = Library->GetOutermost();
	Library = nullptr;
	TArray<UPackage*> Unload{Package};
	UPackageTools::UnloadPackages(Unload);
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	const uint64 MemUnloaded = FPlatformMemory::GetStats().UsedPhysical;
	Start = FPlatformTime::Seconds();
	Library = LoadObject<UDTHPoseLibrary>(nullptr, *(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName)));
	const double LoadSeconds = FPlatformTime::Seconds() - Start;
	if (!Library || !Library->IsCurrent()) { AddError(TEXT("The library did not load back from disk with its pose data.")); return false; }
	const uint64 MemLoaded = FPlatformMemory::GetStats().UsedPhysical;
	Start = FPlatformTime::Seconds();
	for (int32 I = 0; I < Picks.Num(); ++I)
	{
		FDTHPoseData D;
		if (!Library->Data(Picks[I], D) || Hash(D) != Before[I]) AddError(FString::Printf(TEXT("Pose %d reads back differently from disk."), Picks[I]));
	}
	const double ReadMs = (FPlatformTime::Seconds() - Start) * 1000.0 / Picks.Num();
	const uint64 MemRead = FPlatformMemory::GetStats().UsedPhysical;
	AddInfo(FString::Printf(TEXT("From disk: loads in %.2f s (+%.0f MB: the list of poses); %d poses from all over the library in %.2f ms each, +%.0f MB after (a few blocks are kept)."),
		LoadSeconds, MB(MemLoaded - FMath::Min(MemLoaded, MemUnloaded)), Picks.Num(), ReadMs, MB(MemRead - FMath::Min(MemRead, MemLoaded))));
	const int32 Found = Library->FindPose(FString::Printf(TEXT("%s #%d"), *Templates[ScalePoses / 2 % Templates.Num()]->AsObject()->GetStringField(TEXT("label")), ScalePoses / 2));
	if (Found != ScalePoses / 2) AddError(FString::Printf(TEXT("FindPose found %d, not %d."), Found, ScalePoses / 2));

	// Clean up: the library and the samples.
	Package = Library->GetOutermost();
	Library = nullptr;
	Unload = {Package};
	UPackageTools::UnloadPackages(Unload);
	IFileManager::Get().Delete(*PackageFile);
	IAssetRegistry::Get()->ScanFilesSynchronous({PackageFile}, true);
	IFileManager::Get().DeleteDirectory(*Dir, false, true);
	return true;
}

#endif
