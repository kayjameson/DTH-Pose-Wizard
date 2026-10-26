#include "UI/SDTHPoseWizard.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetData.h"
#include "Brushes/SlateDynamicImageBrush.h"
#include "Build/DTHPoseRig.h"
#include "DTHPoseWizardModule.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "Library/DTHPoseLibrary.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditorModule.h"
#include "Settings/DTHPoseSettings.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleColors.h"
#include "UI/SDTHBodyPreview.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SDTHPoseWizard"

TWeakPtr<SDTHPoseWizard> SDTHPoseWizard::Instance;

namespace
{
	FString SamplerScript()
	{
		return FDTHPoseWizardModule::ResourcesDir() / TEXT("Daz") / TEXT("DTH_SamplePoses.dsa");
	}

	/** "People/Genesis 9/Poses/<Vendor>/<Pack>" -> "<Vendor>/<Pack>". */
	FString ShortPack(const FString& Pack)
	{
		FString Out = Pack;
		Out.RemoveFromStart(TEXT("People/Genesis 9/Poses/"));
		Out.RemoveFromStart(TEXT("People/Genesis 9/"));
		return Out;
	}
}

bool SDTHPoseWizard::ShowPage(const FString& Name)
{
	static const TPair<const TCHAR*, EPage> PageNames[] = {
		{ TEXT("Settings"), EPage::Settings }, { TEXT("Build"), EPage::Build }, { TEXT("Poses"), EPage::Poses } };
	for (const auto& Page : PageNames)
	{
		if (Name.Equals(Page.Key, ESearchCase::IgnoreCase))
		{
			CurrentPage = Page.Value;
			return true;
		}
	}
	return false;
}

void SDTHPoseWizard::Construct(const FArguments& InArgs)
{
	Instance = SharedThis(this);
	LoadLibrary();
	if (!Library.IsValid())
	{
		CurrentPage = EPage::Settings;
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(8.f, 8.f, 8.f, 4.f)
			[
				MakeStepBar()
			]
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(8.f, 4.f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(CurrentPage); })
				+ SWidgetSwitcher::Slot()[MakeSettingsPage()]
				+ SWidgetSwitcher::Slot()[MakeBuildPage()]
				+ SWidgetSwitcher::Slot()[MakePosesPage()]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SSeparator).Thickness(1.f)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeActionBar()
			]
		]
	];

	Reload();
}

void SDTHPoseWizard::Reload()
{
	LoadLibrary();
	if (USkeletalMesh* Mesh = Cast<USkeletalMesh>(GetDefault<UDTHPoseSettings>()->Character.TryLoad()))
	{
		SetCharacter(Mesh);
	}
}

TSharedRef<SWidget> SDTHPoseWizard::MakeStepBar()
{
	TSharedRef<SSegmentedControl<int32>> Bar = SNew(SSegmentedControl<int32>)
		.Value_Lambda([this]() { return static_cast<int32>(CurrentPage); })
		.OnValueChanged_Lambda([this](int32 NewValue) { CurrentPage = static_cast<EPage>(NewValue); });
	const FText Labels[] = { LOCTEXT("StepSettings", "Settings"), LOCTEXT("StepBuild", "Build Library"), LOCTEXT("StepPoses", "Poses") };
	static_assert(UE_ARRAY_COUNT(Labels) == static_cast<int32>(EPage::Count), "One label per page");
	for (int32 Index = 0; Index < static_cast<int32>(EPage::Count); ++Index)
	{
		Bar->AddSlot(Index).Text(Labels[Index]);
	}
	return Bar;
}

TSharedRef<SWidget> SDTHPoseWizard::MakeSection(const FText& Title, const TSharedRef<SWidget>& Content)
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(8.f, 4.f))
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold")).Text(Title)
			]
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(FMargin(8.f, 6.f))
			[
				Content
			]
		];
}

