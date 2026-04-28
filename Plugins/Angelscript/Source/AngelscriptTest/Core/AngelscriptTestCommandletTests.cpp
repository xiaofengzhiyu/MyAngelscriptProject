#include "AngelscriptTestCommandlet.h"
#include "AngelscriptEngine.h"
#include "Testing/AngelscriptTestSettings.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "CoreGlobals.h"
#include "HAL/CriticalSection.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Core_AngelscriptTestCommandletTests_Private
{
	class FCommandletLogCaptureOutputDevice final : public FOutputDevice
	{
	public:
		virtual bool CanBeUsedOnAnyThread() const override
		{
			return true;
		}

		virtual bool CanBeUsedOnMultipleThreads() const override
		{
			return true;
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category) override
		{
			FScopeLock Lock(&EntriesCriticalSection);
			FString& Entry = Entries.AddDefaulted_GetRef();
			Entry = FString::Printf(
				TEXT("[%s][%d] %s"),
				*Category.ToString(),
				static_cast<int32>(Verbosity & ELogVerbosity::VerbosityMask),
				V);
		}

		bool ContainsFragment(const FString& Fragment) const
		{
			FScopeLock Lock(&EntriesCriticalSection);
			for (const FString& Entry : Entries)
			{
				if (Entry.Contains(Fragment))
				{
					return true;
				}
			}

			return false;
		}

		FString DescribeEntries() const
		{
			FScopeLock Lock(&EntriesCriticalSection);
			return Entries.Num() > 0 ? FString::Join(Entries, TEXT(" | ")) : TEXT("<none>");
		}

	private:
		mutable FCriticalSection EntriesCriticalSection;
		TArray<FString> Entries;
	};

	FString MakeCommandletTestSuffix()
	{
		return FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
	}

	FName MakeCommandletModuleName(const FString& Suffix)
	{
		return FName(*FString::Printf(TEXT("ASCommandletFailure%s"), *Suffix));
	}

	FString MakeCommandletFilename(const FString& Suffix)
	{
		return FString::Printf(TEXT("Automation/Core/AngelscriptTestCommandlet_%s.as"), *Suffix);
	}

	FString MakeFailingUnitTestScript()
	{
	return TEXT(R"ANGELSCRIPT(
void Test_CommandletShouldFail(FUnitTest& T)
{
	T.AddExpectedError("CommandletShouldFail");
}
)ANGELSCRIPT");
	}

	struct FAutomationTestOutputDeviceLayout : FOutputDevice
	{
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
		}

		std::atomic<FAutomationTestBase*> CurTest{ nullptr };
	};

	struct FAutomationTestMessageFilterLayout : FFeedbackContext
	{
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
		}

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category, double Time) override
		{
		}

		virtual void SerializeRecord(const UE::FLogRecord& Record) override
		{
		}

		std::atomic<FAutomationTestBase*> CurTest{ nullptr };
		std::atomic<FFeedbackContext*> DestinationContext{ nullptr };
		FTransactionallySafeCriticalSection ActionCS;
	};

	// Mirrors the private UE 5.7 AutomationTest framework prefix up to CurrentTest so this
	// test can temporarily detach the outer automation context while invoking commandlet logic.
	struct FAutomationTestFrameworkLayout
	{
		FSimpleMulticastDelegate PreTestingEvent;
		FSimpleMulticastDelegate PostTestingEvent;
		FOnTestEvent OnTestStartEvent;
		FOnTestEvent OnTestEndEvent;
		FOnTestScreenshotComparisonComplete OnScreenshotCompared;
		FOnTestScreenshotComparisonReport OnScreenshotComparisonReport;
		FOnTestDataRetrieved OnTestDataRetrieved;
		FOnPerformanceDataRetrieved OnPerformanceDataRetrieved;
		FSimpleMulticastDelegate OnScreenshotTakenAndCompared;
		FSimpleMulticastDelegate OnBeforeAllTestsEvent;
		FSimpleMulticastDelegate OnAfterAllTestsEvent;
		FAutomationTestOutputDeviceLayout AutomationTestOutputDevice;
		FAutomationTestMessageFilterLayout AutomationTestMessageFilter;
		FFeedbackContext* OriginalGWarn = nullptr;
		TMap<FString, FAutomationTestBase*> AutomationTestClassNameToInstanceMap;
		TSet<FString> AllExistingTags;
		TMap<FString, FString> TestFullNameToTagDataMap;
		TMap<FString, TSet<FString>> ImmutableTags;
		TQueue<TSharedPtr<IAutomationLatentCommand>> LatentCommands;
		TQueue<TSharedPtr<IAutomationNetworkCommand>> NetworkCommands;
		EAutomationTestFlags RequestedTestFilter = EAutomationTestFlags::None;
		double StartTime = 0.0;
		bool bTestSuccessful = false;
		FAutomationTestBase* CurrentTest = nullptr;
	};

	FAutomationTestFrameworkLayout& AccessAutomationTestFrameworkLayout()
	{
		return reinterpret_cast<FAutomationTestFrameworkLayout&>(FAutomationTestFramework::Get());
	}

	struct FScopedDetachedAutomationContext
	{
		FScopedDetachedAutomationContext()
			: Layout(AccessAutomationTestFrameworkLayout())
			, SavedCurrentTest(Layout.CurrentTest)
			, SavedOutputDeviceTest(Layout.AutomationTestOutputDevice.CurTest.load())
			, SavedMessageFilterTest(Layout.AutomationTestMessageFilter.CurTest.load())
		{
			Layout.CurrentTest = nullptr;
			Layout.AutomationTestOutputDevice.CurTest.store(nullptr);
			Layout.AutomationTestMessageFilter.CurTest.store(nullptr);
		}

		~FScopedDetachedAutomationContext()
		{
			Layout.CurrentTest = SavedCurrentTest;
			Layout.AutomationTestOutputDevice.CurTest.store(SavedOutputDeviceTest);
			Layout.AutomationTestMessageFilter.CurTest.store(SavedMessageFilterTest);
		}

	private:
		FAutomationTestFrameworkLayout& Layout;
		FAutomationTestBase* SavedCurrentTest = nullptr;
		FAutomationTestBase* SavedOutputDeviceTest = nullptr;
		FAutomationTestBase* SavedMessageFilterTest = nullptr;
	};
}

