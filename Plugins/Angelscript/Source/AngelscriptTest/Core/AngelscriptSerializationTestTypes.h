#pragma once

#include "CoreMinimal.h"

#include "AngelscriptSerializationTestTypes.generated.h"

USTRUCT()
struct ANGELSCRIPTTEST_API FAngelscriptSerializationInnerValue
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	float Ratio = 0.0f;

	UPROPERTY()
	FString Label;

	UPROPERTY()
	FName Token = NAME_None;
};

USTRUCT()
struct ANGELSCRIPTTEST_API FAngelscriptSerializationNestedFixture
{
	GENERATED_BODY()

	UPROPERTY()
	FAngelscriptSerializationInnerValue Primary;

	UPROPERTY()
	FAngelscriptSerializationInnerValue Secondary;

	UPROPERTY()
	TArray<FAngelscriptSerializationInnerValue> Children;
};

USTRUCT()
struct ANGELSCRIPTTEST_API FAngelscriptSerializationVersionToleranceV1
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	FString Title;

	UPROPERTY()
	FAngelscriptSerializationInnerValue Payload;

	UPROPERTY()
	FName Status = NAME_None;
};

USTRUCT()
struct ANGELSCRIPTTEST_API FAngelscriptSerializationVersionToleranceV2
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	int32 SchemaVersion = 11;

	UPROPERTY()
	FString Title;

	UPROPERTY()
	FAngelscriptSerializationInnerValue Payload;

	UPROPERTY()
	FName Status = NAME_None;

	UPROPERTY()
	float Score = 1.5f;
};