// --- Settings ---------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHPoseWizard::MakeSettingsPage()
{
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs Args;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bAllowSearch = false;
	SettingsView = PropertyEditor.CreateDetailView(Args);
	SettingsView->SetObject(GetMutableDefault<UDTHPoseSettings>());
	SettingsView->OnFinishedChangingProperties().AddLambda([this](const FPropertyChangedEvent&)
	{
		GetMutableDefault<UDTHPoseSettings>()->SaveConfig();
		Reload();
	});

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MakeSection(LOCTEXT("DazSection", "Daz Studio Sampler"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("DazHelp",
						"The library is built from what Daz Studio does with your pose presets. In Daz Studio, open this script "
						"(File > Open, or double-click it). It loads a fresh Genesis 9, records DTH's ROM and every Genesis 9 pose "
						"preset in your content library (any Poses folder under People/), and writes DTH_PoseSamples.json to "
						"Documents/DAZ 3D/Studio4/DTH Pose Wizard. Nothing is saved in Daz. One library serves every DTH Genesis 9 "
						"character. Then set Daz Samples File below and build the library."))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(SamplerScript()))
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("CopyScript", "Copy Path"))
						.OnClicked_Lambda([]() { FPlatformApplicationMisc::ClipboardCopy(*SamplerScript()); return FReply::Handled(); })
					]
					+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ShowScript", "Show in Explorer"))
						.OnClicked_Lambda([]() { FPlatformProcess::ExploreFolder(*SamplerScript()); return FReply::Handled(); })
					]
				])
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
			.Padding(4.f)
			[
				SettingsView.ToSharedRef()
			]
		];
}

// --- Build --------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHPoseWizard::MakeBuildPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			MakeSection(LOCTEXT("LibrarySection", "Library"),
				SNew(STextBlock).AutoWrapText(true).Text(this, &SDTHPoseWizard::GetLibrarySummary))
		]
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			MakeSection(LOCTEXT("ReportSection", "Report"),
				SAssignNew(MessageList, SListView<TSharedPtr<FMessage>>)
				.ListItemsSource(&Messages)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SDTHPoseWizard::MakeMessageRow))
		];
}

TSharedRef<ITableRow> SDTHPoseWizard::MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner)
{
	const TCHAR* IconName = TEXT("Icons.Info");
	if (Message->Severity == FMessage::ESeverity::Warning) IconName = TEXT("Icons.WarningWithColor");
	else if (Message->Severity == FMessage::ESeverity::Error) IconName = TEXT("Icons.ErrorWithColor");
	return SNew(STableRow<TSharedPtr<FMessage>>, Owner)
		.Padding(FMargin(4.f, 2.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.f, 1.f, 6.f, 0.f)
			[
				SNew(SImage).Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Message->Text))
			]
		];
}

FText SDTHPoseWizard::GetLibrarySummary() const
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	const UDTHPoseLibrary* Lib = Library.Get();
	FString Text = FString::Printf(TEXT("Daz samples: %s\nLibrary asset: %s\n"),
		Settings->SamplesFile.FilePath.IsEmpty() ? TEXT("(not set: see Settings)") : *Settings->SamplesFile.FilePath, *Settings->LibraryAsset);
	if (Lib)
	{
		int32 Loaded = 0, Partial = 0, WithMissing = 0;
		for (const FDTHPose& P : Lib->Poses)
		{
			Loaded += P.bLoaded ? 1 : 0;
			Partial += P.bLoaded && P.bPartial ? 1 : 0;
			WithMissing += P.Missing.IsEmpty() ? 0 : 1;
		}
		Text += FString::Printf(TEXT("Built from samples taken %s (Daz %s): %d poses, %d of them partial (hands, feet), %d with shapes a DTH character does not take from a body pose; %d presets did not load, %d folders or files skipped."),
			*Lib->SampledAt, *Lib->DazVersion, Loaded, Partial, WithMissing, Lib->Poses.Num() - Loaded, Lib->Skipped.Num());
	}
	else
	{
		Text += TEXT("Not built yet.");
	}
	return FText::FromString(Text);
}

