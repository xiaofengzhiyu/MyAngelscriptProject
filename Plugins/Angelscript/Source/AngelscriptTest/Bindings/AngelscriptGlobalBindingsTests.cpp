#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGlobalVariableBindingsTest,
	"Angelscript.TestModule.Bindings.GlobalVariableCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGlobalCommandletGlobalsBindingsTest,
	"Angelscript.TestModule.Bindings.GlobalCommandletGlobalsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGlobalVariableBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASGlobalVariableCompat",
		TEXT(R"(
int Entry()
{
	if (CollisionProfile::BlockAllDynamic.Compare(FName("BlockAllDynamic")) != 0)
		return 10;

	FComponentQueryParams FreshParams;
	if (FComponentQueryParams::DefaultComponentQueryParams.ShapeCollisionMask.Bits != FreshParams.ShapeCollisionMask.Bits)
		return 20;

	FGameplayTag EmptyTagCopy = FGameplayTag::EmptyTag;
	if (EmptyTagCopy.IsValid())
		return 30;
	if (!FGameplayTagContainer::EmptyContainer.IsEmpty())
		return 40;
	if (!FGameplayTagQuery::EmptyQuery.IsEmpty())
		return 50;

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

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed = TestEqual(TEXT("Global variable compat operations should preserve bound namespace globals and defaults"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return bPassed;
}

bool FAngelscriptGlobalCommandletGlobalsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = false;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	const bool bExpectedRunningCommandlet = ::IsRunningCommandlet();
	const bool bExpectedRunningCookCommandlet = ::IsRunningCookCommandlet();
	const bool bExpectedRunningDLCCookCommandlet = ::IsRunningDLCCookCommandlet();
	UClass* ExpectedCommandletClass = ::GetRunningCommandletClass();

	FString Script = TEXT(R"(
int Entry()
{
	if (IsRunningCommandlet() != $IS_RUNNING_COMMANDLET$)
		return 10;
	if (IsRunningCookCommandlet() != $IS_RUNNING_COOK_COMMANDLET$)
		return 20;
	if (IsRunningDLCCookCommandlet() != $IS_RUNNING_DLC_COOK_COMMANDLET$)
		return 30;

	UClass RunningCommandletClass = GetRunningCommandletClass();
	if ($EXPECTS_NULL_COMMANDLET_CLASS$)
	{
		if (!(RunningCommandletClass == null))
			return 40;
	}
	else
	{
		if (!IsValid(RunningCommandletClass))
			return 50;
		if (!(RunningCommandletClass.GetName() == "$RUNNING_COMMANDLET_CLASS_NAME$"))
			return 60;
	}

	return 1;
}
)");
	Script.ReplaceInline(TEXT("$IS_RUNNING_COMMANDLET$"), bExpectedRunningCommandlet ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(TEXT("$IS_RUNNING_COOK_COMMANDLET$"), bExpectedRunningCookCommandlet ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(TEXT("$IS_RUNNING_DLC_COOK_COMMANDLET$"), bExpectedRunningDLCCookCommandlet ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(TEXT("$EXPECTS_NULL_COMMANDLET_CLASS$"), ExpectedCommandletClass == nullptr ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(
		TEXT("$RUNNING_COMMANDLET_CLASS_NAME$"),
		ExpectedCommandletClass != nullptr
			? *ExpectedCommandletClass->GetName().ReplaceCharWithEscapedChar()
			: TEXT(""));

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASGlobalCommandletGlobalsCompat",
		Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed = TestEqual(
		TEXT("Commandlet global compat operations should preserve running commandlet flags and the current commandlet class"),
		Result,
		1);
	ASTEST_END_SHARE_CLEAN

	return bPassed;
}

#endif
