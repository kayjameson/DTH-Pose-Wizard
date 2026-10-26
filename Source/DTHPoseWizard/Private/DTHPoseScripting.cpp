#include "DTHPoseScripting.h"

#include "Animation/AnimSequence.h"
#include "Build/DTHPoseRig.h"
#include "DTHPoseWizardModule.h"
#include "Engine/SkeletalMesh.h"
#include "Framework/Docking/TabManager.h"
#include "Library/DTHPoseLibrary.h"
#include "Library/DTHPoseLibraryBuild.h"
#include "Misc/PackageName.h"
#include "Settings/DTHPoseSettings.h"
#include "UI/SDTHPoseWizard.h"

namespace
{
	UDTHPoseLibrary* LoadLibrary()
	{
		const FString& Path = GetDefault<UDTHPoseSettings>()->LibraryAsset;
		const FString Object = Path.Contains(TEXT(".")) ? Path : Path + TEXT(".") + FPackageName::GetShortName(Path);
		return Path.IsEmpty() ? nullptr : LoadObject<UDTHPoseLibrary>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}

	FString Line(const FDTHPoseMessage& M)
	{
		const TCHAR* Prefix = M.Severity == FDTHPoseMessage::ESeverity::Error ? TEXT("ERROR: ")
			: M.Severity == FDTHPoseMessage::ESeverity::Warning ? TEXT("WARNING: ") : TEXT("");
		return Prefix + M.Text;
	}
}

FString UDTHPoseScripting::OpenWizard(const FString& Page)
{
	FGlobalTabmanager::Get()->TryInvokeTab(FDTHPoseWizardModule::WizardTabName);
	const TSharedPtr<SDTHPoseWizard> Wizard = SDTHPoseWizard::Get();
	if (!Wizard.IsValid())
	{
		return TEXT("ERROR: The wizard tab did not open.");
	}
	Wizard->Reload();
	if (!Page.IsEmpty() && !Wizard->ShowPage(Page))
	{
		return FString::Printf(TEXT("ERROR: No page \"%s\" (Settings, Build, Poses)."), *Page);
	}
	return TEXT("The DTH Pose Wizard is open.");
}

TArray<FString> UDTHPoseScripting::BuildLibrary()
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	TArray<FDTHPoseMessage> Report;
	DTHPoseLibraryBuild::Build(Settings->SamplesFile.FilePath, Settings->LibraryAsset, Report);
	TArray<FString> Out;
	for (const FDTHPoseMessage& M : Report) Out.Add(Line(M));
	if (const TSharedPtr<SDTHPoseWizard> Wizard = SDTHPoseWizard::Get()) Wizard->Reload();
	return Out;
}

TArray<FString> UDTHPoseScripting::ListPoses(const FString& Filter)
{
	TArray<FString> Out;
	const UDTHPoseLibrary* Library = LoadLibrary();
	if (!Library)
	{
		Out.Add(TEXT("ERROR: No pose library (build it first)."));
		return Out;
	}
	for (const FDTHPose& P : Library->Poses)
	{
		if (!P.bLoaded || (!Filter.IsEmpty() && !P.Label.Contains(Filter) && !P.Pack.Contains(Filter))) continue;
		Out.Add(FString::Printf(TEXT("%s | %s | %s | %d missing"), *P.Label, *P.Pack, P.bPartial ? TEXT("partial") : TEXT("full"), P.Missing.Num()));
	}
	return Out;
}

FString UDTHPoseScripting::SelectCharacter(const FString& MeshPath)
{
	const TSharedPtr<SDTHPoseWizard> Wizard = SDTHPoseWizard::Get();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Wizard.IsValid() || !Mesh)
	{
		return Wizard.IsValid() ? FString::Printf(TEXT("ERROR: No skeletal mesh at %s."), *MeshPath) : TEXT("ERROR: Open the wizard first (open_wizard).");
	}
	Wizard->SelectCharacter(Mesh);
	return FString::Printf(TEXT("Previewing %s."), *Mesh->GetName());
}

FString UDTHPoseScripting::SelectPose(const FString& Label, const FString& BaseLabel)
{
	const TSharedPtr<SDTHPoseWizard> Wizard = SDTHPoseWizard::Get();
	return Wizard.IsValid() ? Wizard->SelectPose(Label, BaseLabel) : TEXT("ERROR: Open the wizard first (open_wizard).");
}

FString UDTHPoseScripting::CapturePreview(const FString& File, const FString& View)
{
	const TSharedPtr<SDTHPoseWizard> Wizard = SDTHPoseWizard::Get();
	return Wizard.IsValid() ? Wizard->CapturePreview(File, View) : TEXT("ERROR: Open the wizard first (open_wizard).");
}

TArray<FString> UDTHPoseScripting::WriteSequence(const FString& MeshPath, const TArray<FString>& Labels, const FString& BaseLabel, const FString& Name)
{
	TArray<FString> Out;
	const UDTHPoseLibrary* Library = LoadLibrary();
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	if (!Library || !Mesh || Labels.IsEmpty())
	{
		Out.Add(!Library ? TEXT("ERROR: No pose library (build it first).") : !Mesh ? FString::Printf(TEXT("ERROR: No skeletal mesh at %s."), *MeshPath)
			: TEXT("ERROR: No poses given."));
		return Out;
	}
	const int32 Base = BaseLabel.IsEmpty() ? INDEX_NONE : Library->FindPose(BaseLabel);
	if (!BaseLabel.IsEmpty() && Base == INDEX_NONE)
	{
		Out.Add(FString::Printf(TEXT("ERROR: No base pose \"%s\"."), *BaseLabel));
		return Out;
	}
	TArray<FDTHDazState> Frames;
	for (const FString& Label : Labels)
	{
		const int32 Pose = Library->FindPose(Label);
		if (Pose == INDEX_NONE || !Library->Poses[Pose].bLoaded)
		{
			Out.Add(FString::Printf(TEXT("ERROR: No pose \"%s\" in the library."), *Label));
			return Out;
		}
		Frames.Add(DTHPoseRig::Resolve(*Library, Pose, Base));
		for (const FString& M : Library->Poses[Pose].Missing) Out.Add(FString::Printf(TEXT("WARNING: %s: not reproduced: %s"), *Label, *M));
	}
	FString Error;
	const FString SequenceName = Name.IsEmpty() ? TEXT("A_Pose_") + DTHPoseRig::AssetName(Labels[0]) : Name;
	UAnimSequence* Sequence = DTHPoseRig::Write(*Mesh, *Library, Frames, GetDefault<UDTHPoseSettings>()->SequenceFolder.Path, SequenceName, Error);
	Out.Insert(Sequence ? FString::Printf(TEXT("Saved %s for %s: %d frames."), *Sequence->GetPackage()->GetName(), *Mesh->GetName(), Frames.Num())
		: TEXT("ERROR: ") + Error, 0);
	return Out;
}
