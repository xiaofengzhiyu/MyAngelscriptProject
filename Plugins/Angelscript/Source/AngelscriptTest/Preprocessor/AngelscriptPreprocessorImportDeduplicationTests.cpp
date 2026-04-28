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

namespace AngelscriptTest_Preprocessor_AngelscriptPreprocessorImportDeduplicationTests_Private
{
	FString GetFixtureRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("PreprocessorImportDeduplicationFixtures"));
	}

	FString WriteFixture(const FString& RelativeScriptPath, const FString& Contents)
	{
		const FString AbsolutePath = FPaths::Combine(GetFixtureRoot(), RelativeScriptPath);
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(AbsolutePath), true);
		FFileHelper::SaveStringToFile(Contents, *AbsolutePath);
		return AbsolutePath;
	}

	TArray<FString> CollectDiagnosticMessages(
		const FAngelscriptEngine& Engine,
		const TArray<FString>& AbsoluteFilenames,
		int32& OutErrorCount)
	{
		TArray<FString> Messages;
		OutErrorCount = 0;

		for (const FString& AbsoluteFilename : AbsoluteFilenames)
		{
			const FAngelscriptEngine::FDiagnostics* Diagnostics = Engine.Diagnostics.Find(AbsoluteFilename);
			if (Diagnostics == nullptr)
			{
				continue;
			}

			for (const FAngelscriptEngine::FDiagnostic& Diagnostic : Diagnostics->Diagnostics)
			{
				Messages.Add(Diagnostic.Message);
				if (Diagnostic.bIsError)
				{
					++OutErrorCount;
				}
			}
		}

		return Messages;
	}

	const FAngelscriptModuleDesc* FindModuleByName(
		const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules,
		const FString& ModuleName)
	{
		for (const TSharedRef<FAngelscriptModuleDesc>& Module : Modules)
		{
			if (Module->ModuleName == ModuleName)
			{
				return &Module.Get();
			}
		}

		return nullptr;
	}
}