void SDTHPoseWizard::SetMessages(const TArray<FMessage>& Report)
{
	Messages.Reset();
	for (const FMessage& M : Report) Messages.Add(MakeShared<FMessage>(M));
	if (MessageList.IsValid()) MessageList->RequestListRefresh();
}

FReply SDTHPoseWizard::OnBuild()
{
	const UDTHPoseSettings* Settings = GetDefault<UDTHPoseSettings>();
	TArray<FMessage> Report;
	{
		FScopedSlowTask Task(1.f, LOCTEXT("Building", "Building the pose library..."));
		Task.MakeDialog();
		DTHPoseLibraryBuild::Build(Settings->SamplesFile.FilePath, Settings->LibraryAsset, Report);
	}
	SetMessages(Report);
	LoadLibrary();
	Status = Library.IsValid() ? TEXT("Library built.") : TEXT("The build failed: see the report.");
	return FReply::Handled();
}

void SDTHPoseWizard::LoadLibrary()
{
	const FString& Path = GetDefault<UDTHPoseSettings>()->LibraryAsset;
	const FString Object = Path.Contains(TEXT(".")) ? Path : Path + TEXT(".") + FPackageName::GetShortName(Path);
	Library = Path.IsEmpty() ? nullptr : LoadObject<UDTHPoseLibrary>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Icons.Reset();
	SheetRows.Reset();
	if (SheetList.IsValid()) SheetList->RequestListRefresh();
	Selected = Base = INDEX_NONE;
	RefreshPoseList();
	ShowPose(INDEX_NONE);
}

// --- Poses ---------------------------------------------------------------------------------------------------------

TSharedRef<SWidget> SDTHPoseWizard::MakePosesPage()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().FillHeight(1.f)
		[
			SNew(SSplitter)
			+ SSplitter::Slot().Value(0.30f)
			[
				MakeSection(LOCTEXT("PosesSection", "Poses (click to preview, double-click to add to the sheet)"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SSearchBox)
						.HintText(LOCTEXT("SearchHint", "Search poses or packs"))
						.OnTextChanged_Lambda([this](const FText& Text) { Search = Text.ToString(); RefreshPoseList(); })
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(PoseList, SListView<TSharedPtr<int32>>)
						.ListItemsSource(&PoseRows)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SDTHPoseWizard::MakePoseRow)
						.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Item, ESelectInfo::Type) { if (Item.IsValid()) ShowPose(*Item); })
						.OnMouseButtonDoubleClick_Lambda([this](TSharedPtr<int32> Item)
						{
							if (Item.IsValid()) { SheetRows.Add(MakeShared<int32>(*Item)); SheetList->RequestListRefresh(); }
						})
					])
			]
			+ SSplitter::Slot().Value(0.48f)
			[
				MakeSection(LOCTEXT("PreviewSection", "Preview"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
						[
							SNew(STextBlock).Text(LOCTEXT("CharacterLabel", "Character"))
						]
						+ SHorizontalBox::Slot().FillWidth(1.f)
						[
							SNew(SObjectPropertyEntryBox)
							.AllowedClass(USkeletalMesh::StaticClass())
							.DisplayThumbnail(false)
							.ObjectPath_Lambda([this]() { const USkeletalMesh* M = Preview.IsValid() ? Preview->GetCharacter() : nullptr; return M ? M->GetPathName() : FString(); })
							.OnObjectChanged_Lambda([this](const FAssetData& Asset) { SetCharacter(Cast<USkeletalMesh>(Asset.GetAsset())); })
						]
					]
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(Preview, SDTHBodyPreview)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
					[
						MakeViewButtons()
					]
					+ SVerticalBox::Slot().AutoHeight().MaxHeight(110.f).Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(STextBlock).AutoWrapText(true).Text(this, &SDTHPoseWizard::GetPoseDetails)
						]
					])
			]
			+ SSplitter::Slot().Value(0.22f)
			[
				MakeSection(LOCTEXT("SheetSection", "Pose Sheet (one frame each)"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().FillHeight(1.f)
					[
						SAssignNew(SheetList, SListView<TSharedPtr<int32>>)
						.ListItemsSource(&SheetRows)
						.SelectionMode(ESelectionMode::None)
						.OnGenerateRow(this, &SDTHPoseWizard::MakeSheetRow)
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("ClearSheet", "Clear Sheet"))
						.IsEnabled_Lambda([this]() { return !SheetRows.IsEmpty(); })
						.OnClicked_Lambda([this]() { SheetRows.Reset(); SheetList->RequestListRefresh(); return FReply::Handled(); })
					])
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
		[
			MakeWriteBar()
		];
}

