#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"

#include "AngelscriptNativeInterfaceTestTypes.generated.h"

UINTERFACE(BlueprintType)
class ANGELSCRIPTTEST_API UAngelscriptNativeParentInterface : public UInterface
{
	GENERATED_BODY()
};

class ANGELSCRIPTTEST_API IAngelscriptNativeParentInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	int32 GetNativeValue() const;

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void SetNativeMarker(FName Marker);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void AdjustNativeValue(int32 Delta, UPARAM(ref) int32& Value);
};

UINTERFACE(BlueprintType)
class ANGELSCRIPTTEST_API UAngelscriptNativeChildInterface : public UAngelscriptNativeParentInterface
{
	GENERATED_BODY()
};

class ANGELSCRIPTTEST_API IAngelscriptNativeChildInterface : public IAngelscriptNativeParentInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	int32 GetChildValue() const;
};

UCLASS()
class ANGELSCRIPTTEST_API ATestNativeParentInterfaceActor : public AActor, public IAngelscriptNativeParentInterface
{
	GENERATED_BODY()

public:
	ATestNativeParentInterfaceActor();

	UPROPERTY()
	int32 NativeValue = 123;

	UPROPERTY()
	FName NativeMarker = NAME_None;

	UPROPERTY()
	int32 LastAdjustedValue = 0;

	UPROPERTY()
	int32 LastAdjustmentDelta = 0;

	virtual int32 GetNativeValue_Implementation() const override;
	virtual void SetNativeMarker_Implementation(FName Marker) override;
	virtual void AdjustNativeValue_Implementation(int32 Delta, int32& Value) override;
};
