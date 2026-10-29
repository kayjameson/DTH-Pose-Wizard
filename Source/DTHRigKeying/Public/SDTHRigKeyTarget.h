#pragma once

#include "Widgets/SCompoundWidget.h"

class ULevelSequence;

/** The level sequence, binding and frame both wizards key onto (shared settings: pick once, key from either wizard). */
class DTHRIGKEYING_API SDTHRigKeyTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDTHRigKeyTarget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	ULevelSequence* GetSequence() const;
	FString GetBinding() const;
	int32 GetFrame() const;
	bool IsStepped() const;
	/** Moves the frame on, e.g. after keying, so the next key lands on the next frame. */
	void SetFrame(int32 Frame);

private:
	void RefreshBindings();

	TArray<TSharedPtr<FString>> BindingNames;
	TSharedPtr<class SComboBox<TSharedPtr<FString>>> BindingCombo;
};