TSharedRef<SWidget> SDTHPoseWizard::MakeViewButtons()
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const TPair<FText, const TCHAR*> Views[] = {
		{LOCTEXT("ViewBody", "Body"), TEXT("body")}, {LOCTEXT("ViewSide", "Side"), TEXT("side")},
		{LOCTEXT("ViewThreeQuarter", "3/4"), TEXT("threequarter")}, {LOCTEXT("ViewFace", "Face"), TEXT("face")},
		{LOCTEXT("ViewHands", "Hands"), TEXT("hands")} };
	for (const auto& V : Views)
	{
		const FString Name = V.Value;
		Row->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(SButton)
			.Text(V.Key)
			.OnClicked_Lambda([this, Name]() { if (Preview.IsValid()) Preview->SetView(Name); return FReply::Handled(); })
		];
	}
	Row->AddSlot().FillWidth(1.f);
	Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
	[
		SNew(STextBlock).Text_Lambda([this]()
		{
			return FText::FromString(Base == INDEX_NONE ? TEXT("Hand poses over: rest pose") : TEXT("Hand poses over: ") + PoseName(Base));
		})
	];
	Row->AddSlot().AutoWidth()
	[
		SNew(SButton)
		.Text(LOCTEXT("UseAsBase", "Use as Base"))
		.ToolTipText(LOCTEXT("UseAsBaseTip", "Lay partial (hand, foot) poses over the selected full-body pose instead of the rest pose"))
		.IsEnabled_Lambda([this]() { return Library.IsValid() && Library->Poses.IsValidIndex(Selected) && !Library->Poses[Selected].bPartial; })
		.OnClicked_Lambda([this]() { Base = Selected; return FReply::Handled(); })
	];
	Row->AddSlot().AutoWidth().Padding(4.f, 0.f, 0.f, 0.f)
	[
		SNew(SButton)
		.Text(LOCTEXT("ClearBase", "Rest Base"))
		.IsEnabled_Lambda([this]() { return Base != INDEX_NONE; })
		.OnClicked_Lambda([this]() { Base = INDEX_NONE; ShowPose(Selected); return FReply::Handled(); })
	];
	return Row;
}

TSharedRef<SWidget> SDTHPoseWizard::MakeWriteBar()
{
	return MakeSection(LOCTEXT("WriteSection", "Write a Sequence for the Character"),
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("NameLabel", "Name"))
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("NameHint", "Sequence name (empty: from the pose)"))
			.Text_Lambda([this]() { return FText::FromString(SequenceName); })
			.OnTextChanged_Lambda([this](const FText& Text) { SequenceName = Text.ToString(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text_Lambda([]() { return FText::FromString(TEXT("in ") + GetDefault<UDTHPoseSettings>()->SequenceFolder.Path); })
		]);
}

void SDTHPoseWizard::RefreshPoseList()
{
	PoseRows.Reset();
	if (const UDTHPoseLibrary* Lib = Library.Get())
	{
		for (int32 I = 0; I < Lib->Poses.Num(); ++I)
		{
			const FDTHPose& P = Lib->Poses[I];
			if (P.bLoaded && (Search.IsEmpty() || P.Label.Contains(Search) || P.Pack.Contains(Search)))
			{
				PoseRows.Add(MakeShared<int32>(I));
			}
		}
	}
	if (PoseList.IsValid()) PoseList->RequestListRefresh();
}

