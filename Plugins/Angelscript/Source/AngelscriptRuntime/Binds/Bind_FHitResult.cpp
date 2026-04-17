#include "AngelscriptBinds.h"
#include "AngelscriptEngine.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FHitResult(FAngelscriptBinds::EOrder::Late, []
{
	auto FHitResult_ = FAngelscriptBinds::ExistingClass("FHitResult");

	FHitResult_.Constructor("void f(AActor InActor, UPrimitiveComponent InComponent, const FVector& HitLoc, const FVector& HitNorm)", [](FHitResult* Address, class AActor* InActor, class UPrimitiveComponent* InComponent, FVector const& HitLoc, FVector const& HitNorm)
	{
		new(Address) FHitResult(InActor, InComponent, HitLoc, HitNorm);
	});
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FHitResult_, "FHitResult");
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);

	FHitResult_.Constructor("void f(const FVector& TraceStart, const FVector& TraceEnd)", [](FHitResult* Address, FVector const& TraceStart, FVector const& TraceEnd)
	{
		new(Address) FHitResult(TraceStart, TraceEnd);
	});
	SCRIPT_TRIVIAL_NATIVE_CONSTRUCTOR(FHitResult_, "FHitResult");
	FAngelscriptBinds::SetPreviousBindNoDiscard(true);

	FHitResult_.Property("int FaceIndex", &FHitResult::FaceIndex);
	FHitResult_.Property("uint8 ElementIndex", &FHitResult::ElementIndex);
	FHitResult_.Property("int Item", &FHitResult::Item);
	FHitResult_.Property("int MyItem", &FHitResult::MyItem);

	FHitResult_.Property("float32 PenetrationDepth", &FHitResult::PenetrationDepth);
	FHitResult_.Property("float32 Distance", &FHitResult::Distance);
	FHitResult_.Property("float32 Time", &FHitResult::Time);

	FHitResult_.Property("FVector TraceStart", &FHitResult::TraceStart);
	FHitResult_.Property("FVector TraceEnd", &FHitResult::TraceEnd);
	FHitResult_.Property("FVector ImpactNormal", &FHitResult::ImpactNormal);
	FHitResult_.Property("FVector ImpactPoint", &FHitResult::ImpactPoint);
	FHitResult_.Property("FVector Location", &FHitResult::Location);
	FHitResult_.Property("FVector Normal", &FHitResult::Normal);
	FHitResult_.Property("FName BoneName", &FHitResult::BoneName);
	FHitResult_.Property("FName MyBoneName", &FHitResult::MyBoneName);

	FHitResult_.Method("void SetComponent(UPrimitiveComponent InComponent)", [](FHitResult* HitResult, UPrimitiveComponent* Component)
	{
		HitResult->Component = Component;
	});

	FHitResult_.Method("UPrimitiveComponent GetComponent() const", [](FHitResult* HitResult) -> UPrimitiveComponent*
	{
		return HitResult->GetComponent();
	});

	FHitResult_.Method("void SetActor(AActor InActor)", [](FHitResult* HitResult, AActor* Actor)
	{
		HitResult->HitObjectHandle = FActorInstanceHandle(Actor);
	});

	FHitResult_.Method("AActor GetActor() const", [](FHitResult* HitResult) -> AActor*
	{
		return HitResult->GetActor();
	});

	FHitResult_.Method("void Reset()", [](FHitResult* HitResult)
	{
		HitResult->Reset();
	});

	FHitResult_.Method("bool GetbBlockingHit() const", [](FHitResult* HitResult) -> bool
	{
		return HitResult->bBlockingHit;
	});

	FHitResult_.Method("void SetBlockingHit(bool bIsBlocking)", [](FHitResult* HitResult, bool bIsBlocking)
	{
		HitResult->bBlockingHit = bIsBlocking;
	});

	FHitResult_.Method("void SetbBlockingHit(bool bIsBlocking)", [](FHitResult* HitResult, bool bIsBlocking)
	{
		HitResult->bBlockingHit = bIsBlocking;
	});

	FHitResult_.Method("bool GetbStartPenetrating() const", [](FHitResult* HitResult) -> bool
	{
		return HitResult->bStartPenetrating;
	});

	FHitResult_.Method("void SetbStartPenetrating(bool bStartPenetrating)", [](FHitResult* HitResult, bool bStartPenetrating)
	{
		HitResult->bStartPenetrating = bStartPenetrating;
	});
});