using namespace AngelscriptTest_Preprocessor_AngelscriptPreprocessorImportDeduplicationTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPreprocessorDuplicateImportStatementsTest,
	"Angelscript.TestModule.Preprocessor.Import.DuplicateStatementsDeduplicateDependency",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPreprocessorDuplicateImportStatementsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	Engine.ResetDiagnostics();

	const FString SharedRelativePath = TEXT("Tests/Preprocessor/ImportDuplicateStatements/Shared.as");
	const FString SharedAbsolutePath = WriteFixture(
		SharedRelativePath,
		TEXT("int SharedValue()\n")
		TEXT("{\n")
		TEXT("    return 73;\n")
		TEXT("}\n"));

	const FString ConsumerRelativePath = TEXT("Tests/Preprocessor/ImportDuplicateStatements/UsesShared.as");
	const FString ConsumerAbsolutePath = WriteFixture(
		ConsumerRelativePath,
		TEXT("import Tests.Preprocessor.ImportDuplicateStatements.Shared;\n")
		TEXT("import Tests.Preprocessor.ImportDuplicateStatements.Shared;\n")
		TEXT("int Entry()\n")
		TEXT("{\n")
		TEXT("    return SharedValue();\n")
		TEXT("}\n"));

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("Tests.Preprocessor.ImportDuplicateStatements.UsesShared"));
		Engine.DiscardModule(TEXT("Tests.Preprocessor.ImportDuplicateStatements.Shared"));
		IFileManager::Get().Delete(*ConsumerAbsolutePath, false, true);
		IFileManager::Get().Delete(*SharedAbsolutePath, false, true);
	};

	TGuardValue<bool> AutomaticImportGuard(Engine.bUseAutomaticImportMethod, false);
	FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());

	FAngelscriptPreprocessor Preprocessor;
	Preprocessor.AddFile(SharedRelativePath, SharedAbsolutePath);
	Preprocessor.AddFile(ConsumerRelativePath, ConsumerAbsolutePath);

	const bool bPreprocessSucceeded = Preprocessor.Preprocess();
	const TArray<TSharedRef<FAngelscriptModuleDesc>> Modules = Preprocessor.GetModulesToCompile();

	int32 PreprocessErrorCount = 0;
	const TArray<FString> PreprocessMessages = CollectDiagnosticMessages(
		Engine,
		{SharedAbsolutePath, ConsumerAbsolutePath},
		PreprocessErrorCount);
	const FString PreprocessDiagnostics = FString::Join(PreprocessMessages, TEXT("\n"));

	bPassed &= TestTrue(
		TEXT("Duplicate import statements should preprocess successfully"),
		bPreprocessSucceeded);
	bPassed &= TestEqual(
		TEXT("Duplicate import statements should not emit preprocessing errors"),
		PreprocessErrorCount,
		0);
	bPassed &= TestTrue(
		TEXT("Duplicate import statements should not accumulate preprocessing diagnostics"),
		PreprocessDiagnostics.IsEmpty());
	bPassed &= TestEqual(
		TEXT("Duplicate import statements should still emit exactly two module descriptors"),
		Modules.Num(),
		2);
	if (!bPreprocessSucceeded || Modules.Num() != 2)
	{
		return false;
	}

	const FAngelscriptModuleDesc* SharedModule = FindModuleByName(
		Modules,
		TEXT("Tests.Preprocessor.ImportDuplicateStatements.Shared"));
	const FAngelscriptModuleDesc* ConsumerModule = FindModuleByName(
		Modules,
		TEXT("Tests.Preprocessor.ImportDuplicateStatements.UsesShared"));
	if (!TestNotNull(
			TEXT("Duplicate import statements should emit the shared provider module descriptor"),
			SharedModule)
		|| !TestNotNull(
			TEXT("Duplicate import statements should emit the importing consumer module descriptor"),
			ConsumerModule))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Provider module should not gain synthetic imports when only the consumer imports it"),
		SharedModule->ImportedModules.Num(),
		0);
	bPassed &= TestEqual(
		TEXT("Consumer module should record the duplicated provider import only once"),
		ConsumerModule->ImportedModules.Num(),
		1);
	bPassed &= TestTrue(
		TEXT("Consumer module should preserve the normalized provider module name in ImportedModules"),
		ConsumerModule->ImportedModules.Contains(TEXT("Tests.Preprocessor.ImportDuplicateStatements.Shared")));
	bPassed &= TestTrue(
		TEXT("Consumer module should materialize at least one code section after stripping duplicate imports"),
		ConsumerModule->Code.Num() > 0);
	if (ConsumerModule->Code.Num() == 0)
	{
		return false;
	}

	const FString& ConsumerCode = ConsumerModule->Code[0].Code;
	bPassed &= TestFalse(
		TEXT("Consumer module should not preserve the duplicated raw import statement in processed code"),
		ConsumerCode.Contains(TEXT("import Tests.Preprocessor.ImportDuplicateStatements.Shared;")));
	bPassed &= TestTrue(
		TEXT("Consumer module should preserve the callable entry function after stripping duplicate imports"),
		ConsumerCode.Contains(TEXT("int Entry()")));

	Engine.ResetDiagnostics();

	TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
	const ECompileResult CompileResult = Engine.CompileModules(
		ECompileType::SoftReloadOnly,
		Modules,
		CompiledModules);

	int32 CompileErrorCount = 0;
	const TArray<FString> CompileMessages = CollectDiagnosticMessages(
		Engine,
		{SharedAbsolutePath, ConsumerAbsolutePath},
		CompileErrorCount);
	const FString CompileDiagnostics = FString::Join(CompileMessages, TEXT("\n"));

	bPassed &= TestEqual(
		TEXT("Duplicate import statements should compile as FullyHandled"),
		CompileResult,
		ECompileResult::FullyHandled);
	bPassed &= TestEqual(
		TEXT("Duplicate import statements should keep compile diagnostics empty"),
		CompileErrorCount,
		0);
	bPassed &= TestTrue(
		TEXT("Duplicate import statements should not accumulate compile messages"),
		CompileDiagnostics.IsEmpty());
	bPassed &= TestEqual(
		TEXT("Duplicate import statements should compile exactly two module descriptors"),
		CompiledModules.Num(),
		2);
	if (CompileResult != ECompileResult::FullyHandled || CompiledModules.Num() != 2)
	{
		return false;
	}

	TSharedPtr<FAngelscriptModuleDesc> CompiledConsumerModule = Engine.GetModule(TEXT("Tests.Preprocessor.ImportDuplicateStatements.UsesShared"));
	if (!TestTrue(
		TEXT("Duplicate import statements should register the consumer module on the engine after compile"),
		CompiledConsumerModule.IsValid()))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Compiled consumer module should still expose exactly one imported dependency"),
		CompiledConsumerModule->ImportedModules.Num(),
		1);
	if (!TestNotNull(
		TEXT("Duplicate import statements should expose a backing script module for the consumer"),
		CompiledConsumerModule->ScriptModule))
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *CompiledConsumerModule->ScriptModule, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Duplicate import statements should still execute the imported provider function after compile"),
		Result,
		73);

	ASTEST_END_SHARE_CLEAN

	return bPassed;
}

#endif