FString SDTHPoseWizard::PoseName(int32 Pose) const
{
	const UDTHPoseLibrary* Lib = Library.Get();
	return Lib && Lib->Poses.IsValidIndex(Pose) ? Lib->Poses[Pose].Label : FString(TEXT("rest pose"));
}

const FSlateBrush* SDTHPoseWizard::GetIcon(int32 Pose)
{
	if (const TSharedPtr<FSlateDynamicImageBrush>* Found = Icons.Find(Pose))
	{
		return Found->IsValid() ? Found->Get() : FAppStyle::GetBrush("ClassIcon.AnimSequence");
	}
	TSharedPtr<FSlateDynamicImageBrush> Brush;
	const UDTHPoseLibrary* Lib = Library.Get();
	if (Lib && Lib->Poses.IsValidIndex(Pose) && FPaths::FileExists(Lib->Poses[Pose].IconFile))
	{
		Brush = MakeShared<FSlateDynamicImageBrush>(FName(*Lib->Poses[Pose].IconFile), FVector2D(48.0, 48.0));
	}
	Icons.Add(Pose, Brush);
	return Brush.IsValid() ? Brush.Get() : FAppStyle::GetBrush("ClassIcon.AnimSequence");
}

TSharedRef<ITableRow> SDTHPoseWizard::MakePoseRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	const FDTHPose& P = Library->Poses[*Item];
	FString Tip = P.SourceFile;
	if (P.bPartial) Tip += TEXT("\nPartial pose: laid over the base pose (rest pose unless you pick one).");
	if (!P.Missing.IsEmpty()) Tip += TEXT("\nNot reproduced on a DTH character:\n") + FString::Join(P.Missing, TEXT("\n"));
	FString Line = ShortPack(P.Pack);
	if (P.bPartial) Line += TEXT("  |  partial");
	if (!P.Missing.IsEmpty()) Line += FString::Printf(TEXT("  |  %d shapes not reproduced"), P.Missing.Num());
	const int32 Index = *Item;
	return SNew(STableRow<TSharedPtr<int32>>, Owner)
		.Padding(FMargin(2.f))
		.ToolTipText(FText::FromString(Tip))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SBox).WidthOverride(48.f).HeightOverride(48.f)
				[
					SNew(SImage).Image(GetIcon(Index))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock).Text(FText::FromString(P.Label))
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle("SmallFont"))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(Line))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("AddTip", "Add to the pose sheet"))
				.OnClicked_Lambda([this, Index]() { SheetRows.Add(MakeShared<int32>(Index)); SheetList->RequestListRefresh(); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

TSharedRef<ITableRow> SDTHPoseWizard::MakeSheetRow(TSharedPtr<int32> Item, const TSharedRef<STableViewBase>& Owner)
{
	const int32 Frame = SheetRows.IndexOfByKey(Item);
	return SNew(STableRow<TSharedPtr<int32>>, Owner)
		.Padding(FMargin(2.f, 3.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d  %s"), Frame, *PoseName(*Item))))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ToolTipText(LOCTEXT("RemoveTip", "Remove from the sheet"))
				.OnClicked_Lambda([this, Item]() { SheetRows.Remove(Item); SheetList->RequestListRefresh(); return FReply::Handled(); })
				[
					SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")).ColorAndOpacity(FSlateColor::UseForeground())
				]
			]
		];
}

FText SDTHPoseWizard::GetPoseDetails() const
{
	const UDTHPoseLibrary* Lib = Library.Get();
	if (!Lib || !Lib->Poses.IsValidIndex(Selected))
	{
		return LOCTEXT("NoPose", "Pick a pose to preview it on the character.");
	}
	const FDTHPose& P = Lib->Poses[Selected];
	FString Text = P.Label + TEXT("  (") + ShortPack(P.Pack) + TEXT(")");
	if (P.bPartial) Text += TEXT("\nPartial pose, over the ") + (Base == INDEX_NONE ? FString(TEXT("rest pose")) : PoseName(Base)) + TEXT(".");
	if (P.Missing.IsEmpty())
	{
		Text += TEXT("\nEverything this preset does is reproduced (DTH drives the joint correctives from the bones).");
	}
	else
	{
		Text += TEXT("\nNot reproduced on a DTH character:\n") + FString::Join(P.Missing, TEXT("\n"));
	}
	return FText::FromString(Text);
}

void SDTHPoseWizard::ShowPose(int32 Pose)
{
	Selected = Pose;
	const UDTHPoseLibrary* Lib = Library.Get();
	USkeletalMesh* Mesh = Preview.IsValid() ? Preview->GetCharacter() : nullptr;
	if (!Lib || !Mesh)
	{
		return;
	}
	const DTHPoseSolve::FRig Rig = DTHPoseRig::Make(*Mesh, *Lib);
	const FDTHDazState State = Lib->Poses.IsValidIndex(Pose) ? DTHPoseRig::Resolve(*Lib, Pose, Base) : Lib->State(Lib->Rest);
	Preview->SetPose(DTHPoseRig::LocalPose(Rig, *Mesh, State));
}

void SDTHPoseWizard::SetCharacter(USkeletalMesh* Mesh)
{
	if (!Preview.IsValid())
	{
		return;
	}
	Preview->SetCharacter(Mesh);
	UDTHPoseSettings* Settings = GetMutableDefault<UDTHPoseSettings>();
	Settings->Character = Mesh ? FSoftObjectPath(Mesh) : FSoftObjectPath();
	Settings->SaveConfig();
	ShowPose(Selected);
}

FString SDTHPoseWizard::SelectPose(const FString& Label, const FString& BaseLabel)
{
	const UDTHPoseLibrary* Lib = Library.Get();
	if (!Lib)
	{
		return TEXT("ERROR: No pose library loaded.");
	}
	const int32 Pose = Lib->FindPose(Label);
	if (Pose == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: No pose \"%s\" in the library."), *Label);
	}
	Base = BaseLabel.IsEmpty() ? INDEX_NONE : Lib->FindPose(BaseLabel);
	if (!BaseLabel.IsEmpty() && Base == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: No base pose \"%s\" in the library."), *BaseLabel);
	}
	ShowPose(Pose);
	return FString::Printf(TEXT("Previewing %s%s."), *Label, Base == INDEX_NONE ? TEXT("") : *(TEXT(" over ") + BaseLabel));
}

