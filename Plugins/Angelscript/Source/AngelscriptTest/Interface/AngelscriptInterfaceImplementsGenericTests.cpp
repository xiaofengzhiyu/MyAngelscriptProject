#include "Core/AngelscriptEngine.h"
#include "Shared/AngelscriptNativeInterfaceTestTypes.h"
#include "Shared/AngelscriptNativeInterfaceTestHelpers.h"
#include "Shared/AngelscriptFunctionalTestUtils.h"
#include "Shared/AngelscriptTestMacros.h"

#include "Components/ActorTestSpawner.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

// Phase 5 tests — preprocessor sugar rewrites `Obj.Implements<T>()` into
// `Obj.ImplementsInterface(T::StaticClass())`, so AS-side code reads more
// like C++ `Obj->Implements<UFoo>()`. These tests validate:
//   1. The rewrite works for script-defined UINTERFACE (pure AS interface).
//   2. The rewrite works for a C++ UInterface already auto-bound via Phase 5
//      of Bind_BlueprintType.cpp.
//   3. The rewrite correctly returns `false` when the object does NOT
//      implement the interface (verifies we didn't accidentally hard-code
//      the result).
//   4. Pre-existing `ImplementsInterface(UClass)` calls continue to work
//      identically — Phase 5 is strictly additive.
#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptFunctionalTestUtils;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceImplementsGenericScriptInterfaceTrueTest,
	"Angelscript.TestModule.Interface.ImplementsGeneric.ScriptInterfaceTrue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceImplementsGenericCppInterfaceTrueTest,
	"Angelscript.TestModule.Interface.ImplementsGeneric.CppInterfaceTrue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceImplementsGenericNotImplementedFalseTest,
	"Angelscript.TestModule.Interface.ImplementsGeneric.NotImplementedFalse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestInterfaceImplementsGenericLegacyApiCompatTest,
	"Angelscript.TestModule.Interface.ImplementsGeneric.LegacyApiCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Case 1: Script-defined interface
bool FAngelscriptTestInterfaceImplementsGenericScriptInterfaceTrueTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	do
	{

	static const FName ModuleName(TEXT("TestInterfaceImplementsGenericScript"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ModuleName,
		TEXT("TestInterfaceImplementsGenericScript.as"),
		TEXT(R"AS(
UINTERFACE()
interface UIImplementsGenericScriptMarker
{
	void Marker();
}

UCLASS()
class ATestImplementsGenericScript : AActor, UIImplementsGenericScriptMarker
{
	UPROPERTY()
	int bTemplateFormResult = 0;

	UPROPERTY()
	int bLegacyFormResult = 0;

	UFUNCTION()
	void Marker() {}

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		// Phase 5 sugar — reads like C++.
		bTemplateFormResult = this.Implements<UIImplementsGenericScriptMarker>() ? 1 : 0;
		// Legacy form must still work unchanged.
		bLegacyFormResult = this.ImplementsInterface(UIImplementsGenericScriptMarker::StaticClass()) ? 1 : 0;
	}
}
)AS"),
		TEXT("ATestImplementsGenericScript"));
	if (!TestNotNull(TEXT("ScriptClass should compile"), ScriptClass))
	{
		break;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();
	AActor* ScriptActor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (!TestNotNull(TEXT("ScriptActor should spawn"), ScriptActor))
	{
		break;
	}

	BeginPlayActor(Engine, *ScriptActor);

	int32 bTemplateFormResult = 0;
	int32 bLegacyFormResult = 0;
	if (!ReadPropertyValue<FIntProperty>(*this, ScriptActor, TEXT("bTemplateFormResult"), bTemplateFormResult)
		|| !ReadPropertyValue<FIntProperty>(*this, ScriptActor, TEXT("bLegacyFormResult"), bLegacyFormResult))
	{
		break;
	}

	TestEqual(TEXT("this.Implements<UIFoo>() should return true for script-defined interface implementor"),
		bTemplateFormResult, 1);
	TestEqual(TEXT("this.ImplementsInterface(UIFoo::StaticClass()) legacy form should still return true"),
		bLegacyFormResult, 1);

	}
	while (false);
	ASTEST_END_SHARE_FRESH
	return !HasAnyErrors();
}

// Case 2: C++ UInterface
bool FAngelscriptTestInterfaceImplementsGenericCppInterfaceTrueTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	do
	{

	AngelscriptNativeInterfaceTestHelpers::EnsureNativeInterfaceBound(UAngelscriptNativeParentInterface::StaticClass());

	static const FName ModuleName(TEXT("TestInterfaceImplementsGenericCpp"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ModuleName,
		TEXT("TestInterfaceImplementsGenericCpp.as"),
		TEXT(R"AS(
UCLASS()
class ATestImplementsGenericCpp : AActor
{
	UPROPERTY()
	UObject Target;

	UPROPERTY()
	int bTemplateFormResult = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		if (Target != nullptr)
		{
			bTemplateFormResult = Target.Implements<UAngelscriptNativeParentInterface>() ? 1 : 0;
		}
	}
}
)AS"),
		TEXT("ATestImplementsGenericCpp"));
	if (!TestNotNull(TEXT("ScriptClass should compile"), ScriptClass))
	{
		break;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();
	ATestNativeParentInterfaceActor* NativeActor = Spawner.GetWorld().SpawnActor<ATestNativeParentInterfaceActor>();
	AActor* ScriptActor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (!TestNotNull(TEXT("NativeActor should spawn"), NativeActor)
		|| !TestNotNull(TEXT("ScriptActor should spawn"), ScriptActor))
	{
		break;
	}

	FObjectPropertyBase* TargetProperty = FindFProperty<FObjectPropertyBase>(ScriptActor->GetClass(), TEXT("Target"));
	if (!TestNotNull(TEXT("Script actor should expose 'Target'"), TargetProperty))
	{
		break;
	}
	TargetProperty->SetObjectPropertyValue_InContainer(ScriptActor, NativeActor);

	BeginPlayActor(Engine, *ScriptActor);

	int32 bTemplateFormResult = 0;
	if (!ReadPropertyValue<FIntProperty>(*this, ScriptActor, TEXT("bTemplateFormResult"), bTemplateFormResult))
	{
		break;
	}

	TestEqual(TEXT("Target.Implements<UAngelscriptNativeParentInterface>() should return true for native implementor"),
		bTemplateFormResult, 1);

	}
	while (false);
	ASTEST_END_SHARE_FRESH
	return !HasAnyErrors();
}

