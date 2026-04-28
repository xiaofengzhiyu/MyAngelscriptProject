#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Preprocessor/AngelscriptPreprocessor.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Preprocessor_AngelscriptPreprocessorEditorOnlyBlockTests_Private
{
	TUniquePtr<FAngelscriptEngine> CreateEditorOnlyDirectiveEngine()
	{
		FAngelscriptEngineConfig Config;
		Config.bIsEditor = true;

		FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		return FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
	}

	FString GetEditorOnlyDirectiveFixtureRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PreprocessorEditorOnlyDirectiveFixtures"));
	}

	FString WriteEditorOnlyDirectiveFixture(const FString& RelativeScriptPath, const FString& Contents)
	{
		const FString AbsolutePath = FPaths::Combine(GetEditorOnlyDirectiveFixtureRoot(), RelativeScriptPath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), true);
		FFileHelper::SaveStringToFile(Contents, *AbsolutePath);
		return AbsolutePath;
	}

	int32 CountErrorDiagnostics(const FAngelscriptEngine& Engine, const FString& AbsoluteFilename)
	{
		const FAngelscriptEngine::FDiagnostics* Diagnostics = Engine.Diagnostics.Find(AbsoluteFilename);
		if (Diagnostics == nullptr)
		{
			return 0;
		}

		int32 ErrorCount = 0;
		for (const FAngelscriptEngine::FDiagnostic& Diagnostic : Diagnostics->Diagnostics)
		{
			if (Diagnostic.bIsError)
			{
				++ErrorCount;
			}
		}

		return ErrorCount;
	}
}

