// The wizard itself, through its Python twins (unreal.DTHPoseScripting): open the tab, build the library, preview poses
// on the character (a hand pose over a seated base too), capture the real preview, write a pose sheet. Uses the
// settings' Testing values; the library and sequences go to the settings' Pose Library and Sequence Folder (point them
// at the test folder). Screens in Saved/DTHPoseWizard/phase1.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DTHPoseScripting.h"
#include "Misc/Paths.h"
#include "Settings/DTHPoseSettings.h"
#include "Tests/AutomationCommon.h"

namespace
{
	bool HasError(const TArray<FString>& Lines)
	{
		return Lines.ContainsByPredicate([](const FString& L) { return L.StartsWith(TEXT("ERROR:")); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDTHPoseP1Wizard, "DTH.Pose.P1.Wizard", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDTHPoseP1Wizard::RunTest(const FString&)
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	if (!Settings->LibraryAsset.StartsWith(Settings->TestFolder.Path) || !Settings->SequenceFolder.Path.StartsWith(Settings->TestFolder.Path))
	{
		AddError(TEXT("Point Pose Library and Sequence Folder at the Test Folder first: this test writes to them."));
		return false;
	}
	const FString Mesh = Settings->Character.ToString();
	const FString Dir = UDTHPoseSettings::SavedDir() / TEXT("phase1");

	const TArray<FString> Report = UDTHPoseScripting::BuildLibrary();
	for (const FString& L : Report) AddInfo(L);
	if (HasError(Report)) { AddError(TEXT("The library did not build.")); return false; }
	const TArray<FString> Poses = UDTHPoseScripting::ListPoses(TEXT(""));
	for (const FString& L : Poses) AddInfo(L);
	if (Poses.Num() < 3 || HasError(Poses)) { AddError(TEXT("The library lists fewer poses than sampled.")); return false; }
	const FString Seated = TEXT("CDI SVP G9B SitA");

	const FString Open = UDTHPoseScripting::OpenWizard(TEXT("Poses"));
	AddInfo(Open);
	if (Open.StartsWith(TEXT("ERROR:"))) { AddError(Open); return false; }
	AddInfo(UDTHPoseScripting::SelectCharacter(Mesh));

	struct FShot { FString Pose, Base, View; };
	const TArray<FShot> Shots = {
		{Seated, TEXT(""), TEXT("body")}, {Seated, TEXT(""), TEXT("threequarter")}, {Seated, TEXT(""), TEXT("face")},
		{TEXT("Kings Guard Gun LHand"), Seated, TEXT("hands")}, {TEXT("Kings Guard Gun LHand"), TEXT(""), TEXT("hands")},
		{TEXT("Action Essentials Genesis 9 Base 28 Standing"), TEXT(""), TEXT("body")} };
	// The tab and its viewport need a few frames to lay out and draw before a capture.
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.0f));
	for (const FShot& S : Shots)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S]()
		{
			const FString Line = UDTHPoseScripting::SelectPose(S.Pose, S.Base);
			AddInfo(Line);
			if (Line.StartsWith(TEXT("ERROR:"))) AddError(Line);
			return true;
		}));
		ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.0f));
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, S, Dir]()
		{
			const FString Name = FPaths::MakeValidFileName((S.Pose + (S.Base.IsEmpty() ? FString() : TEXT("_over_") + S.Base) + TEXT("_") + S.View).Replace(TEXT(" "), TEXT("_")));
			const FString Line = UDTHPoseScripting::CapturePreview(Dir / (Name + TEXT(".png")), S.View);
			AddInfo(Line);
			if (Line.StartsWith(TEXT("ERROR:"))) AddError(Line);
			return true;
		}));
	}
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([this, Mesh, Seated]()
	{
		const TArray<FString> Lines = UDTHPoseScripting::WriteSequence(Mesh, {Seated, TEXT("Kings Guard Gun LHand"), TEXT("Action Essentials Genesis 9 Base 28 Standing")},
			Seated, TEXT("A_WizardPoseSheet"));
		for (const FString& L : Lines) AddInfo(L);
		if (HasError(Lines)) AddError(TEXT("The pose sheet was not written."));
		return true;
	}));
	return true;
}

#endif
