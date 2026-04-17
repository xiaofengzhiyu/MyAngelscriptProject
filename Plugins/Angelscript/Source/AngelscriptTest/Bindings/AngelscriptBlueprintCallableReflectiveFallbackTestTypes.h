#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "AngelscriptBlueprintCallableReflectiveFallbackTestTypes.generated.h"

UCLASS()
class ANGELSCRIPTTEST_API UAngelscriptBlueprintCallableReflectiveFallbackTestLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	static int32 EligibleCallable(int32 Value);

	UFUNCTION(BlueprintCallable)
	static int32 TooManyArgumentsCallable(
		int32 Arg00,
		int32 Arg01,
		int32 Arg02,
		int32 Arg03,
		int32 Arg04,
		int32 Arg05,
		int32 Arg06,
		int32 Arg07,
		int32 Arg08,
		int32 Arg09,
		int32 Arg10,
		int32 Arg11,
		int32 Arg12,
		int32 Arg13,
		int32 Arg14,
		int32 Arg15,
		int32 Arg16);
};
