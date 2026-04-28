#pragma once

#include "CoreMinimal.h"

#include "AngelscriptASFunctionArgumentLifetimeTestTypes.generated.h"

USTRUCT(BlueprintType)
struct FAngelscriptASFunctionArgumentLifetimeFixture
{
	GENERATED_BODY()

	static int32 DestructorCount;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Value = 0;

	~FAngelscriptASFunctionArgumentLifetimeFixture();

	static void ResetCounters();
};
