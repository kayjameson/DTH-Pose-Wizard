#pragma once

#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "DTHRigKeyingSettings.generated.h"

/** Where the Pose and Expression Wizards key onto a control rig. Shared, so both wizards key the same track. */
UCLASS(config = EditorPerProjectUserSettings)
class DTHRIGKEYING_API UDTHRigKeyingSettings : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(config)
	FSoftObjectPath Sequence;

	/** The binding's name in the sequence (the character). */
	UPROPERTY(config)
	FString Binding;

	UPROPERTY(config)
	int32 Frame = 0;

	/** Hold each key until the next (one pose or expression per frame) instead of blending between keys. */
	UPROPERTY(config)
	bool bStepped = true;
};
