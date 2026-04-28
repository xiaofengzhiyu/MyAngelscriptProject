#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASStruct.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

// Test Layer: Runtime Integration
#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace ASStructDiscardTest
{
	static const FName ModuleName(TEXT("ASStructDiscardModule"));
	static const FName StructName(TEXT("DiscardableStruct"));
	static const FString ScriptFilename(TEXT("ASStructDiscardModule.as"));

	FString GetScriptAbsoluteFilename()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), ScriptFilename);
	}

	UASStruct* FindStruct()
	{
		return FindObject<UASStruct>(FAngelscriptEngine::GetPackage(), *StructName.ToString());
	}

	bool VerifyStructState(
		FAutomationTestBase& Test,
		UASStruct* Struct,
		const TCHAR* StageLabel,
		bool bExpectScriptType,
		bool bExpectToStringFunction,
		bool bExpectIdentical,
		bool bExpectHash)
	{
		const FString StructMessage = FString::Printf(TEXT("%s should publish a script struct"), StageLabel);
		if (!Test.TestNotNull(*StructMessage, Struct))
		{
			return false;
		}

		UScriptStruct::ICppStructOps* CppStructOps = Struct->GetCppStructOps();
		const FString OpsMessage = FString::Printf(TEXT("%s should keep cpp struct ops allocated"), StageLabel);
		if (!Test.TestNotNull(*OpsMessage, CppStructOps))
		{
			return false;
		}

		const bool bScriptTypeMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected script type presence"), StageLabel),
			Struct->ScriptType != nullptr,
			bExpectScriptType);
		const bool bToStringMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected ToString binding presence"), StageLabel),
			Struct->GetToStringFunction() != nullptr,
			bExpectToStringFunction);
		const bool bStructFlagMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected STRUCT_IdenticalNative flag"), StageLabel),
			EnumHasAnyFlags(Struct->StructFlags, STRUCT_IdenticalNative),
			bExpectIdentical);
		const bool bHasIdenticalMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected cpp-ops identical capability"), StageLabel),
			CppStructOps->HasIdentical(),
			bExpectIdentical);
		const bool bHasTypeHashMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected cpp-ops hash capability"), StageLabel),
			CppStructOps->HasGetTypeHash(),
			bExpectHash);
		const bool bComputedPropertyFlagMatches = Test.TestEqual(
			*FString::Printf(TEXT("%s should keep the expected CPF_HasGetValueTypeHash computed property flag"), StageLabel),
			EnumHasAnyFlags(CppStructOps->GetComputedPropertyFlags(), CPF_HasGetValueTypeHash),
			bExpectHash);
		return bScriptTypeMatches
			&& bToStringMatches
			&& bStructFlagMatches
			&& bHasIdenticalMatches
			&& bHasTypeHashMatches
			&& bComputedPropertyFlagMatches;
	}
}

using namespace ASStructDiscardTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptASStructDiscardModuleClearsScriptTypeAndNativeOpsTest,
	"Angelscript.TestModule.ClassGenerator.ASStruct.DiscardModuleClearsScriptTypeAndNativeOps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptASStructDiscardModuleClearsScriptTypeAndNativeOpsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	bool bPassed = true;
	ASTEST_BEGIN_SHARE_FRESH
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ASStructDiscardTest::ModuleName.ToString());
		IFileManager::Get().Delete(*ASStructDiscardTest::GetScriptAbsoluteFilename(), false, true, true);
		ResetSharedCloneEngine(Engine);
	};

	const FString ScriptSource = TEXT(R"AS(
USTRUCT()
struct FDiscardableStruct
{
	UPROPERTY()
	int Value = 7;

	bool opEquals(const FDiscardableStruct& Other) const
	{
		return Value == Other.Value;
	}

	uint32 Hash() const
	{
		return uint32(Value + 11);
	}

	FString ToString() const
	{
		return "Discardable";
	}
};
)AS");

	if (!TestTrue(
			TEXT("ASStruct discard test should compile the struct module"),
			CompileAnnotatedModuleFromMemory(&Engine, ASStructDiscardTest::ModuleName, ASStructDiscardTest::ScriptFilename, ScriptSource)))
	{
		return false;
	}

	UASStruct* Struct = ASStructDiscardTest::FindStruct();
	if (!TestNotNull(TEXT("ASStruct discard test should register the generated struct in the Angelscript package"), Struct))
	{
		return false;
	}

	Struct->PrepareCppStructOps();
	if (!ASStructDiscardTest::VerifyStructState(
			*this,
			Struct,
			TEXT("ASStruct discard test before discard"),
			true,
			true,
			true,
			true))
	{
		return false;
	}

	const bool bDiscarded = Engine.DiscardModule(*ASStructDiscardTest::ModuleName.ToString());
	bPassed &= TestTrue(TEXT("ASStruct discard test should discard the owning module successfully"), bDiscarded);
	bPassed &= TestFalse(
		TEXT("ASStruct discard test should remove the module record after discard"),
		Engine.GetModuleByModuleName(ASStructDiscardTest::ModuleName.ToString()).IsValid());
	if (!ASStructDiscardTest::VerifyStructState(
			*this,
			Struct,
			TEXT("ASStruct discard test after discard"),
			false,
			false,
			false,
			false))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return bPassed;
}

#endif
