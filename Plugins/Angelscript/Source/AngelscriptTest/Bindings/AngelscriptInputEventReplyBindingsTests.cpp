#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/SlateWrapperTypes.h"
#include "Input/Reply.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Types/SlateEnums.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptInputEventReplyBindingsTests_Private
{
	static constexpr ANSICHAR EventReplyModuleName[] = "ASInputEventReplyStateCompat";
	static constexpr ANSICHAR InputChordModuleName[] = "ASInputChordConstructorCompat";

	bool ExecuteEventReplyFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		FEventReply& OutReply)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Input event reply test should create an execution context"), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		if (!Test.TestEqual(TEXT("Input event reply function should prepare"), Context->Prepare(Function), asSUCCESS))
		{
			return false;
		}

		const int32 ExecuteResult = Context->Execute();
		if (!Test.TestEqual(TEXT("Input event reply function should execute"), ExecuteResult, asEXECUTION_FINISHED))
		{
			if (ExecuteResult == asEXECUTION_EXCEPTION && Context->GetExceptionString() != nullptr)
			{
				Test.AddError(FString::Printf(
					TEXT("Input event reply function raised script exception: %s"),
					UTF8_TO_TCHAR(Context->GetExceptionString())));
			}
			return false;
		}

		void* ReturnValueAddress = Context->GetAddressOfReturnValue();
		if (!Test.TestNotNull(TEXT("Input event reply function should expose return storage"), ReturnValueAddress))
		{
			return false;
		}

		OutReply = *static_cast<FEventReply*>(ReturnValueAddress);
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptInputEventReplyBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputEventReplyStateBindingsTest,
	"Angelscript.TestModule.Bindings.InputEvents.EventReplyStateCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptInputChordConstructorBindingsTest,
	"Angelscript.TestModule.Bindings.InputEvents.InputChordConstructorCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptInputEventReplyStateBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(EventReplyModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		EventReplyModuleName,
		TEXT(R"AS(
FEventReply BuildHandledReply()
{
	FEventReply Reply = FEventReply::Handled();
	Reply.PreventThrottling();
	Reply.SetMousePos(FIntPoint(12, 34));
	Reply.SetNavigation(EUINavigation::Down, ENavigationGenesis::Keyboard, ENavigationSource::WidgetUnderCursor);
	Reply.ReleaseMouseCapture();
	Reply.ReleaseMouseLock();
	Reply.ClearUserFocus(true);
	return Reply;
}

FEventReply BuildUnhandledReply()
{
	FEventReply Reply = FEventReply::Unhandled();
	Reply.PreventThrottling();
	return Reply;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	FEventReply HandledReply;
	if (!ExecuteEventReplyFunction(*this, Engine, *Module, TEXT("FEventReply BuildHandledReply()"), HandledReply))
	{
		return false;
	}

	const FReply& HandledNativeReply = HandledReply.NativeReply;
	bPassed &= TestTrue(TEXT("FEventReply::Handled should return a handled native reply"), HandledNativeReply.IsEventHandled());
	bPassed &= TestFalse(TEXT("PreventThrottling should disable reply throttling"), HandledNativeReply.ShouldThrottle());
	bPassed &= TestTrue(TEXT("SetMousePos should record a requested mouse position"), HandledNativeReply.GetRequestedMousePos().IsSet());
	if (HandledNativeReply.GetRequestedMousePos().IsSet())
	{
		bPassed &= TestEqual(
			TEXT("SetMousePos should preserve the requested coordinates"),
			HandledNativeReply.GetRequestedMousePos().GetValue(),
			FIntPoint(12, 34));
	}
	bPassed &= TestEqual(
		TEXT("SetNavigation should preserve the requested navigation type"),
		static_cast<uint8>(HandledNativeReply.GetNavigationType()),
		static_cast<uint8>(EUINavigation::Down));
	bPassed &= TestEqual(
		TEXT("SetNavigation should preserve the requested navigation genesis"),
		static_cast<uint8>(HandledNativeReply.GetNavigationGenesis()),
		static_cast<uint8>(ENavigationGenesis::Keyboard));
	bPassed &= TestEqual(
		TEXT("SetNavigation should preserve the requested navigation source"),
		static_cast<uint8>(HandledNativeReply.GetNavigationSource()),
		static_cast<uint8>(ENavigationSource::WidgetUnderCursor));
	bPassed &= TestTrue(TEXT("ReleaseMouseCapture should mark the native reply for mouse release"), HandledNativeReply.ShouldReleaseMouse());
	bPassed &= TestTrue(TEXT("ReleaseMouseLock should mark the native reply for mouse-lock release"), HandledNativeReply.ShouldReleaseMouseLock());
	bPassed &= TestTrue(TEXT("ClearUserFocus(true) should request user-focus release"), HandledNativeReply.ShouldReleaseUserFocus());
	bPassed &= TestFalse(TEXT("ClearUserFocus should not leave a set-focus request active"), HandledNativeReply.ShouldSetUserFocus());
	bPassed &= TestTrue(TEXT("ClearUserFocus(true) should affect all users"), HandledNativeReply.AffectsAllUsers());

	FEventReply UnhandledReply;
	if (!ExecuteEventReplyFunction(*this, Engine, *Module, TEXT("FEventReply BuildUnhandledReply()"), UnhandledReply))
	{
		return false;
	}

	const FReply& UnhandledNativeReply = UnhandledReply.NativeReply;
	bPassed &= TestFalse(TEXT("FEventReply::Unhandled should return an unhandled native reply"), UnhandledNativeReply.IsEventHandled());
	bPassed &= TestFalse(TEXT("PreventThrottling should also affect unhandled replies"), UnhandledNativeReply.ShouldThrottle());
	bPassed &= TestFalse(TEXT("Unhandled reply should not request a mouse position by default"), UnhandledNativeReply.GetRequestedMousePos().IsSet());
	bPassed &= TestEqual(
		TEXT("Unhandled reply should keep navigation invalid by default"),
		static_cast<uint8>(UnhandledNativeReply.GetNavigationType()),
		static_cast<uint8>(EUINavigation::Invalid));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptInputChordConstructorBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(InputChordModuleName));
	};

	int32 Result = INDEX_NONE;
	ASTEST_COMPILE_RUN_INT(
		Engine,
		InputChordModuleName,
		TEXT(R"AS(
int Entry()
{
	FInputChord KeyOnly(EKeys::A);
	if (!(KeyOnly.Key == EKeys::A))
		return 10;
	if (KeyOnly.bShift || KeyOnly.bCtrl || KeyOnly.bAlt || KeyOnly.bCmd)
		return 20;

	FInputChord Modified(EKeys::B, true, false, true, false);
	if (!(Modified.Key == EKeys::B))
		return 30;
	if (!Modified.bShift || Modified.bCtrl || !Modified.bAlt || Modified.bCmd)
		return 40;

	Modified.Key = EKeys::C;
	Modified.bCtrl = true;
	Modified.bAlt = false;
	if (!(Modified.Key == EKeys::C))
		return 50;
	if (!Modified.bShift || !Modified.bCtrl || Modified.bAlt || Modified.bCmd)
		return 60;

	FInputChord FromName(FKey(n"Enter"));
	if (!(FromName.Key == EKeys::Enter))
		return 70;
	if (FromName.bShift || FromName.bCtrl || FromName.bAlt || FromName.bCmd)
		return 80;

	return 1;
}
)AS"),
		TEXT("int Entry()"),
		Result);

	bPassed &= TestEqual(
		TEXT("FInputChord script constructors should preserve key and modifier field readback"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
