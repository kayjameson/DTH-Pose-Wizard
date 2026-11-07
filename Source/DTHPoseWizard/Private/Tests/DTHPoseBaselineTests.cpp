#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/DTHPoseTestUtil.h"

/**
 * Checks that a code change left every written sequence as it was: each sequence under the baseline folder (passed as
 * -DTHPoseBaseline=/Game/...) is compared key by key with the one at the same place under Test Folder, written by the
 * same tests after the change. Run it after the tests that write sequences, in its own run. Without the switch it
 * only says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseMatchesBaselineTest, "DTH.Pose.Refactor.MatchesBaseline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDTHPoseMatchesBaselineTest::RunTest(const FString& Parameters)
{
	FString Baseline;
	if (!FParse::Value(FCommandLine::Get(), TEXT("DTHPoseBaseline="), Baseline))
	{
		AddInfo(TEXT("No -DTHPoseBaseline=<content folder> on the command line: nothing to compare."));
		return true;
	}
	FDTHPoseTestContext C;
	if (!C.Load(*this)) return false;
	Baseline.RemoveFromEnd(TEXT("/"));
	if (Baseline == C.TestFolder)
	{
		AddError(TEXT("The baseline folder is Test Folder itself."));
		return false;
	}

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	Registry.ScanPathsSynchronous({Baseline, C.TestFolder}, true);
	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*Baseline), Assets, true);

	TArray<FString> Lines;
	int32 Compared = 0, Differ = 0;
	double WorstDeg = 0.0, WorstCm = 0.0, WorstScale = 0.0;
	for (const FAssetData& A : Assets)
	{
		if (A.AssetClassPath != UAnimSequence::StaticClass()->GetClassPathName()) continue;
		const FString Path = A.PackageName.ToString();
		// DTH's own FBX imports are ground truth, not something the change writes.
		if (Path.StartsWith(Baseline / TEXT("Import/"))) continue;
		const FString Rel = Path.Mid(Baseline.Len());
		const FString OtherPath = C.TestFolder + Rel + TEXT(".") + A.AssetName.ToString();
		const UAnimSequence* Old = Cast<UAnimSequence>(A.GetAsset());
		const UAnimSequence* New = LoadObject<UAnimSequence>(nullptr, *OtherPath);
		if (!Old || !New)
		{
			AddError(FString::Printf(TEXT("%s: %s."), *Rel, New ? TEXT("baseline unreadable") : TEXT("not written after the change")));
			++Differ;
			continue;
		}
		++Compared;
		const int32 Keys = Old->GetNumberOfSampledKeys();
		if (Keys != New->GetNumberOfSampledKeys() || Old->GetSamplingFrameRate() != New->GetSamplingFrameRate())
		{
			AddError(FString::Printf(TEXT("%s: %d keys at %s before, %d at %s after."), *Rel, Keys, *Old->GetSamplingFrameRate().ToPrettyText().ToString(),
				New->GetNumberOfSampledKeys(), *New->GetSamplingFrameRate().ToPrettyText().ToString()));
			++Differ;
			continue;
		}
		bool bSame = true;
		for (int32 K = 0; K < Keys; ++K)
		{
			const TMap<FName, FTransform> P = DTHPoseTest::TrackPose(*Old, K);
			const TMap<FName, FTransform> Q = DTHPoseTest::TrackPose(*New, K);
			if (P.Num() != Q.Num()) bSame = false;
			for (const TPair<FName, FTransform>& B : P)
			{
				const FTransform* T = Q.Find(B.Key);
				if (!T)
				{
					bSame = false;
					continue;
				}
				const double Deg = DTHPoseTest::AngleDeg(B.Value.GetRotation(), T->GetRotation());
				const double Cm = FVector::Dist(B.Value.GetTranslation(), T->GetTranslation());
				const double Scale = (B.Value.GetScale3D() - T->GetScale3D()).GetAbsMax();
				WorstDeg = FMath::Max(WorstDeg, Deg);
				WorstCm = FMath::Max(WorstCm, Cm);
				WorstScale = FMath::Max(WorstScale, Scale);
				if (Deg > 1e-6 || Cm > 1e-6 || Scale > 1e-9) bSame = false;
			}
		}
		Lines.Add(FString::Printf(TEXT("%s %s (%d keys)"), bSame ? TEXT("same   ") : TEXT("DIFFERS"), *Rel, Keys));
		if (!bSame)
		{
			AddError(FString::Printf(TEXT("%s differs from the baseline."), *Rel));
			++Differ;
		}
	}
	Lines.Insert(FString::Printf(TEXT("Baseline %s vs %s: %d sequences compared, %d differ. Largest change %.3g deg, %.3g cm, scale %.3g."),
		*Baseline, *C.TestFolder, Compared, Differ, WorstDeg, WorstCm, WorstScale), 0);
	AddInfo(Lines[0]);
	AddInfo(TEXT("Report: ") + DTHPoseTest::WriteReport(TEXT("matches_baseline.txt"), Lines));
	if (Compared == 0)
	{
		AddError(FString::Printf(TEXT("No sequences under %s to compare."), *Baseline));
		return false;
	}
	return Differ == 0;
}

#endif