FString SDTHPoseWizard::CapturePreview(const FString& File, const FString& View)
{
	FString Error;
	if (!Preview.IsValid() || !Preview->Capture(File, View, Error))
	{
		return TEXT("ERROR: ") + (Error.IsEmpty() ? FString(TEXT("No preview.")) : Error);
	}
	return FString::Printf(TEXT("Saved %s."), *File);
}

FReply SDTHPoseWizard::OnWritePose()
{
	const UDTHPoseLibrary* Lib = Library.Get();
	USkeletalMesh* Mesh = Preview.IsValid() ? Preview->GetCharacter() : nullptr;
	if (!Lib || !Mesh || !Lib->Poses.IsValidIndex(Selected))
	{
		return FReply::Handled();
	}
	const FString Name = SequenceName.IsEmpty() ? TEXT("A_Pose_") + DTHPoseRig::AssetName(PoseName(Selected)) : SequenceName;
	FString Error;
	const FDTHDazState State = DTHPoseRig::Resolve(*Lib, Selected, Base);
	UAnimSequence* Sequence = DTHPoseRig::Write(*Mesh, *Lib, {State}, GetDefault<UDTHPoseSettings>()->SequenceFolder.Path, Name, Error);
	Status = Sequence ? FString::Printf(TEXT("Saved %s for %s."), *Sequence->GetPackage()->GetName(), *Mesh->GetName()) : Error;
	return FReply::Handled();
}