using namespace AngelscriptTest_Preprocessor_AngelscriptPreprocessorEditorOnlyBlockTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPreprocessorEditorOnlyDataBlockLinesRecordedTest,
	"Angelscript.TestModule.Preprocessor.Directives.EditorOnlyDataBlockLinesRecorded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPreprocessorEditorOnlyDataBlockLinesRecordedTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateEditorOnlyDirectiveEngine();
	if (!TestNotNull(TEXT("EDITORONLY_DATA block-line test should create an editor-configured engine"), OwnedEngine.Get()))
	{
		return false;
	}

	FAngelscriptEngine& Engine = *OwnedEngine;
	ASTEST_BEGIN_FULL

	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should run with editor scripts enabled in the active engine context"),
		FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext());

	static const FName ModuleName(TEXT("Tests.Preprocessor.Directives.EditorOnlyDataBlockLinesRecorded"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
	};

	const FString RelativeScriptPath = TEXT("Tests/Preprocessor/Directives/EditorOnlyDataBlockLinesRecorded.as");
	const FString ScriptSource = TEXT(
		"#if EDITORONLY_DATA\n"
		"int EditorOnlyHelper()\n"
		"{\n"
		"    return 3;\n"
		"}\n"
		"#endif\n"
		"int Entry()\n"
		"{\n"
		"    return 7;\n"
		"}\n"
		"#if EDITORONLY_DATA\n"
		"int TailEditorOnlyHelper()\n"
		"{\n"
		"    return 11;\n"
		"}\n"
		"#endif\n");
	const FString AbsoluteScriptPath = WriteEditorOnlyDirectiveFixture(RelativeScriptPath, ScriptSource);

	Engine.ResetDiagnostics();

	FAngelscriptPreprocessor Preprocessor;
	Preprocessor.AddFile(RelativeScriptPath, AbsoluteScriptPath);

	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should expose the current context flag to the preprocessor"),
		Preprocessor.PreprocessorFlags.FindRef(TEXT("EDITORONLY_DATA")));

	const bool bPreprocessSucceeded = Preprocessor.Preprocess();
	const TArray<TSharedRef<FAngelscriptModuleDesc>> Modules = Preprocessor.GetModulesToCompile();

	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should preprocess successfully"),
		bPreprocessSucceeded);
	bPassed &= TestEqual(
		TEXT("EDITORONLY_DATA block-line test should not emit preprocessing errors"),
		CountErrorDiagnostics(Engine, AbsoluteScriptPath),
		0);
	bPassed &= TestEqual(
		TEXT("EDITORONLY_DATA block-line test should keep exactly one module descriptor"),
		Modules.Num(),
		1);

	const FAngelscriptModuleDesc* Module = Modules.Num() > 0 ? &Modules[0].Get() : nullptr;
	bPassed &= TestNotNull(
		TEXT("EDITORONLY_DATA block-line test should still produce a preprocessed module descriptor"),
		Module);

	if (Module != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("EDITORONLY_DATA block-line test should preserve the normalized module name"),
			Module->ModuleName,
			ModuleName.ToString());
		bPassed &= TestEqual(
			TEXT("EDITORONLY_DATA block-line test should record both editor-only regions"),
			Module->EditorOnlyBlockLines.Num(),
			2);

		if (Module->EditorOnlyBlockLines.Num() == 2)
		{
			bPassed &= TestEqual(
				TEXT("EDITORONLY_DATA block-line test should mark the first block start line"),
				Module->EditorOnlyBlockLines[0].Key,
				1);
			bPassed &= TestEqual(
				TEXT("EDITORONLY_DATA block-line test should mark the first block end line"),
				Module->EditorOnlyBlockLines[0].Value,
				6);
			bPassed &= TestEqual(
				TEXT("EDITORONLY_DATA block-line test should mark the second block start line"),
				Module->EditorOnlyBlockLines[1].Key,
				11);
			bPassed &= TestEqual(
				TEXT("EDITORONLY_DATA block-line test should mark the second block end line"),
				Module->EditorOnlyBlockLines[1].Value,
				16);
		}

		const FString ProcessedCode = Module->Code.Num() > 0 ? Module->Code[0].Code : FString();
		bPassed &= TestTrue(
			TEXT("EDITORONLY_DATA block-line test should keep the active Entry body in processed code"),
			ProcessedCode.Contains(TEXT("return 7;")));
		bPassed &= TestFalse(
			TEXT("EDITORONLY_DATA block-line test should strip raw #if text from processed code"),
			ProcessedCode.Contains(TEXT("#if")));
	}

	Engine.ResetDiagnostics();

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		RelativeScriptPath,
		ScriptSource,
		true,
		Summary,
		true);

	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should compile through the preprocessor pipeline"),
		bCompiled);
	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should report preprocessor usage in compile summary"),
		Summary.bUsedPreprocessor);
	bPassed &= TestEqual(
		TEXT("EDITORONLY_DATA block-line test should keep compile diagnostics empty"),
		Summary.Diagnostics.Num(),
		0);
	bPassed &= TestEqual(
		TEXT("EDITORONLY_DATA block-line test should produce exactly one compiled module"),
		Summary.CompiledModuleCount,
		1);

	int32 EntryResult = 0;
	const bool bExecuted = bCompiled
		&& ExecuteIntFunction(&Engine, RelativeScriptPath, ModuleName, TEXT("int Entry()"), EntryResult);
	bPassed &= TestTrue(
		TEXT("EDITORONLY_DATA block-line test should execute the compiled Entry function"),
		bExecuted);
	if (bExecuted)
	{
		bPassed &= TestEqual(
			TEXT("EDITORONLY_DATA block-line test should preserve the non-editor-only Entry result"),
			EntryResult,
			7);
	}

	ASTEST_END_FULL
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPreprocessorEditorOnlyBlockLinesRecordedTest,
	"Angelscript.TestModule.Preprocessor.Directives.EditorOnlyBlockLinesRecorded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPreprocessorEditorOnlyBlockLinesRecordedTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateEditorOnlyDirectiveEngine();
	if (!TestNotNull(TEXT("EDITOR block-line test should create an editor-configured engine"), OwnedEngine.Get()))
	{
		return false;
	}

	FAngelscriptEngine& Engine = *OwnedEngine;
	ASTEST_BEGIN_FULL

	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should run with editor scripts enabled in the active engine context"),
		FAngelscriptEngine::ShouldUseEditorScriptsForCurrentContext());

	static const FName ModuleName(TEXT("Tests.Preprocessor.Directives.EditorOnlyBlockLinesRecorded"));
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
	};

	const FString RelativeScriptPath = TEXT("Tests/Preprocessor/Directives/EditorOnlyBlockLinesRecorded.as");
	const FString ScriptSource = TEXT(
		"#if EDITOR\n"
		"int EditorOnlyHelper() { return 3; }\n"
		"#endif\n"
		"int Entry()\n"
		"{ return 7; }\n"
		"#if EDITOR\n"
		"int TailEditorOnlyHelper() { return 11; }\n"
		"#endif\n");
	const FString AbsoluteScriptPath = WriteEditorOnlyDirectiveFixture(RelativeScriptPath, ScriptSource);

	Engine.ResetDiagnostics();

	FAngelscriptPreprocessor Preprocessor;
	Preprocessor.AddFile(RelativeScriptPath, AbsoluteScriptPath);

	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should expose the current context flag to the preprocessor"),
		Preprocessor.PreprocessorFlags.FindRef(TEXT("EDITOR")));

	const bool bPreprocessSucceeded = Preprocessor.Preprocess();
	const TArray<TSharedRef<FAngelscriptModuleDesc>> Modules = Preprocessor.GetModulesToCompile();

	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should preprocess successfully"),
		bPreprocessSucceeded);
	bPassed &= TestEqual(
		TEXT("EDITOR block-line test should not emit preprocessing errors"),
		CountErrorDiagnostics(Engine, AbsoluteScriptPath),
		0);
	bPassed &= TestEqual(
		TEXT("EDITOR block-line test should keep exactly one module descriptor"),
		Modules.Num(),
		1);

	const FAngelscriptModuleDesc* Module = Modules.Num() > 0 ? &Modules[0].Get() : nullptr;
	bPassed &= TestNotNull(
		TEXT("EDITOR block-line test should still produce a preprocessed module descriptor"),
		Module);

	if (Module != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("EDITOR block-line test should preserve the normalized module name"),
			Module->ModuleName,
			ModuleName.ToString());
		bPassed &= TestEqual(
			TEXT("EDITOR block-line test should record both editor-only regions"),
			Module->EditorOnlyBlockLines.Num(),
			2);

		if (Module->EditorOnlyBlockLines.Num() == 2)
		{
			bPassed &= TestEqual(
				TEXT("EDITOR block-line test should mark the first block start line"),
				Module->EditorOnlyBlockLines[0].Key,
				1);
			bPassed &= TestEqual(
				TEXT("EDITOR block-line test should mark the first block end line"),
				Module->EditorOnlyBlockLines[0].Value,
				3);
			bPassed &= TestEqual(
				TEXT("EDITOR block-line test should mark the second block start line"),
				Module->EditorOnlyBlockLines[1].Key,
				6);
			bPassed &= TestEqual(
				TEXT("EDITOR block-line test should mark the second block end line"),
				Module->EditorOnlyBlockLines[1].Value,
				8);
		}

		const FString ProcessedCode = Module->Code.Num() > 0 ? Module->Code[0].Code : FString();
		bPassed &= TestTrue(
			TEXT("EDITOR block-line test should keep the active Entry body in processed code"),
			ProcessedCode.Contains(TEXT("return 7;")));
		bPassed &= TestFalse(
			TEXT("EDITOR block-line test should strip raw #if text from processed code"),
			ProcessedCode.Contains(TEXT("#if")));
	}

	Engine.ResetDiagnostics();

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		RelativeScriptPath,
		ScriptSource,
		true,
		Summary,
		true);

	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should compile through the preprocessor pipeline"),
		bCompiled);
	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should report preprocessor usage in compile summary"),
		Summary.bUsedPreprocessor);
	bPassed &= TestEqual(
		TEXT("EDITOR block-line test should keep compile diagnostics empty"),
		Summary.Diagnostics.Num(),
		0);
	bPassed &= TestEqual(
		TEXT("EDITOR block-line test should produce exactly one compiled module"),
		Summary.CompiledModuleCount,
		1);

	int32 EntryResult = 0;
	const bool bExecuted = bCompiled
		&& ExecuteIntFunction(&Engine, RelativeScriptPath, ModuleName, TEXT("int Entry()"), EntryResult);
	bPassed &= TestTrue(
		TEXT("EDITOR block-line test should execute the compiled Entry function"),
		bExecuted);
	if (bExecuted)
	{
		bPassed &= TestEqual(
			TEXT("EDITOR block-line test should preserve the non-editor-only Entry result"),
			EntryResult,
			7);
	}

	ASTEST_END_FULL
	return bPassed;
}

#endif
