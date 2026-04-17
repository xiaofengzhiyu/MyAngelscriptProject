#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"

#include "AngelscriptDataTableBindingTestTypes.generated.h"

USTRUCT(BlueprintType)
struct FAngelscriptBindingDataTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName Category = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Label;
};
