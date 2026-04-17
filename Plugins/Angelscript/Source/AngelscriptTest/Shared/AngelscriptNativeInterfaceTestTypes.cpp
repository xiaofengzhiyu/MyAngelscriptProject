#include "Shared/AngelscriptNativeInterfaceTestTypes.h"

ATestNativeParentInterfaceActor::ATestNativeParentInterfaceActor()
{
	PrimaryActorTick.bCanEverTick = false;
}

int32 ATestNativeParentInterfaceActor::GetNativeValue_Implementation() const
{
	return NativeValue;
}

void ATestNativeParentInterfaceActor::SetNativeMarker_Implementation(FName Marker)
{
	NativeMarker = Marker;
}

void ATestNativeParentInterfaceActor::AdjustNativeValue_Implementation(int32 Delta, int32& Value)
{
	Value += Delta;
	LastAdjustmentDelta = Delta;
	LastAdjustedValue = Value;
}
