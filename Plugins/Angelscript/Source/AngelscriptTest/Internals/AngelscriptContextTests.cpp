#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_context.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptContextTests_Private
{
	FString GetFunctionDeclaration(asIScriptFunction* Function)
	{
		return Function != nullptr ? UTF8_TO_TCHAR(Function->GetDeclaration()) : FString();
	}

	bool DeclarationContains(asIScriptFunction* Function, const TCHAR* Needle)
	{
		return Function != nullptr && GetFunctionDeclaration(Function).Contains(Needle);
	}
}

using namespace AngelscriptTest_Internals_AngelscriptContextTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptContextStateAndUserDataTest,
	"Angelscript.TestModule.Internals.Context.StateAndUserData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptContextExceptionMetadataTest,
	"Angelscript.TestModule.Internals.Context.ExceptionMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptContextStateAndUserDataTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ContextStateAndUserData",
		TEXT(R"AS(
int AddOne(int Value)
{
	int Local = Value + 1;
	return Local;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int AddOne(int)"));
	if (Function == nullptr)
	{
		return false;
	}

	asIScriptContext* RawContext = Engine.CreateContext();
	if (!TestNotNull(TEXT("Context.StateAndUserData should create a script context"), RawContext))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RawContext->Release();
	};

	asCContext* Context = static_cast<asCContext*>(RawContext);
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should start in the uninitialized state"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));

	void* const FirstPrimaryValue = reinterpret_cast<void*>(static_cast<SIZE_T>(0x1010));
	void* const ReplacedPrimaryValue = reinterpret_cast<void*>(static_cast<SIZE_T>(0x2020));
	void* const SecondaryValue = reinterpret_cast<void*>(static_cast<SIZE_T>(0x3030));
	constexpr asPWORD PrimarySlot = 0;
	constexpr asPWORD SecondarySlot = 7;

	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should report an empty default user-data slot before writes"),
		Context->GetUserData(PrimarySlot) == nullptr);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should report an empty secondary user-data slot before writes"),
		Context->GetUserData(SecondarySlot) == nullptr);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should not return a previous pointer when populating the default slot the first time"),
		Context->SetUserData(FirstPrimaryValue, PrimarySlot) == nullptr);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should return the previous default-slot pointer when replacing it"),
		Context->SetUserData(ReplacedPrimaryValue, PrimarySlot) == FirstPrimaryValue);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should not return a previous pointer when populating an independent secondary slot"),
		Context->SetUserData(SecondaryValue, SecondarySlot) == nullptr);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should keep the replaced default-slot pointer visible"),
		Context->GetUserData(PrimarySlot) == ReplacedPrimaryValue);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should keep the secondary-slot pointer isolated from the default slot"),
		Context->GetUserData(SecondarySlot) == SecondaryValue);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should keep unrelated user-data slots empty"),
		Context->GetUserData(99) == nullptr);

	const int32 PrepareResult = Context->Prepare(Function);
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should prepare successfully"),
		PrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should transition to the prepared state after Prepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));

	const int32 SetArgResult = PrepareResult == asSUCCESS ? Context->SetArgDWord(0, 41) : PrepareResult;
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should accept the integer argument"),
		SetArgResult,
		static_cast<int32>(asSUCCESS));

	const int32 ExecuteResult = SetArgResult == asSUCCESS ? Context->Execute() : SetArgResult;
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should execute successfully"),
		ExecuteResult,
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should transition to the finished state after Execute"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should keep the correct return value after execution"),
		static_cast<int32>(Context->GetReturnDWord()),
		42);

	const int32 UnprepareResult = Context->Unprepare();
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should unprepare successfully after a finished run"),
		UnprepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should return to the uninitialized state after Unprepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_UNINITIALIZED));

	const int32 SecondPrepareResult = Context->Prepare(Function);
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should allow the same context to prepare the function again after Unprepare"),
		SecondPrepareResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should re-enter the prepared state on the second Prepare"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_PREPARED));

	const int32 SecondSetArgResult = SecondPrepareResult == asSUCCESS ? Context->SetArgDWord(0, 9) : SecondPrepareResult;
	const int32 SecondExecuteResult = SecondSetArgResult == asSUCCESS ? Context->Execute() : SecondSetArgResult;
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should execute successfully on the second run"),
		SecondExecuteResult,
		static_cast<int32>(asEXECUTION_FINISHED));
	bPassed &= TestEqual(
		TEXT("Context.StateAndUserData should keep the second run return value isolated from the first"),
		static_cast<int32>(Context->GetReturnDWord()),
		10);
	bPassed &= TestTrue(
		TEXT("Context.StateAndUserData should preserve user-data values across prepare/execute/unprepare cycles"),
		Context->GetUserData(PrimarySlot) == ReplacedPrimaryValue && Context->GetUserData(SecondarySlot) == SecondaryValue);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptContextExceptionMetadataTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASContextExceptionMetadata",
		TEXT(R"AS(
void FailInner(int Value)
{
	int Inner = Value * 2;
	if (Inner > 0)
	{
		throw("ContextMetadataFailure");
	}
}

void TriggerFailure(int Seed)
{
	int Local = Seed + 1;
	FailInner(Local);
}

int Entry()
{
	TriggerFailure(20);
	return 0;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	asIScriptContext* RawContext = Engine.CreateContext();
	if (!TestNotNull(TEXT("Context.ExceptionMetadata should create a script context"), RawContext))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		RawContext->Release();
	};

	asCContext* Context = static_cast<asCContext*>(RawContext);
	const int32 PrepareResult = Context->Prepare(EntryFunction);
	if (!TestEqual(
			TEXT("Context.ExceptionMetadata should prepare Entry() successfully"),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	AddExpectedError(TEXT("ContextMetadataFailure"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("ASContextExceptionMetadata"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("void FailInner(int) | Line"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("void TriggerFailure(int) | Line"), EAutomationExpectedErrorFlags::Contains, 1, false);
	AddExpectedError(TEXT("int Entry() | Line"), EAutomationExpectedErrorFlags::Contains, 1, false);

	const int32 ExecuteResult = Context->Execute();
	int32 ExceptionColumn = 0;
	const char* ExceptionSectionAnsi = nullptr;
	const int32 ExceptionLine = Context->GetExceptionLineNumber(&ExceptionColumn, &ExceptionSectionAnsi);
	const FString ExceptionSection = ExceptionSectionAnsi != nullptr ? UTF8_TO_TCHAR(ExceptionSectionAnsi) : FString();
	const FString ExceptionString = Context->GetExceptionString() != nullptr ? UTF8_TO_TCHAR(Context->GetExceptionString()) : FString();
	asIScriptFunction* ExceptionFunction = Context->GetExceptionFunction();
	const asUINT CallstackSize = Context->GetCallstackSize();

	bool bFoundInnerFrame = false;
	bool bFoundTriggerFrame = false;
	bool bFoundEntryFrame = false;
	bool bAllFrameLinesPositive = true;
	bool bFoundInnerVariable = false;
	bool bFoundLocalVariable = false;
	bool bFoundAnyVarAddress = false;

	for (asUINT StackLevel = 0; StackLevel < CallstackSize; ++StackLevel)
	{
		asIScriptFunction* StackFunction = Context->GetFunction(StackLevel);
		const int32 StackLine = Context->GetLineNumber(StackLevel, nullptr, nullptr);
		bAllFrameLinesPositive &= StackLine > 0;
		bFoundInnerFrame |= DeclarationContains(StackFunction, TEXT("FailInner"));
		bFoundTriggerFrame |= DeclarationContains(StackFunction, TEXT("TriggerFailure"));
		bFoundEntryFrame |= DeclarationContains(StackFunction, TEXT("Entry"));

		const int32 VarCount = Context->GetVarCount(StackLevel);
		for (int32 VarIndex = 0; VarIndex < VarCount; ++VarIndex)
		{
			const char* VarNameAnsi = Context->GetVarName(VarIndex, StackLevel);
			const char* VarDeclAnsi = Context->GetVarDeclaration(VarIndex, StackLevel, false);
			const FString VarName = VarNameAnsi != nullptr ? UTF8_TO_TCHAR(VarNameAnsi) : FString();
			const FString VarDecl = VarDeclAnsi != nullptr ? UTF8_TO_TCHAR(VarDeclAnsi) : FString();

			bFoundInnerVariable |= VarName == TEXT("Inner") || VarDecl.Contains(TEXT("Inner"));
			bFoundLocalVariable |= VarName == TEXT("Local") || VarDecl.Contains(TEXT("Local"));
			bFoundAnyVarAddress |= Context->GetAddressOfVar(VarIndex, StackLevel) != nullptr;
		}
	}

	bPassed &= TestEqual(
		TEXT("Context.ExceptionMetadata should stop with an execution exception"),
		ExecuteResult,
		static_cast<int32>(asEXECUTION_EXCEPTION));
	bPassed &= TestEqual(
		TEXT("Context.ExceptionMetadata should expose the exception state after Execute"),
		static_cast<int32>(Context->GetState()),
		static_cast<int32>(asEXECUTION_EXCEPTION));
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should preserve the thrown exception string"),
		!ExceptionString.IsEmpty() && ExceptionString.Contains(TEXT("ContextMetadataFailure")));
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should surface the innermost failing function"),
		DeclarationContains(ExceptionFunction, TEXT("FailInner")));
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should expose a positive exception line number"),
		ExceptionLine > 0);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should expose a positive exception column number"),
		ExceptionColumn > 0);
	bPassed &= TestFalse(
		TEXT("Context.ExceptionMetadata should expose a non-empty exception section name"),
		ExceptionSection.IsEmpty());
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should preserve at least three frames in the callstack"),
		CallstackSize >= 3);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should keep the failing inner frame in the callstack"),
		bFoundInnerFrame);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should keep the middle frame in the callstack"),
		bFoundTriggerFrame);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should keep the entry frame in the callstack"),
		bFoundEntryFrame);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should expose positive source line numbers for captured frames"),
		bAllFrameLinesPositive);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should keep the failing frame's Inner local visible through debugger metadata"),
		bFoundInnerVariable);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should keep the caller frame's Local variable visible through debugger metadata"),
		bFoundLocalVariable);
	bPassed &= TestTrue(
		TEXT("Context.ExceptionMetadata should expose at least one addressable in-scope variable during exception inspection"),
		bFoundAnyVarAddress);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
