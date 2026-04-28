#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

// Test Layer: Runtime Integration
#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace ClassGeneratorDecisionTest
{
	static const FName RequestedModuleName(TEXT("SetupReportsStructuralReloadLines"));
	static const FString RelativeFilename(TEXT("ClassGenerator/SetupReportsStructuralReloadLines.as"));
	static const TCHAR* FullReloadRequiredMessage =
		TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot perform a full reload right now. Keeping old angelscript code active.");

	static FString BuildBaselineScript()
	{
		return TEXT(R"AS(
UCLASS()
class UDecisionLineTarget : UObject
{
	UPROPERTY()
	int Value;

	UFUNCTION()
	int GetValue(int Delta)
	{
		return Value + Delta;
	}

	UFUNCTION()
	int GetBodyOnlyValue()
	{
		return 11;
	}
}
)AS");
	}

	static FString BuildStructuralReloadScript()
	{
		return TEXT(R"AS(
UCLASS()
class UDecisionLineTarget : UObject
{
	UPROPERTY()
	int Value;

	UPROPERTY()
	int ExtraValue;

	UFUNCTION()
	int GetValue(int Delta, int Bonus)
	{
		return Value + Delta + Bonus;
	}

	UFUNCTION()
	int GetBodyOnlyValue()
	{
		return 17;
	}
}
)AS");
	}

	static int32 FindLineNumberContainingOccurrence(const FString& Source, const FString& Needle, const int32 Occurrence = 1)
	{
		if (Occurrence <= 0)
		{
			return INDEX_NONE;
		}

		int32 SearchStart = 0;
		int32 FoundCount = 0;
		while (SearchStart < Source.Len())
		{
			const int32 MatchIndex = Source.Find(Needle, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
			if (MatchIndex == INDEX_NONE)
			{
				return INDEX_NONE;
			}

			++FoundCount;
			if (FoundCount == Occurrence)
			{
				int32 LineNumber = 1;
				for (int32 Index = 0; Index < MatchIndex; ++Index)
				{
					if (Source[Index] == '\n')
					{
						++LineNumber;
					}
				}
				return LineNumber;
			}

			SearchStart = MatchIndex + Needle.Len();
		}

		return INDEX_NONE;
	}

	static TArray<int32> CollectDiagnosticRowsContaining(
		const FAngelscriptEngine& Engine,
		const FString& Section,
		const FString& MessageFragment)
	{
		TArray<int32> Rows;
		const FAngelscriptEngine::FDiagnostics* Diagnostics = Engine.Diagnostics.Find(Section);
		if (Diagnostics == nullptr)
		{
			return Rows;
		}

		for (const FAngelscriptEngine::FDiagnostic& Diagnostic : Diagnostics->Diagnostics)
		{
			if (Diagnostic.bIsError && Diagnostic.Message.Contains(MessageFragment))
			{
				Rows.AddUnique(Diagnostic.Row);
			}
		}

		return Rows;
	}
}

using namespace ClassGeneratorDecisionTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptClassGeneratorSetupReportsStructuralReloadLinesTest,
	"Angelscript.TestModule.ClassGenerator.SetupReportsStructuralReloadLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptClassGeneratorSetupReportsStructuralReloadLinesTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	Engine.ResetDiagnostics();
	Engine.LastEmittedDiagnostics.Empty();
	Engine.bDiagnosticsDirty = false;

	const FString BaselineScript = ClassGeneratorDecisionTest::BuildBaselineScript();
	const FString StructuralReloadScript = ClassGeneratorDecisionTest::BuildStructuralReloadScript();
	const int32 ExpectedAddedPropertyLine =
		ClassGeneratorDecisionTest::FindLineNumberContainingOccurrence(StructuralReloadScript, TEXT("UPROPERTY()"), 2);
	const int32 ExpectedChangedSignatureLine =
		ClassGeneratorDecisionTest::FindLineNumberContainingOccurrence(StructuralReloadScript, TEXT("int GetValue(int Delta, int Bonus)"));
	const int32 UnexpectedBodyOnlyFunctionLine =
		ClassGeneratorDecisionTest::FindLineNumberContainingOccurrence(StructuralReloadScript, TEXT("int GetBodyOnlyValue()"));
	const FString ResolvedAutomationFilename =
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), ClassGeneratorDecisionTest::RelativeFilename);

	ON_SCOPE_EXIT
	{
		ResetSharedCloneEngine(Engine);
	};

	if (!TestTrue(
		TEXT("ClassGenerator decision test should resolve the added property line"),
		ExpectedAddedPropertyLine != INDEX_NONE))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("ClassGenerator decision test should resolve the changed signature line"),
		ExpectedChangedSignatureLine != INDEX_NONE))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("ClassGenerator decision test should resolve the body-only function line"),
		UnexpectedBodyOnlyFunctionLine != INDEX_NONE))
	{
		return false;
	}

	if (!TestTrue(
		TEXT("ClassGenerator decision test should compile the baseline annotated module"),
		CompileAnnotatedModuleFromMemory(
			&Engine,
			ClassGeneratorDecisionTest::RequestedModuleName,
			ClassGeneratorDecisionTest::RelativeFilename,
			BaselineScript)))
	{
		return false;
	}

	Engine.ResetDiagnostics();
	Engine.LastEmittedDiagnostics.Empty();
	Engine.bDiagnosticsDirty = false;

	FAngelscriptClassGenerator::EReloadRequirement ReloadRequirement = FAngelscriptClassGenerator::Error;
	bool bWantsFullReload = false;
	bool bNeedsFullReload = false;
	const bool bAnalyzed = AnalyzeReloadFromMemory(
		&Engine,
		ClassGeneratorDecisionTest::RequestedModuleName,
		ClassGeneratorDecisionTest::RelativeFilename,
		StructuralReloadScript,
		ReloadRequirement,
		bWantsFullReload,
		bNeedsFullReload);
	if (!TestTrue(
		TEXT("Structural reload decision fixture should analyze successfully"),
		bAnalyzed))
	{
		return false;
	}

	const TArray<int32> ReloadDiagnosticRows = ClassGeneratorDecisionTest::CollectDiagnosticRowsContaining(
		Engine,
		ResolvedAutomationFilename,
		ClassGeneratorDecisionTest::FullReloadRequiredMessage);
	const bool bHasAddedPropertyLine = ReloadDiagnosticRows.Contains(ExpectedAddedPropertyLine);
	const bool bHasChangedSignatureLine = ReloadDiagnosticRows.Contains(ExpectedChangedSignatureLine);
	const bool bHasBodyOnlyFunctionLine = ReloadDiagnosticRows.Contains(UnexpectedBodyOnlyFunctionLine);

	bPassed &= TestEqual(
		TEXT("Generator setup should classify the structural reload fixture as full reload required"),
		ReloadRequirement,
		FAngelscriptClassGenerator::FullReloadRequired);
	bPassed &= TestTrue(
		TEXT("Reload analysis should suggest a full reload for the structural reload fixture"),
		bWantsFullReload);
	bPassed &= TestTrue(
		TEXT("Reload analysis should require a full reload for the structural reload fixture"),
		bNeedsFullReload);
	bPassed &= TestTrue(
		TEXT("SoftReloadOnly diagnostics should expose at least one structural reload line"),
		ReloadDiagnosticRows.Num() > 0);
	bPassed &= TestTrue(
		TEXT("SoftReloadOnly diagnostics should report the newly added UPROPERTY line"),
		bHasAddedPropertyLine);
	bPassed &= TestTrue(
		TEXT("SoftReloadOnly diagnostics should report the changed UFUNCTION signature line"),
		bHasChangedSignatureLine);
	bPassed &= TestFalse(
		TEXT("SoftReloadOnly diagnostics should not report a body-only function change"),
		bHasBodyOnlyFunctionLine);

	ASTEST_END_SHARE_FRESH
	return bPassed;
}

#endif
