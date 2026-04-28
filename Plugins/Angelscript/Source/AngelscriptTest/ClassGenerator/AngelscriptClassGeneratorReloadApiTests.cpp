#include "Shared/AngelscriptTestEngineHelper.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Shared/AngelscriptTestUtilities.h"

#include "ClassGenerator/AngelscriptClassGenerator.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Preprocessor/AngelscriptPreprocessor.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_builder.h"
#include "source/as_module.h"
#include "source/as_scriptengine.h"
#include "EndAngelscriptHeaders.h"

// Test Layer: Runtime Integration
#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace ClassGeneratorReloadApiTest
{
	static const FName ModuleName(TEXT("GeneratorReloadApiSoft"));
	static const FString ScriptFilename(TEXT("GeneratorReloadApiSoft.as"));
	static const FName GeneratedClassName(TEXT("UGeneratorSoftReloadTarget"));

	struct FPreparedAnnotatedModule
	{
		FString AbsoluteFilename;
		TSharedPtr<FAngelscriptModuleDesc> Module;
	};

	static FString GetInitialCompileAbsoluteFilename()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), ScriptFilename);
	}

	static FString BuildScriptV1()
	{
		return TEXT(R"AS(
UCLASS()
class UGeneratorSoftReloadTarget : UObject
{
	UFUNCTION()
	int GetValue()
	{
		return 1;
	}
}
)AS");
	}

	static FString BuildScriptV2()
	{
		return TEXT(R"AS(
UCLASS()
class UGeneratorSoftReloadTarget : UObject
{
	UFUNCTION()
	int GetValue()
	{
		return 2;
	}
}
)AS");
	}

	static bool ExecuteGetValue(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UClass* Class,
		const int32 ExpectedValue,
		const TCHAR* Context)
	{
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should expose the generated class"), Context), Class))
		{
			return false;
		}

		UFunction* GetValueFunction = FindGeneratedFunction(Class, TEXT("GetValue"));
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should expose GetValue"), Context), GetValueFunction))
		{
			return false;
		}

		UObject* RuntimeObject = NewObject<UObject>(GetTransientPackage(), Class);
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should instantiate the generated class"), Context), RuntimeObject))
		{
			return false;
		}

		int32 Result = 0;
		if (!Test.TestTrue(
			*FString::Printf(TEXT("%s should execute GetValue on the game thread"), Context),
			ExecuteGeneratedIntEventOnGameThread(&Engine, RuntimeObject, GetValueFunction, Result)))
		{
			return false;
		}

		return Test.TestEqual(
			*FString::Printf(TEXT("%s should return the expected value"), Context),
			Result,
			ExpectedValue);
	}

	static bool PrepareAnnotatedModuleForGenerator(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ScriptSource,
		FPreparedAnnotatedModule& OutPreparedModule)
	{
		const FString AutomationDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"));
		const FString UniqueSuffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		OutPreparedModule.AbsoluteFilename = FPaths::Combine(
			AutomationDirectory,
			FString::Printf(TEXT("%s_%s.as"), *ModuleName.ToString(), *UniqueSuffix));

		IFileManager::Get().MakeDirectory(*AutomationDirectory, true);
		if (!FFileHelper::SaveStringToFile(ScriptSource, *OutPreparedModule.AbsoluteFilename))
		{
			Test.AddError(FString::Printf(
				TEXT("ClassGenerator reload-api test failed to write prepared fixture script to '%s'."),
				*OutPreparedModule.AbsoluteFilename));
			return false;
		}

		FAngelscriptPreprocessor Preprocessor;
		Preprocessor.AddFile(ScriptFilename, OutPreparedModule.AbsoluteFilename);
		if (!Preprocessor.Preprocess())
		{
			ReportCompileDiagnostics(Test, Engine, OutPreparedModule.AbsoluteFilename);
			Test.AddError(TEXT("ClassGenerator reload-api test failed to preprocess the prepared annotated module."));
			return false;
		}

		const TArray<TSharedRef<FAngelscriptModuleDesc>> Modules = Preprocessor.GetModulesToCompile();
		if (!Test.TestEqual(
			TEXT("ClassGenerator reload-api test should preprocess exactly one module descriptor"),
			Modules.Num(),
			1))
		{
			return false;
		}

		OutPreparedModule.Module = Modules[0];
		if (!Test.TestEqual(
			TEXT("Prepared reload-api module should contain exactly one class descriptor"),
			OutPreparedModule.Module->Classes.Num(),
			1))
		{
			return false;
		}

		return Test.TestEqual(
			TEXT("Prepared reload-api module should describe the expected generated class"),
			OutPreparedModule.Module->Classes[0]->ClassName,
			GeneratedClassName.ToString());
	}

	static bool CompilePreparedAnnotatedModuleForGenerator(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const ECompileType CompileType,
		const FPreparedAnnotatedModule& PreparedModule)
	{
		if (!PreparedModule.Module.IsValid())
		{
			Test.AddError(TEXT("Prepared reload-api module was null before compilation."));
			return false;
		}

		TSharedRef<FAngelscriptModuleDesc> Module = PreparedModule.Module.ToSharedRef();
		FMemMark MemoryMark(FMemStack::Get());
		FAngelscriptEngineScope EngineScope(Engine);
		TGuardValue<bool> AutomaticImportGuard(Engine.bUseAutomaticImportMethod, false);
		FScopedAutomaticImportsOverride AutomaticImportsOverride(Engine.GetScriptEngine());

		asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
		if (!Test.TestNotNull(TEXT("Prepared reload-api compilation should have a script engine"), ScriptEngine))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			ScriptEngine->BuildCompleted();
		};

		Engine.Diagnostics.Empty();
		Engine.LastEmittedDiagnostics.Empty();
		Engine.bDiagnosticsDirty = false;

		for (const FAngelscriptModuleDesc::FCodeSection& Section : Module->Code)
		{
			FAngelscriptEngine::FDiagnostics& Diagnostics = Engine.Diagnostics.FindOrAdd(Section.AbsoluteFilename);
			Diagnostics.Diagnostics.Reset();
			Diagnostics.Filename = Section.AbsoluteFilename;
			Diagnostics.bIsCompiling = true;
		}

		ScriptEngine->RequestBuild();
		ScriptEngine->PrepareEngine();

		if (CompileType != ECompileType::Initial)
		{
			const TSharedPtr<FAngelscriptModuleDesc> OldModule = Engine.GetModuleByModuleName(Module->ModuleName);
			if (OldModule.IsValid() && OldModule->ScriptModule != nullptr)
			{
				static_cast<asCModule*>(OldModule->ScriptModule)->RemoveTypesAndGlobalsFromEngineAvailability();
			}
		}

		const TArray<TSharedRef<FAngelscriptModuleDesc>> ImportedModules;
		Engine.CompileModule_Types_Stage1(CompileType, Module, ImportedModules);

		asCModule* ScriptModule = static_cast<asCModule*>(Module->ScriptModule);
		if (Module->bCompileError || !Test.TestNotNull(TEXT("Prepared reload-api module should create a backing script module"), ScriptModule))
		{
			ReportCompileDiagnostics(Test, Engine, PreparedModule.AbsoluteFilename);
			return false;
		}

		if (!Module->bLoadedPrecompiledCode)
		{
			if (ScriptModule->builder->BuildParallelParseScripts() != asSUCCESS)
			{
				Module->bCompileError = true;
			}

			if (!Module->bCompileError && ScriptModule->builder->BuildGenerateTypes() != asSUCCESS)
			{
				Module->bCompileError = true;
			}
		}

		Engine.CompileModule_Functions_Stage2(CompileType, Module);
		if (!Module->bLoadedPrecompiledCode && !Module->bCompileError)
		{
			if (ScriptModule->builder->BuildLayoutClasses() != asSUCCESS)
			{
				Module->bCompileError = true;
			}

			if (!Module->bCompileError && ScriptModule->builder->BuildLayoutFunctions() != asSUCCESS)
			{
				Module->bCompileError = true;
			}
		}

		if (Module->bCompileError)
		{
			ReportCompileDiagnostics(Test, Engine, PreparedModule.AbsoluteFilename);
			return false;
		}

		Engine.CompileModule_Code_Stage3(CompileType, Module);
		if (Module->bCompileError)
		{
			ReportCompileDiagnostics(Test, Engine, PreparedModule.AbsoluteFilename);
			return false;
		}

		Engine.CompileModule_Globals_Stage4(CompileType, Module);
		return true;
	}
}

