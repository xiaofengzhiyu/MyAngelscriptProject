#include "Testing/Network/FakeNetDriver.h"

#include "Misc/AutomationTest.h"
#include "UObject/Class.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptFakeNetDriverTests_Private
{
	UFakeNetDriver* CreateTransientFakeNetDriver(FAutomationTestBase& Test, const TCHAR* Context)
	{
		UPackage* TransientOuter = GetTransientPackage();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should resolve the transient package"), Context), TransientOuter))
		{
			return nullptr;
		}

		const FName ObjectName = MakeUniqueObjectName(TransientOuter, UFakeNetDriver::StaticClass(), TEXT("AngelscriptFakeNetDriver"));
		UFakeNetDriver* Driver = NewObject<UFakeNetDriver>(TransientOuter, ObjectName, RF_Transient);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should allocate a fake net driver object"), Context), Driver))
		{
			return nullptr;
		}

		return Driver;
	}
}

using namespace AngelscriptFakeNetDriverTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFakeNetDriverDefaultsTest,
	"Angelscript.CppTests.Networking.FakeNetDriver.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFakeNetDriverGarbageCollectionTest,
	"Angelscript.CppTests.Networking.FakeNetDriver.GarbageCollection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptFakeNetDriverDefaultsTest::RunTest(const FString& Parameters)
{
	UFakeNetDriver* Driver = CreateTransientFakeNetDriver(*this, TEXT("FakeNetDriver.Defaults"));
	if (Driver == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Fake net driver defaults test should derive from UNetDriver"), Driver->IsA<UNetDriver>());
	TestTrue(TEXT("Fake net driver defaults test should keep the UCLASS transient flag"), Driver->GetClass()->HasAnyClassFlags(CLASS_Transient));
	TestTrue(TEXT("Fake net driver defaults test should keep the UCLASS config flag"), Driver->GetClass()->HasAnyClassFlags(CLASS_Config));
	TestEqual(TEXT("Fake net driver defaults test should keep the Engine config bucket"), Driver->GetClass()->ClassConfigName, NAME_Engine);
	TestTrue(TEXT("Fake net driver defaults test should preserve the transient object flag on test instances"), Driver->HasAnyFlags(RF_Transient));
	TestTrue(TEXT("Fake net driver defaults test should default the server seam to true"), Driver->bIsServer);
	TestTrue(TEXT("Fake net driver defaults test should report server mode through the UNetDriver override"), Driver->IsServer());

	Driver->bIsServer = false;
	TestFalse(TEXT("Fake net driver defaults test should mirror a false server seam through IsServer()"), Driver->IsServer());

	Driver->bIsServer = true;
	return TestTrue(TEXT("Fake net driver defaults test should mirror a restored true server seam through IsServer()"), Driver->IsServer());
}

bool FAngelscriptFakeNetDriverGarbageCollectionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UFakeNetDriver> StrongDriver(CreateTransientFakeNetDriver(*this, TEXT("FakeNetDriver.GarbageCollection")));
	if (!TestNotNull(TEXT("Fake net driver garbage-collection test should keep a strong reference to the driver"), StrongDriver.Get()))
	{
		return false;
	}

	TWeakObjectPtr<UFakeNetDriver> WeakDriver = StrongDriver.Get();
	TestTrue(TEXT("Fake net driver garbage-collection test should keep the driver alive while strongly referenced"), WeakDriver.IsValid());

	StrongDriver.Reset();
	CollectGarbage(RF_NoFlags, true);

	return TestFalse(TEXT("Fake net driver garbage-collection test should release the transient driver after the final strong reference is dropped"), WeakDriver.IsValid());
}

#endif
