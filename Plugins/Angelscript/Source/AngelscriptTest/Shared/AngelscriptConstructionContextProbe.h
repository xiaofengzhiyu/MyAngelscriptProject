#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "AngelscriptConstructionContextProbe.generated.h"

UCLASS()
class ANGELSCRIPTTEST_API UAngelscriptConstructionContextProbe : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	UObject* CaptureConstructingObject();

	UFUNCTION()
	UObject* GetCapturedObject() const;

	UFUNCTION()
	int32 GetCaptureCount() const;

	UFUNCTION()
	void ResetCapturedObject();

	static UObject* GetLastCapturedObject();
	static int32 GetLastCaptureCount();
	static void ResetCaptureState();
};
