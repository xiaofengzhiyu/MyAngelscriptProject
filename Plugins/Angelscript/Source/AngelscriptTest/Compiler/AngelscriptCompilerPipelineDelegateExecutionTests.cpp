#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Core/AngelscriptEngine.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace CompilerPipelineDelegateExecutionTest
{
	static const FName ModuleName(TEXT("CompilerDelegateTrailingCommentExecutes"));
	static const FString ScriptFilename(TEXT("CompilerDelegateTrailingCommentExecutes.as"));
	static const FName GeneratedClassName(TEXT("UCompilerDelegateTrailingCommentCarrier"));
	static const FName RunSingleFunctionName(TEXT("RunSingle"));
	static const FName RunMultiFunctionName(TEXT("RunMulti"));
	static const FString SingleDelegateName(TEXT("FCommentSingle"));
	static const FString MultiDelegateName(TEXT("FCommentMulti"));
}

using namespace CompilerPipelineDelegateExecutionTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCompilerDelegateDeclarationWithTrailingBlockCommentExecutesTest,
	"Angelscript.TestModule.Compiler.EndToEnd.DelegateDeclarationWithTrailingBlockCommentExecutes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptCompilerDelegateDeclarationWithTrailingBlockCommentExecutesTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	const FString ScriptSource = TEXT(R"AS(
delegate void FCommentSingle(int Value) /* trailing block comment */;
event void FCommentMulti(int Value) /* trailing block comment */;

UCLASS()
class UCompilerDelegateTrailingCommentCarrier : UObject
{
	UPROPERTY()
	int Score = 0;

	UPROPERTY()
	FCommentSingle Single;

	UPROPERTY()
	FCommentMulti Multi;

	UFUNCTION()
	void HandleSingle(int Value)
	{
		Score = Value;
	}

	UFUNCTION()
	void HandleMulti(int Value)
	{
		Score = Value;
	}

	UFUNCTION()
	int RunSingle()
	{
		Score = 0;
		Single.BindUFunction(this, n"HandleSingle");
		Single.Execute(17);
		return Score;
	}

	UFUNCTION()
	int RunMulti()
	{
		Score = 0;
		Multi.AddUFunction(this, n"HandleMulti");
		Multi.Broadcast(29);
		return Score;
	}
}
)AS");

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*CompilerPipelineDelegateExecutionTest::ModuleName.ToString());
	};

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::FullReload,
		CompilerPipelineDelegateExecutionTest::ModuleName,
		CompilerPipelineDelegateExecutionTest::ScriptFilename,
		ScriptSource,
		true,
		Summary);

	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should compile successfully"),
		bCompiled);
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should compile through the preprocessor path"),
		Summary.bUsedPreprocessor);
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should report compile success"),
		Summary.bCompileSucceeded);
	bPassed &= TestEqual(
		TEXT("Delegate declarations with trailing block comments should finish with FullyHandled"),
		Summary.CompileResult,
		ECompileResult::FullyHandled);
	bPassed &= TestEqual(
		TEXT("Delegate declarations with trailing block comments should keep compile diagnostics empty"),
		Summary.Diagnostics.Num(),
		0);
	if (!bCompiled)
	{
		return false;
	}

	const TSharedPtr<FAngelscriptDelegateDesc> SingleDelegate = Engine.GetDelegate(SingleDelegateName);
	const TSharedPtr<FAngelscriptDelegateDesc> MultiDelegate = Engine.GetDelegate(MultiDelegateName);
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should register the single-cast delegate"),
		SingleDelegate.IsValid());
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should register the multicast event delegate"),
		MultiDelegate.IsValid());
	if (SingleDelegate.IsValid())
	{
		bPassed &= TestFalse(
			TEXT("Delegate declarations with trailing block comments should preserve the single-cast flag"),
			SingleDelegate->bIsMulticast);
	}
	if (MultiDelegate.IsValid())
	{
		bPassed &= TestTrue(
			TEXT("Delegate declarations with trailing block comments should preserve the multicast flag"),
			MultiDelegate->bIsMulticast);
	}

	UClass* GeneratedClass = FindGeneratedClass(&Engine, GeneratedClassName);
	if (!TestNotNull(
			TEXT("Delegate declarations with trailing block comments should publish the generated carrier class"),
			GeneratedClass))
	{
		return false;
	}

	UFunction* RunSingleFunction = FindGeneratedFunction(GeneratedClass, RunSingleFunctionName);
	UFunction* RunMultiFunction = FindGeneratedFunction(GeneratedClass, RunMultiFunctionName);
	if (!TestNotNull(
			TEXT("Delegate declarations with trailing block comments should generate the single-cast execution function"),
			RunSingleFunction)
		|| !TestNotNull(
			TEXT("Delegate declarations with trailing block comments should generate the multicast execution function"),
			RunMultiFunction))
	{
		return false;
	}

	UObject* RuntimeObject = NewObject<UObject>(GetTransientPackage(), GeneratedClass, TEXT("CompilerDelegateTrailingCommentCarrier"));
	if (!TestNotNull(
			TEXT("Delegate declarations with trailing block comments should instantiate the generated carrier object"),
			RuntimeObject))
	{
		return false;
	}

	int32 SingleResult = INDEX_NONE;
	int32 MultiResult = INDEX_NONE;
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should execute the single-cast path on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, RuntimeObject, RunSingleFunction, SingleResult));
	bPassed &= TestTrue(
		TEXT("Delegate declarations with trailing block comments should execute the multicast path on the game thread"),
		ExecuteGeneratedIntEventOnGameThread(&Engine, RuntimeObject, RunMultiFunction, MultiResult));

	bPassed &= TestEqual(
		TEXT("Delegate declarations with trailing block comments should let the bound single-cast delegate write back the expected score"),
		SingleResult,
		17);
	bPassed &= TestEqual(
		TEXT("Delegate declarations with trailing block comments should let the bound multicast delegate write back the expected score"),
		MultiResult,
		29);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