FReply SDTHPoseWizard::OnWriteSheet()
{
	const UDTHPoseLibrary* Lib = Library.Get();
	USkeletalMesh* Mesh = Preview.IsValid() ? Preview->GetCharacter() : nullptr;
	if (!Lib || !Mesh || SheetRows.IsEmpty())
	{
		return FReply::Handled();
	}
	TArray<FDTHDazState> Frames;
	for (const TSharedPtr<int32>& Row : SheetRows) Frames.Add(DTHPoseRig::Resolve(*Lib, *Row, Base));
	const FString Name = SequenceName.IsEmpty() ? FString(TEXT("A_PoseSheet")) : SequenceName;
	FString Error;
	UAnimSequence* Sequence = DTHPoseRig::Write(*Mesh, *Lib, Frames, GetDefault<UDTHPoseSettings>()->SequenceFolder.Path, Name, Error);
	Status = Sequence ? FString::Printf(TEXT("Saved %s for %s: %d poses, one per frame."), *Sequence->GetPackage()->GetName(), *Mesh->GetName(), Frames.Num()) : Error;
	return FReply::Handled();
}

// --- Action bar ---------------------------------------------------------------------------------------------------

FText SDTHPoseWizard::GetStatusText() const
{
	if (!Status.IsEmpty())
	{
		return FText::FromString(Status);
	}
	if (!Library.IsValid())
	{
		return LOCTEXT("NoLibrary", "No pose library yet: run the Daz sampler (Settings), then Build Library.");
	}
	return FText::Format(LOCTEXT("Ready", "{0} poses; {1} on the sheet."), FText::AsNumber(PoseRows.Num()), FText::AsNumber(SheetRows.Num()));
}

TSharedRef<SWidget> SDTHPoseWizard::MakeActionBar()
{
	auto Primary = [this](const FText& Label, const FText& Tip, EPage Page, TFunction<bool()> Enabled, FReply (SDTHPoseWizard::*Handler)())
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.Visibility_Lambda([this, Page]() { return CurrentPage == Page ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsEnabled_Lambda([Enabled]() { return Enabled(); })
			.OnClicked(this, Handler)
			.ToolTipText(Tip)
			[
				SNew(STextBlock).TextStyle(FAppStyle::Get(), "SmallButtonText").Text(Label)
			];
	};
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
		.Padding(FMargin(8.f, 6.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock).ColorAndOpacity(FStyleColors::Foreground).Text(this, &SDTHPoseWizard::GetStatusText)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				Primary(LOCTEXT("BuildButton", "Build Library"), LOCTEXT("BuildTip", "Write the pose library from the Daz samples"),
					EPage::Build, []() { return !GetDefault<UDTHPoseSettings>()->SamplesFile.FilePath.IsEmpty(); }, &SDTHPoseWizard::OnBuild)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				Primary(LOCTEXT("WritePose", "Create Sequence"), LOCTEXT("WritePoseTip", "Write the previewed pose as a one-frame sequence for the character"),
					EPage::Poses, [this]() { return Library.IsValid() && Library->Poses.IsValidIndex(Selected) && Preview.IsValid() && Preview->GetCharacter(); },
					&SDTHPoseWizard::OnWritePose)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.f, 0.f, 0.f, 0.f)
			[
				Primary(LOCTEXT("WriteSheet", "Create Pose Sheet"), LOCTEXT("WriteSheetTip", "Write the pose sheet as one sequence for the character, one pose per frame"),
					EPage::Poses, [this]() { return Library.IsValid() && !SheetRows.IsEmpty() && Preview.IsValid() && Preview->GetCharacter(); },
					&SDTHPoseWizard::OnWriteSheet)
			]
		];
}

#undef LOCTEXT_NAMESPACE
