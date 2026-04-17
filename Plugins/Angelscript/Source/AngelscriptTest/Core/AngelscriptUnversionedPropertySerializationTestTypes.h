#pragma once

#include "CoreMinimal.h"

#include "AngelscriptUnversionedPropertySerializationTestTypes.generated.h"

USTRUCT()
struct ANGELSCRIPTTEST_API FAngelscriptUnversionedPropertySerializationFixture
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	bool bEnabled = false;

	UPROPERTY()
	FName Label = NAME_None;

	UPROPERTY()
	TArray<int32> Values;
};
