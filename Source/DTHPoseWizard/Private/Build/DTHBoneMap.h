#pragma once

#include "CoreMinimal.h"

/** Which Daz Genesis 9 bone each DTH (UE5 Mannequin) bone stands for. The same for every DTH Genesis 9 character. */
namespace DTHBoneMap
{
	struct FEntry
	{
		const TCHAR* UE;
		const TCHAR* Daz;
		/** The UE joint sits on the Daz joint (false: DTH places it, and it is mapped by hierarchy). */
		bool bJoint;
	};

	const TArray<FEntry>& Table();
}