using namespace AngelscriptTest_Core_AngelscriptTestCommandletTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptTestCommandletUnitTestFailureReturnsTwoTest,
	"Angelscript.TestModule.Core.Commandlet.TestCommandlet.UnitTestFailureReturnsTwo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptTestCommandletUnitTestFailureReturnsTwoTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL

	UAngelscriptTestSettings* TestSettings = GetMutableDefault<UAngelscriptTestSettings>();
	if (!TestNotNull(TEXT("TestCommandlet.UnitTestFailureReturnsTwo should resolve mutable test settings"), TestSettings))
	{
		return false;
	}

	const bool bOriginalEnableTestDiscovery = TestSettings->bEnableTestDiscovery;
	const bool bOriginalDidInitialCompileSucceed = Engine.bDidInitialCompileSucceed;
	ON_SCOPE_EXIT
	{
		TestSettings->bEnableTestDiscovery = bOriginalEnableTestDiscovery;
		Engine.bDidInitialCompileSucceed = bOriginalDidInitialCompileSucceed;
	};

	TestSettings->bEnableTestDiscovery = true;
	Engine.bDidInitialCompileSucceed = true;

	const int32 ActiveModulesBeforeCompile = Engine.GetActiveModules().Num();
	const FString ModuleSuffix = MakeCommandletTestSuffix();
	const FName ModuleName = MakeCommandletModuleName(ModuleSuffix);
	const FString ModuleNameString = ModuleName.ToString();
	const FString QualifiedTestName = ModuleNameString + TEXT(".Test_CommandletShouldFail");

	if (!TestTrue(
			TEXT("TestCommandlet.UnitTestFailureReturnsTwo should compile the in-memory module with a failing unit test"),
			CompileModuleFromMemory(&Engine, ModuleName, MakeCommandletFilename(ModuleSuffix), MakeFailingUnitTestScript())))
	{
		return false;
	}

	const TSharedPtr<FAngelscriptModuleDesc> Module = Engine.GetModule(ModuleNameString);
	bool bOk = true;
	bOk &= TestTrue(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should register the compiled module in the active engine"),
		Module.IsValid());
	bOk &= TestEqual(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should add exactly one active module after compilation"),
		Engine.GetActiveModules().Num(),
		ActiveModulesBeforeCompile + 1);
	if (!bOk || !Module.IsValid())
	{
		return false;
	}

	asIScriptModule* ScriptModule = Module->ScriptModule;
	if (!TestNotNull(TEXT("TestCommandlet.UnitTestFailureReturnsTwo should expose the compiled raw script module"), ScriptModule))
	{
		return false;
	}

	asIScriptFunction* UnitTestFunction = GetFunctionByDecl(*this, *ScriptModule, TEXT("void Test_CommandletShouldFail(FUnitTest&)"));
	if (!TestNotNull(TEXT("TestCommandlet.UnitTestFailureReturnsTwo should resolve the failing unit test function by declaration"), UnitTestFunction))
	{
		return false;
	}

	Module->UnitTestFunctions.Reset();
	FAngelscriptTestDesc TestDesc;
	TestDesc.Function = UnitTestFunction;
	TestDesc.bIsComplexTest = false;
	TestDesc.ComplexTestParam.Reset();
	Module->UnitTestFunctions.Add(TEXT("Test_CommandletShouldFail"), TestDesc);

	bOk &= TestTrue(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should discover the failing unit test in the compiled module"),
		Module->UnitTestFunctions.Contains(TEXT("Test_CommandletShouldFail")));
	bOk &= TestEqual(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should discover exactly one unit test in the compiled module"),
		Module->UnitTestFunctions.Num(),
		1);
	bOk &= TestTrue(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should start the commandlet branch with a successful initial compile state"),
		Engine.bDidInitialCompileSucceed);
	if (!bOk)
	{
		return false;
	}

	const int32 ActiveModulesBeforeCommandlet = Engine.GetActiveModules().Num();

	FCommandletLogCaptureOutputDevice CaptureDevice;
	GLog->AddOutputDevice(&CaptureDevice);
	ON_SCOPE_EXIT
	{
		GLog->RemoveOutputDevice(&CaptureDevice);
	};

	UAngelscriptTestCommandlet* Commandlet = NewObject<UAngelscriptTestCommandlet>(GetTransientPackage());
	if (!TestNotNull(TEXT("TestCommandlet.UnitTestFailureReturnsTwo should create the test commandlet object"), Commandlet))
	{
		return false;
	}

	const int32 Result = [&Commandlet]()
	{
		FScopedDetachedAutomationContext DetachedAutomationContext;
		return Commandlet->Main(TEXT(""));
	}();

	bOk = true;
	bOk &= TestEqual(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should return exit code 2 when a discovered unit test fails"),
		Result,
		2);
	bOk &= TestTrue(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should leave bDidInitialCompileSucceed true to prove the failure came from unit tests"),
		Engine.bDidInitialCompileSucceed);
	bOk &= TestEqual(
		TEXT("TestCommandlet.UnitTestFailureReturnsTwo should preserve the active module count across Main()"),
		Engine.GetActiveModules().Num(),
		ActiveModulesBeforeCommandlet);
	bOk &= TestTrue(
		FString::Printf(
			TEXT("TestCommandlet.UnitTestFailureReturnsTwo should log a [FAILED] marker for the failing unit test; captured logs: %s"),
			*CaptureDevice.DescribeEntries()),
		CaptureDevice.ContainsFragment(TEXT("[FAILED]")));
	bOk &= TestTrue(
		FString::Printf(
			TEXT("TestCommandlet.UnitTestFailureReturnsTwo should log either the failing unit test name '%s' or the failure payload; captured logs: %s"),
			*QualifiedTestName,
			*CaptureDevice.DescribeEntries()),
		CaptureDevice.ContainsFragment(QualifiedTestName) || CaptureDevice.ContainsFragment(TEXT("CommandletShouldFail")));

	bPassed = bOk;

	ASTEST_END_FULL
	return bPassed;
}

#endif