// Case 3: object does not implement the interface
bool FAngelscriptTestInterfaceImplementsGenericNotImplementedFalseTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	do
	{

	AngelscriptNativeInterfaceTestHelpers::EnsureNativeInterfaceBound(UAngelscriptNativeParentInterface::StaticClass());

	static const FName ModuleName(TEXT("TestInterfaceImplementsGenericFalse"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	// Script actor declared WITHOUT the interface — the query must return false.
	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ModuleName,
		TEXT("TestInterfaceImplementsGenericFalse.as"),
		TEXT(R"AS(
UCLASS()
class ATestImplementsGenericFalse : AActor
{
	UPROPERTY()
	int bTemplateFormResult = 1;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		// Self is AActor only — should NOT implement the native interface.
		bTemplateFormResult = this.Implements<UAngelscriptNativeParentInterface>() ? 1 : 0;
	}
}
)AS"),
		TEXT("ATestImplementsGenericFalse"));
	if (!TestNotNull(TEXT("ScriptClass should compile"), ScriptClass))
	{
		break;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();
	AActor* ScriptActor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (!TestNotNull(TEXT("ScriptActor should spawn"), ScriptActor))
	{
		break;
	}

	BeginPlayActor(Engine, *ScriptActor);

	int32 bTemplateFormResult = 1;
	if (!ReadPropertyValue<FIntProperty>(*this, ScriptActor, TEXT("bTemplateFormResult"), bTemplateFormResult))
	{
		break;
	}

	TestEqual(TEXT("this.Implements<UIFoo>() should return false when the class does NOT implement the interface"),
		bTemplateFormResult, 0);

	}
	while (false);
	ASTEST_END_SHARE_FRESH
	return !HasAnyErrors();
}

// Case 4: legacy ImplementsInterface(UClass) still works alongside the new sugar
bool FAngelscriptTestInterfaceImplementsGenericLegacyApiCompatTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH
	do
	{

	AngelscriptNativeInterfaceTestHelpers::EnsureNativeInterfaceBound(UAngelscriptNativeParentInterface::StaticClass());

	static const FName ModuleName(TEXT("TestInterfaceImplementsGenericLegacy"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
	};

	UClass* ScriptClass = CompileScriptModule(
		*this,
		Engine,
		ModuleName,
		TEXT("TestInterfaceImplementsGenericLegacy.as"),
		TEXT(R"AS(
UCLASS()
class ATestImplementsGenericLegacy : AActor
{
	UPROPERTY()
	UObject Target;

	UPROPERTY()
	int bLegacyResult = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		if (Target != nullptr)
		{
			// Pure legacy form — confirm the rewrite pass didn't break it.
			UClass InterfaceClass = UAngelscriptNativeParentInterface::StaticClass();
			bLegacyResult = Target.ImplementsInterface(InterfaceClass) ? 1 : 0;
		}
	}
}
)AS"),
		TEXT("ATestImplementsGenericLegacy"));
	if (!TestNotNull(TEXT("ScriptClass should compile"), ScriptClass))
	{
		break;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();
	ATestNativeParentInterfaceActor* NativeActor = Spawner.GetWorld().SpawnActor<ATestNativeParentInterfaceActor>();
	AActor* ScriptActor = SpawnScriptActor(*this, Spawner, ScriptClass);
	if (!TestNotNull(TEXT("NativeActor should spawn"), NativeActor)
		|| !TestNotNull(TEXT("ScriptActor should spawn"), ScriptActor))
	{
		break;
	}

	FObjectPropertyBase* TargetProperty = FindFProperty<FObjectPropertyBase>(ScriptActor->GetClass(), TEXT("Target"));
	if (!TestNotNull(TEXT("Script actor should expose 'Target'"), TargetProperty))
	{
		break;
	}
	TargetProperty->SetObjectPropertyValue_InContainer(ScriptActor, NativeActor);

	BeginPlayActor(Engine, *ScriptActor);

	int32 bLegacyResult = 0;
	if (!ReadPropertyValue<FIntProperty>(*this, ScriptActor, TEXT("bLegacyResult"), bLegacyResult))
	{
		break;
	}

	TestEqual(TEXT("Legacy ImplementsInterface(UClass) must continue to work after Phase 5 sugar"),
		bLegacyResult, 1);

	}
	while (false);
	ASTEST_END_SHARE_FRESH
	return !HasAnyErrors();
}

#endif
