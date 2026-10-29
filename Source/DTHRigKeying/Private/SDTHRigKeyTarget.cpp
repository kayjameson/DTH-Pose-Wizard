#include "SDTHRigKeyTarget.h"

#include "DTHRigKeying.h"
#include "DTHRigKeyingSettings.h"
#include "LevelSequence.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SDTHRigKeyTarget"

namespace
{
	UDTHRigKeyingSettings* Settings() { return GetMutableDefault<UDTHRigKeyingSettings>(); }
}

void SDTHRigKeyTarget::Construct(const FArguments& InArgs)
{
	RefreshBindings();
	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(ULevelSequence::StaticClass())
			.ObjectPath_Lambda([]() { return Settings()->Sequence.ToString(); })
			.OnObjectChanged_Lambda([this](const FAssetData& Asset)
			{
				Settings()->Sequence = Asset.ToSoftObjectPath();
				Settings()->SaveConfig();
				RefreshBindings();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SAssignNew(BindingCombo, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&BindingNames)
			.ToolTipText(LOCTEXT("BindingTip", "The character in the sequence. Its control rig track is keyed (added if it has none)."))
			.OnComboBoxOpening_Lambda([this]() { RefreshBindings(); })
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) { return SNew(STextBlock).Text(FText::FromString(*Item)); })
			.OnSelectionChanged_Lambda([](TSharedPtr<FString> Item, ESelectInfo::Type)
			{
				if (Item.IsValid()) { Settings()->Binding = *Item; Settings()->SaveConfig(); }
			})
			[
				SNew(STextBlock).Text_Lambda([]()
				{
					return Settings()->Binding.IsEmpty() ? LOCTEXT("PickBinding", "Character...") : FText::FromString(Settings()->Binding);
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 4.f, 0.f)
		[
			SNew(STextBlock).Text(LOCTEXT("FrameLabel", "Frame"))
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
		[
			SNew(SBox).WidthOverride(64.f)
			[
				SNew(SSpinBox<int32>)
				.MinValue(0)
				.Value_Lambda([]() { return Settings()->Frame; })
				.OnValueChanged_Lambda([](int32 V) { Settings()->Frame = V; })
				.OnValueCommitted_Lambda([](int32 V, ETextCommit::Type) { Settings()->Frame = V; Settings()->SaveConfig(); })
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("SteppedTip", "Hold each key until the next one (one pose or expression per frame). Off: blend between keys."))
			.IsChecked_Lambda([]() { return Settings()->bStepped ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([](ECheckBoxState S) { Settings()->bStepped = S == ECheckBoxState::Checked; Settings()->SaveConfig(); })
			[
				SNew(STextBlock).Text(LOCTEXT("Stepped", "Stepped"))
			]
		]
	];
}

void SDTHRigKeyTarget::RefreshBindings()
{
	BindingNames.Reset();
	for (const DTHRigKeying::FBinding& B : DTHRigKeying::Bindings(GetSequence()))
	{
		BindingNames.Add(MakeShared<FString>(B.Name));
	}
	if (BindingCombo.IsValid()) BindingCombo->RefreshOptions();
}

ULevelSequence* SDTHRigKeyTarget::GetSequence() const
{
	return Cast<ULevelSequence>(Settings()->Sequence.TryLoad());
}

FString SDTHRigKeyTarget::GetBinding() const
{
	return Settings()->Binding;
}

int32 SDTHRigKeyTarget::GetFrame() const
{
	return Settings()->Frame;
}

bool SDTHRigKeyTarget::IsStepped() const
{
	return Settings()->bStepped;
}

void SDTHRigKeyTarget::SetFrame(int32 Frame)
{
	Settings()->Frame = FMath::Max(0, Frame);
	Settings()->SaveConfig();
}

#undef LOCTEXT_NAMESPACE