using namespace ClassGeneratorReloadApiTest;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptClassGeneratorPerformSoftReloadAppliesPreparedBodyOnlyModuleTest,
	"Angelscript.TestModule.ClassGenerator.Generator.PerformSoftReloadAppliesPreparedBodyOnlyModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptClassGeneratorPerformSoftReloadAppliesPreparedBodyOnlyModuleTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	FPreparedAnnotatedModule PreparedModule;
	TSharedPtr<FAngelscriptModuleDesc> ActiveModule;
	ON_SCOPE_EXIT
	{
		if (ActiveModule.IsValid())
		{
			Engine.DiscardModule(*ActiveModule->ModuleName);
		}

		IFileManager::Get().Delete(*GetInitialCompileAbsoluteFilename(), false, true, true);
		if (!PreparedModule.AbsoluteFilename.IsEmpty())
		{
			IFileManager::Get().Delete(*PreparedModule.AbsoluteFilename, false, true, true);
		}

		ResetSharedCloneEngine(Engine);
	};

	if (!TestTrue(
		TEXT("Direct PerformSoftReload test should compile the initial annotated module"),
		CompileAnnotatedModuleFromMemory(&Engine, ModuleName, ScriptFilename, BuildScriptV1())))
	{
		return false;
	}

	ActiveModule = Engine.GetModuleByFilenameOrModuleName(GetInitialCompileAbsoluteFilename(), ModuleName.ToString());
	if (!TestNotNull(TEXT("Initial direct PerformSoftReload fixture should register an active module record"), ActiveModule.Get()))
	{
		return false;
	}

	UClass* OldClass = FindGeneratedClass(&Engine, GeneratedClassName);
	if (!ExecuteGetValue(*this, Engine, OldClass, 1, TEXT("Initial direct PerformSoftReload baseline")))
	{
		return false;
	}

	if (!PrepareAnnotatedModuleForGenerator(*this, Engine, BuildScriptV2(), PreparedModule))
	{
		return false;
	}

	if (!TestEqual(
		TEXT("Prepared reload-api module should target the same module name as the active baseline module"),
		PreparedModule.Module->ModuleName,
		ActiveModule->ModuleName))
	{
		return false;
	}

	if (!CompilePreparedAnnotatedModuleForGenerator(*this, Engine, ECompileType::SoftReloadOnly, PreparedModule))
	{
		return false;
	}

	FAngelscriptClassGenerator Generator;
	Generator.AddModule(PreparedModule.Module.ToSharedRef());

	const FAngelscriptClassGenerator::EReloadRequirement ReloadRequirement = Generator.Setup();
	if (!TestEqual(
		TEXT("Prepared body-only module should stay on the soft reload path"),
		ReloadRequirement,
		FAngelscriptClassGenerator::EReloadRequirement::SoftReload))
	{
		return false;
	}

	if (!TestFalse(
		TEXT("Prepared body-only module should not suggest a full reload"),
		Generator.WantsFullReload(PreparedModule.Module.ToSharedRef())))
	{
		return false;
	}

	if (!TestFalse(
		TEXT("Prepared body-only module should not require a full reload"),
		Generator.NeedsFullReload(PreparedModule.Module.ToSharedRef())))
	{
		return false;
	}

	Generator.PerformSoftReload();

	UClass* ReloadedClass = FindGeneratedClass(&Engine, GeneratedClassName);
	if (!TestEqual(
		TEXT("Direct PerformSoftReload should keep the same generated class object"),
		ReloadedClass,
		OldClass))
	{
		return false;
	}

	if (!ExecuteGetValue(*this, Engine, ReloadedClass, 2, TEXT("Direct PerformSoftReload body-only update")))
	{
		return false;
	}

	ASTEST_END_SHARE_FRESH
	return true;
}

#endif
