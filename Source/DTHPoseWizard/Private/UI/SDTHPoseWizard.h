#pragma once

#include "Library/DTHPoseLibraryBuild.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class IDetailsView;
class ITableRow;
class SDTHBodyPreview;
class STableViewBase;
class UAnimSequence;
class UDTHPoseLibrary;
class USkeletalMesh;
struct FSlateDynamicImageBrush;

/** The Pose Wizard's tab: Settings (and the Daz script), Build Library, and Poses (browse, preview, write sequences). */
class SDTHPoseWizard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHPoseWizard) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The open wizard, if its tab exists (for scripting). */
	static TSharedPtr<SDTHPoseWizard> Get() { return Instance.Pin(); }
	/** Shows a page by name: Settings, Build or Poses. False for an unknown name. */
	bool ShowPage(const FString& Name);
	/** Reads the settings again: the library asset and the character. */
	void Reload();
	/** Previews this character. */
	void SelectCharacter(USkeletalMesh* Mesh) { SetCharacter(Mesh); }
	/** Previews a library pose by label (a partial pose over BaseLabel, "" for the rest pose). A status line, "ERROR: ..." on failure. */
	FString SelectPose(const FString& Label, const FString& BaseLabel);
	/** Saves the preview, framed as View (body, side, threequarter, face, hands), to File. A status line. */
	FString CapturePreview(const FString& File, const FString& View);

private:
	enum class EPage : int32 { Settings, Build, Poses, Count };
	using FMessage = FDTHPoseMessage;

	TSharedRef<SWidget> MakeStepBar();
	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> MakeSettingsPage();
	TSharedRef<SWidget> MakeBuildPage();
	TSharedRef<SWidget> MakePosesPage();
	TSharedRef<SWidget> MakeViewButtons();
	TSharedRef<SWidget> MakeWriteBar();
	TSharedRef<SWidget> MakeActionBar();
	TSharedRef<ITableRow> MakeMessageRow(TSharedPtr<FMessage> Message, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakePoseRow(TSharedPtr<int32> Pose, const TSharedRef<STableViewBase>& Owner);
	TSharedRef<ITableRow> MakeSheetRow(TSharedPtr<int32> Pose, const TSharedRef<STableViewBase>& Owner);

	FReply OnBuild();
	FReply OnWritePose();
	FReply OnWriteSheet();
	void LoadLibrary();
	void RefreshPoseList();
	void ShowPose(int32 Pose);
	void SetCharacter(USkeletalMesh* Mesh);
	void SetMessages(const TArray<FMessage>& Report);

	FText GetStatusText() const;
	FText GetLibrarySummary() const;
	FText GetPoseDetails() const;
	FString PoseName(int32 Pose) const;
	const FSlateBrush* GetIcon(int32 Pose);

	EPage CurrentPage = EPage::Poses;
	TSharedPtr<IDetailsView> SettingsView;
	TWeakObjectPtr<UDTHPoseLibrary> Library;
	FString Status;

	TArray<TSharedPtr<FMessage>> Messages;
	TSharedPtr<SListView<TSharedPtr<FMessage>>> MessageList;

	FString Search;
	TArray<TSharedPtr<int32>> PoseRows;
	TSharedPtr<SListView<TSharedPtr<int32>>> PoseList;
	TMap<int32, TSharedPtr<FSlateDynamicImageBrush>> Icons;
	int32 Selected = INDEX_NONE;
	/** The pose a partial (hand) pose is laid over: INDEX_NONE for the rest pose (UnrealPose). */
	int32 Base = INDEX_NONE;

	TArray<TSharedPtr<int32>> SheetRows;
	TSharedPtr<SListView<TSharedPtr<int32>>> SheetList;

	TSharedPtr<SDTHBodyPreview> Preview;
	FString SequenceName;

	static TWeakPtr<SDTHPoseWizard> Instance;
};
