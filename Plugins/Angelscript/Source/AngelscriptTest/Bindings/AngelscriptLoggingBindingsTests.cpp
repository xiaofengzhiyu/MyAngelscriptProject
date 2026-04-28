#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "CoreGlobals.h"
#include "HAL/CriticalSection.h"
#include "Misc/AutomationTest.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLoggingConditionalBindingsTest,
	"Angelscript.TestModule.Bindings.Logging.ConditionalAndCategoryCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLoggingThrowIfBindingsTest,
	"Angelscript.TestModule.Bindings.Logging.ThrowIfExceptionCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptLoggingVerbosityRoutingBindingsTest,
	"Angelscript.TestModule.Bindings.LoggingVerbosityRouting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace AngelscriptTest_Bindings_AngelscriptLoggingBindingsTests_Private
{
	static constexpr ANSICHAR LoggingConditionalModuleName[] = "ASLoggingConditionalAndCategoryCompat";
	static constexpr ANSICHAR LoggingThrowIfModuleName[] = "ASLoggingThrowIfExceptionCompat";
	static constexpr ANSICHAR LoggingVerbosityRoutingModuleName[] = "ASLoggingVerbosityRouting";

	struct FLoggingExecutionResult
	{
		int32 PrepareResult = MIN_int32;
		int32 ExecuteResult = MIN_int32;
		FString ExceptionString;
		FString ExceptionFunctionDeclaration;
	};

	struct FCapturedLogEntry
	{
		FName Category;
		ELogVerbosity::Type Verbosity = ELogVerbosity::NoLogging;
		FString Message;
	};

	class FLoggingCaptureOutputDevice final : public FOutputDevice
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
			FCapturedLogEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.Category = Category;
			Entry.Verbosity = static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			Entry.Message = V;
		}

		int32 CountMatchingEntries(ELogVerbosity::Type Verbosity, const FString& Message) const
		{
			FScopeLock Lock(&EntriesCriticalSection);
			int32 Count = 0;
			for (const FCapturedLogEntry& Entry : Entries)
			{
				if (Entry.Verbosity == Verbosity && Entry.Message == Message)
				{
					++Count;
				}
			}

			return Count;
		}

		int32 CountMessageEntries(const FString& Message) const
		{
			FScopeLock Lock(&EntriesCriticalSection);
			int32 Count = 0;
			for (const FCapturedLogEntry& Entry : Entries)
			{
				if (Entry.Message == Message)
				{
					++Count;
				}
			}

			return Count;
		}

		FString DescribeEntries() const
		{
			FScopeLock Lock(&EntriesCriticalSection);
			FString Description;
			for (const FCapturedLogEntry& Entry : Entries)
			{
				if (!Description.IsEmpty())
				{
					Description += TEXT(" | ");
				}

				Description += FString::Printf(
					TEXT("[%s][%d]%s"),
					*Entry.Category.ToString(),
					static_cast<int32>(Entry.Verbosity),
					*Entry.Message);
			}

			return Description;
		}

	private:
		mutable FCriticalSection EntriesCriticalSection;
		TArray<FCapturedLogEntry> Entries;
	};

	bool ExecuteAndCapture(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		FLoggingExecutionResult& OutResult)
	{
		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Logging bindings test should create an execution context"), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		OutResult.PrepareResult = Context->Prepare(&Function);
		OutResult.ExecuteResult = OutResult.PrepareResult == asSUCCESS ? Context->Execute() : OutResult.PrepareResult;
		OutResult.ExceptionString = Context->GetExceptionString() != nullptr ? UTF8_TO_TCHAR(Context->GetExceptionString()) : TEXT("");

		if (asIScriptFunction* ExceptionFunction = Context->GetExceptionFunction())
		{
			OutResult.ExceptionFunctionDeclaration = UTF8_TO_TCHAR(ExceptionFunction->GetDeclaration());
		}

		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptLoggingBindingsTests_Private;

bool FAngelscriptLoggingConditionalBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("LoggingTrueErrorCompat"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("LoggingCategoryErrorCompat"), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(LoggingConditionalModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		LoggingConditionalModuleName,
		TEXT(R"(
int Entry()
{
	LogIf(false, "LoggingFalseLogShouldNotAppear");
	LogInfoIf(false, "LoggingFalseInfoShouldNotAppear");
	LogDisplayIf(false, "LoggingFalseDisplayShouldNotAppear");
	WarningIf(false, "LoggingFalseWarningShouldNotAppear");
	ErrorIf(false, "LoggingFalseErrorShouldNotAppear");

	LogIf(true, "LoggingTrueLogCompat");
	LogInfoIf(true, "LoggingTrueInfoCompat");
	LogDisplayIf(true, "LoggingTrueDisplayCompat");
	WarningIf(true, "LoggingTrueWarningCompat");
	ErrorIf(true, "LoggingTrueErrorCompat");

	FName Category("AngelscriptTestLogging");
	Log(Category, "LoggingCategoryLogCompat");
	LogInfo(Category, "LoggingCategoryInfoCompat");
	LogDisplay(Category, "LoggingCategoryDisplayCompat");
	Warning(Category, "LoggingCategoryWarningCompat");
	Error(Category, "LoggingCategoryErrorCompat");

	LogIf(false, Category, "LoggingFalseCategoryLogShouldNotAppear");
	LogInfoIf(false, Category, "LoggingFalseCategoryInfoShouldNotAppear");
	LogDisplayIf(false, Category, "LoggingFalseCategoryDisplayShouldNotAppear");
	WarningIf(false, Category, "LoggingFalseCategoryWarningShouldNotAppear");
	ErrorIf(false, Category, "LoggingFalseCategoryErrorShouldNotAppear");

	return 1;
}
)"));
	if (Module == nullptr)
	{
		bPassed = false;
	}
	else if (asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()")))
	{
		int32 Result = 0;
		bPassed &= ExecuteIntFunction(*this, Engine, *Function, Result);
		if (bPassed)
		{
			bPassed &= TestEqual(
				TEXT("Logging conditional and category bindings should execute through true and false branches"),
				Result,
				1);
		}
	}
	else
	{
		bPassed = false;
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptLoggingVerbosityRoutingBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("LoggingVerbosityRoutingErrorMessage"), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(LoggingVerbosityRoutingModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		LoggingVerbosityRoutingModuleName,
		TEXT(R"(
int Entry()
{
	Log("LoggingVerbosityRoutingLogMessage");
	LogDisplay("LoggingVerbosityRoutingDisplayMessage");
	Warning("LoggingVerbosityRoutingWarningMessage");
	Error("LoggingVerbosityRoutingErrorMessage");
	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	if (!TestNotNull(TEXT("Logging verbosity routing test should expose GLog for capture"), GLog))
	{
		return false;
	}

	FLoggingCaptureOutputDevice CaptureDevice;
	GLog->AddOutputDevice(&CaptureDevice);
	ON_SCOPE_EXIT
	{
		if (GLog != nullptr)
		{
			GLog->RemoveOutputDevice(&CaptureDevice);
		}
	};

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	GLog->FlushThreadedLogs(EOutputDeviceRedirectorFlushOptions::None);

	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should execute successfully"),
		Result,
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should emit the plain Log() payload exactly once"),
		CaptureDevice.CountMessageEntries(TEXT("LoggingVerbosityRoutingLogMessage")),
		1);
	if (CaptureDevice.CountMessageEntries(TEXT("LoggingVerbosityRoutingLogMessage")) == 0)
	{
		AddInfo(FString::Printf(TEXT("Logging verbosity routing capture dump: %s"), *CaptureDevice.DescribeEntries()));
	}
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should emit the display payload exactly once"),
		CaptureDevice.CountMessageEntries(TEXT("[Display] LoggingVerbosityRoutingDisplayMessage")),
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should emit the warning payload exactly once"),
		CaptureDevice.CountMessageEntries(TEXT("LoggingVerbosityRoutingWarningMessage")),
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should emit the error payload exactly once even when AddExpectedError downgrades the surfaced verbosity"),
		CaptureDevice.CountMessageEntries(TEXT("LoggingVerbosityRoutingErrorMessage")),
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should route Log() through ELogVerbosity::Log without altering the message"),
		CaptureDevice.CountMatchingEntries(
			ELogVerbosity::Log,
			TEXT("LoggingVerbosityRoutingLogMessage")),
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should route LogDisplay() through ELogVerbosity::Display with the display prefix"),
		CaptureDevice.CountMatchingEntries(
			ELogVerbosity::Display,
			TEXT("[Display] LoggingVerbosityRoutingDisplayMessage")),
		1);
	bPassed &= TestEqual(
		TEXT("Logging verbosity routing test should route Warning() through ELogVerbosity::Warning without dropping the payload"),
		CaptureDevice.CountMatchingEntries(
			ELogVerbosity::Warning,
			TEXT("LoggingVerbosityRoutingWarningMessage")),
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptLoggingThrowIfBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	AddExpectedError(TEXT("LoggingThrowIfCompat"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("ASLoggingThrowIfExceptionCompat"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("void EntryThrowIfTrue()"), EAutomationExpectedErrorFlags::Contains, 1);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(LoggingThrowIfModuleName));
		ResetSharedCloneEngine(Engine);
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		LoggingThrowIfModuleName,
		TEXT(R"(
void EntryThrowIfFalse()
{
	ThrowIf(false, "LoggingThrowIfFalseShouldNotThrow");
}

void EntryThrowIfTrue()
{
	ThrowIf(true, "LoggingThrowIfCompat");
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* FalseFunction = GetFunctionByDecl(*this, *Module, TEXT("void EntryThrowIfFalse()"));
	asIScriptFunction* TrueFunction = GetFunctionByDecl(*this, *Module, TEXT("void EntryThrowIfTrue()"));
	if (FalseFunction == nullptr || TrueFunction == nullptr)
	{
		return false;
	}

	FLoggingExecutionResult FalseResult;
	if (!ExecuteAndCapture(*this, Engine, *FalseFunction, FalseResult))
	{
		return false;
	}

	FLoggingExecutionResult TrueResult;
	if (!ExecuteAndCapture(*this, Engine, *TrueFunction, TrueResult))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ThrowIf(false) should prepare successfully"),
		FalseResult.PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("ThrowIf(false) should finish without raising an exception"),
		FalseResult.ExecuteResult,
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestTrue(
		TEXT("ThrowIf(false) should leave the exception string empty"),
		FalseResult.ExceptionString.IsEmpty());

	bPassed &= TestEqual(
		TEXT("ThrowIf(true) should prepare successfully"),
		TrueResult.PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("ThrowIf(true) should raise a script exception"),
		TrueResult.ExecuteResult,
		static_cast<int32>(asEXECUTION_EXCEPTION));
	bPassed &= TestTrue(
		TEXT("ThrowIf(true) should surface the requested exception text"),
		TrueResult.ExceptionString.Contains(TEXT("LoggingThrowIfCompat")));
	bPassed &= TestTrue(
		TEXT("ThrowIf(true) should report the throwing script entry function"),
		TrueResult.ExceptionFunctionDeclaration.Contains(TEXT("EntryThrowIfTrue")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
