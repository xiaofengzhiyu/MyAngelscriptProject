#pragma once
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "AngelscriptEngine.h"
#include "AngelscriptHitResultLibrary.generated.h"

UCLASS(Meta = (ScriptMixin = "FHitResult"))
class UAngelscriptHitResultLibrary : public UObject
{
	GENERATED_BODY()

public:

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void SetComponent(FHitResult& HitResult, UPrimitiveComponent* Component)
	{
		HitResult.Component = Component;
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void SetActor(FHitResult& HitResult, AActor* Actor)
	{
		HitResult.HitObjectHandle = FActorInstanceHandle(Actor);
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void Reset(FHitResult& HitResult)
	{
		HitResult.Reset();
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static UPrimitiveComponent* GetComponent(const FHitResult& HitResult)
	{
		return HitResult.GetComponent();
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static AActor* GetActor(const FHitResult& HitResult)
	{
		return HitResult.GetActor();
	}

	/**
	 * Physical material that was hit.
	 * @note Must set bReturnPhysicalMaterial on the swept PrimitiveComponent or in the query params for this to be returned.
	 */
	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void SetPhysMaterial(FHitResult& HitResult, UPhysicalMaterial* PhysMaterial)
	{
		HitResult.PhysMaterial = PhysMaterial;
	}

	/**
	 * Physical material that was hit.
	 * @note Must set bReturnPhysicalMaterial on the swept PrimitiveComponent or in the query params for this to be returned.
	 */
	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static UPhysicalMaterial* GetPhysMaterial(const FHitResult& HitResult)
	{
		return HitResult.PhysMaterial.Get();
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static bool GetbBlockingHit(const FHitResult& HitResult)
	{
		return HitResult.bBlockingHit;
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial, DeprecatedFunction, DeprecationMessage = "Assign bBlockingHit instead"))
	static void SetBlockingHit(FHitResult& HitResult, bool bIsBlocking)
	{
		HitResult.bBlockingHit = bIsBlocking;
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void SetbBlockingHit(FHitResult& HitResult, bool bIsBlocking)
	{
		HitResult.bBlockingHit = bIsBlocking;
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static bool GetbStartPenetrating(const FHitResult& HitResult)
	{
		return HitResult.bStartPenetrating;
	}

	UFUNCTION(BlueprintCallable, Meta = (ScriptTrivial))
	static void SetbStartPenetrating(FHitResult& HitResult, bool bStartPenetrating)
	{
		HitResult.bStartPenetrating = bStartPenetrating;
	}
};
