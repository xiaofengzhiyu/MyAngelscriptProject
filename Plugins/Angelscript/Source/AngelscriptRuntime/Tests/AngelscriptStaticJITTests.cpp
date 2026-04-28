#include "AngelscriptEngine.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "StaticJIT/AngelscriptStaticJIT.h"
#include "StaticJIT/PrecompiledData.h"
#include "StaticJIT/StaticJITConfig.h"
#include "StaticJIT/StaticJITHeader.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_module.h"
#include "source/as_scriptfunction.h"
#include "source/as_scriptengine.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS && AS_CAN_GENERATE_JIT

namespace AngelscriptStaticJITTests_Private
{
	static constexpr ANSICHAR BasicFunctionSource[] = "int DoubleValue(int Value) { return Value * 2; }";
	static constexpr ANSICHAR BasicFunctionDecl[] = "int DoubleValue(int)";
	static constexpr ANSICHAR BasicFunctionName[] = "DoubleValue";
	static constexpr uint32 RegisteredFunctionId = 0x10203040u;
	static constexpr uint64 FunctionLookupToken = 0x11111111ull;
	static constexpr uint64 SystemFunctionLookupToken = 0x22222222ull;
	static constexpr uint64 GlobalVarLookupToken = 0x33333333ull;
	static constexpr uint64 TypeLookupToken = 0x44444444ull;
	static constexpr uint64 PropertyLookupToken = 0x55555555ull;

	static FString MakeUniqueModuleName(const TCHAR* Prefix)
	{
		return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	static void StaticJITVMEntryStub(FScriptExecution& Execution, asDWORD* l_fp, asQWORD* l_outValue)
	{
		(void)Execution;
		(void)l_fp;
		(void)l_outValue;
	}

	static void StaticJITParmsEntryStub(FScriptExecution& Execution, void* Object, void* Parms)
	{
		(void)Execution;
		(void)Object;
		(void)Parms;
	}

	static void StaticJITRawStub()
	{
	}

	struct FScopedJITDatabaseSnapshot
	{
		FScopedJITDatabaseSnapshot()
		{
			Save(FJITDatabase::Get());
		}

		~FScopedJITDatabaseSnapshot()
		{
			Restore(FJITDatabase::Get());
		}

	private:
		void Save(const FJITDatabase& Database)
		{
			SavedFunctions = Database.Functions;
			SavedFunctionLookups = Database.FunctionLookups;
			SavedSystemFunctionPointerLookups = Database.SystemFunctionPointerLookups;
			SavedGlobalVarLookups = Database.GlobalVarLookups;
			SavedTypeInfoLookups = Database.TypeInfoLookups;
			SavedPropertyOffsetLookups = Database.PropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			SavedVerifyPropertyOffsets = Database.VerifyPropertyOffsets;
			SavedVerifyTypeSizes = Database.VerifyTypeSizes;
			SavedVerifyTypeAlignments = Database.VerifyTypeAlignments;
#endif
		}

		void Restore(FJITDatabase& Database) const
		{
			Database.Functions = SavedFunctions;
			Database.FunctionLookups = SavedFunctionLookups;
			Database.SystemFunctionPointerLookups = SavedSystemFunctionPointerLookups;
			Database.GlobalVarLookups = SavedGlobalVarLookups;
			Database.TypeInfoLookups = SavedTypeInfoLookups;
			Database.PropertyOffsetLookups = SavedPropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			Database.VerifyPropertyOffsets = SavedVerifyPropertyOffsets;
			Database.VerifyTypeSizes = SavedVerifyTypeSizes;
			Database.VerifyTypeAlignments = SavedVerifyTypeAlignments;
#endif
		}

		TMap<uint32, FJITDatabase::FJITFunctions> SavedFunctions;
		TArray<void**> SavedFunctionLookups;
		TArray<void*> SavedSystemFunctionPointerLookups;
		TArray<void**> SavedGlobalVarLookups;
		TArray<void**> SavedTypeInfoLookups;
		TArray<TPair<uint64, uint32*>> SavedPropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
		TArray<TPair<uint64, SIZE_T>> SavedVerifyPropertyOffsets;
		TArray<TPair<uint64, SIZE_T>> SavedVerifyTypeSizes;
		TArray<TPair<uint64, SIZE_T>> SavedVerifyTypeAlignments;
#endif
	};

	static TUniquePtr<FAngelscriptEngine> CreateTestEngine(FAutomationTestBase& Test)
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
		Test.TestNotNull(TEXT("StaticJIT tests should create an isolated testing engine"), Engine.Get());
		return Engine;
	}

	static asIScriptFunction* CompileFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const ANSICHAR* Source,
		const ANSICHAR* Declaration,
		asIScriptModule*& OutModule)
	{
		FAngelscriptEngineScope EngineScope(Engine);

		OutModule = Engine.GetScriptEngine()->GetModule(TCHAR_TO_ANSI(*ModuleName), asGM_ALWAYS_CREATE);
		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT helper should create module '%s'"), *ModuleName), OutModule))
		{
			return nullptr;
		}

		asIScriptFunction* Function = nullptr;
		const int32 CompileResult = OutModule->CompileFunction(TCHAR_TO_ANSI(*ModuleName), Source, 0, 0, &Function);
		if (!Test.TestEqual(*FString::Printf(TEXT("StaticJIT helper should compile '%s'"), *ModuleName), CompileResult, asSUCCESS))
		{
			return nullptr;
		}

		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT helper should resolve '%s'"), ANSI_TO_TCHAR(Declaration)), Function))
		{
			return nullptr;
		}

		return Function;
	}

	static asIScriptModule* BuildModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const ANSICHAR* Source,
		const ANSICHAR* FunctionName)
	{
		FAngelscriptEngineScope EngineScope(Engine);

		asIScriptModule* Module = Engine.GetScriptEngine()->GetModule(TCHAR_TO_ANSI(*ModuleName), asGM_ALWAYS_CREATE);
		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT helper should create buildable module '%s'"), *ModuleName), Module))
		{
			return nullptr;
		}

		const int32 AddSectionResult = Module->AddScriptSection(TCHAR_TO_ANSI(*ModuleName), Source);
		if (!Test.TestEqual(*FString::Printf(TEXT("StaticJIT helper should add a script section to '%s'"), *ModuleName), AddSectionResult, asSUCCESS))
		{
			return nullptr;
		}

		const int32 BuildResult = Module->Build();
		if (!Test.TestEqual(*FString::Printf(TEXT("StaticJIT helper should build '%s'"), *ModuleName), BuildResult, asSUCCESS))
		{
			return nullptr;
		}

		asIScriptFunction* Function = Module->GetFunctionByName(FunctionName);
		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT helper should resolve built function '%s'"), ANSI_TO_TCHAR(FunctionName)), Function))
		{
			return nullptr;
		}

		return Module;
	}

	static bool GenerateSourceText(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		bool bEmitDebugMetadata,
		FString& OutSourceText)
	{
		FString Error;
		FAngelscriptEngineScope EngineScope(Engine);
		const bool bGenerated = GenerateStaticJITSourceTextForTesting(&Module, OutSourceText, bEmitDebugMetadata, &Error);
		if (!Test.TestTrue(TEXT("GenerateStaticJITSourceTextForTesting should succeed for a compiled module"), bGenerated))
		{
			if (!Error.IsEmpty())
			{
				Test.AddError(Error);
			}
			return false;
		}

		if (!Test.TestTrue(TEXT("GenerateStaticJITSourceTextForTesting should not emit an error for a compiled module"), Error.IsEmpty()))
		{
			return false;
		}

		return Test.TestFalse(TEXT("GenerateStaticJITSourceTextForTesting should produce non-empty source text"), OutSourceText.IsEmpty());
	}
}

using namespace AngelscriptStaticJITTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITCompileFunctionGenerateModeQueuesFunctionTest,
	"Angelscript.CppTests.StaticJIT.CompileFunction.GenerateModeQueuesFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITGenerateSourceTextDebugMetadataToggleTest,
	"Angelscript.CppTests.StaticJIT.GenerateSourceText.DebugMetadataToggle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITGenerateSourceTextRejectsNullModuleTest,
	"Angelscript.CppTests.StaticJIT.GenerateSourceText.RejectsNullModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITDatabaseClearResetsRegisteredStateTest,
	"Angelscript.CppTests.StaticJIT.Database.ClearResetsRegisteredState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStaticJITCompileFunctionGenerateModeQueuesFunctionTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asIScriptModule* Module = nullptr;
	const FString ModuleName = MakeUniqueModuleName(TEXT("StaticJITCompileQueue"));
	asIScriptFunction* Function = CompileFunction(*this, *OwnedEngine, ModuleName, BasicFunctionSource, BasicFunctionDecl, Module);
	if (Function == nullptr)
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		Function->Release();
	};

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("Generate-mode compile should expose the underlying script engine"), ScriptEngine))
	{
		return false;
	}

	FAngelscriptStaticJIT JIT;
	FAngelscriptPrecompiledData PrecompiledData(ScriptEngine);
	JIT.PrecompiledData = &PrecompiledData;
	JIT.bGenerateOutputCode = true;

	asJITFunction GeneratedEntry = &StaticJITVMEntryStub;
	const int32 CompileResult = JIT.CompileFunction(Function, &GeneratedEntry);
	if (!TestEqual(TEXT("Generate-mode CompileFunction should queue the script function for source generation"), CompileResult, 1))
	{
		return false;
	}

	if (!TestNull(TEXT("Generate-mode CompileFunction should not bind a runtime JIT function pointer"), GeneratedEntry))
	{
		return false;
	}

	const bool bQueuedFunction = TestTrue(
		TEXT("Generate-mode CompileFunction should record the queued script function"),
		JIT.FunctionsToGenerate.Contains(reinterpret_cast<asCScriptFunction*>(Function)));

	JIT.ReleaseJITFunction(GeneratedEntry);
	const bool bReleaseIsNoOp = TestEqual(
		TEXT("ReleaseJITFunction should not discard queued generate-mode work"),
		JIT.FunctionsToGenerate.Num(),
		1);

	return bQueuedFunction && bReleaseIsNoOp;
}

bool FAngelscriptStaticJITGenerateSourceTextDebugMetadataToggleTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	const FString ModuleName = MakeUniqueModuleName(TEXT("StaticJITSourceText"));
	asIScriptModule* Module = BuildModule(*this, *OwnedEngine, ModuleName, BasicFunctionSource, BasicFunctionName);
	if (Module == nullptr)
	{
		return false;
	}

	FString SourceWithDebug;
	FString SourceWithoutDebug;
	if (!GenerateSourceText(*this, *OwnedEngine, *Module, true, SourceWithDebug)
		|| !GenerateSourceText(*this, *OwnedEngine, *Module, false, SourceWithoutDebug))
	{
		return false;
	}

	if (!TestTrue(TEXT("Debug-metadata source generation should emit callstack frame markers"), SourceWithDebug.Contains(TEXT("SCRIPT_DEBUG_CALLSTACK_FRAME"))))
	{
		return false;
	}

	if (!TestTrue(TEXT("Debug-metadata source generation should emit a script debug filename binding"), SourceWithDebug.Contains(TEXT("SCRIPT_DEBUG_FILENAME"))))
	{
		return false;
	}

	if (!TestFalse(TEXT("Non-debug source generation should omit callstack frame markers"), SourceWithoutDebug.Contains(TEXT("SCRIPT_DEBUG_CALLSTACK_FRAME"))))
	{
		return false;
	}

	if (!TestFalse(TEXT("Non-debug source generation should omit script debug filename bindings"), SourceWithoutDebug.Contains(TEXT("SCRIPT_DEBUG_FILENAME"))))
	{
		return false;
	}

	return TestNotEqual(TEXT("Debug metadata should materially change the generated static JIT source"), SourceWithDebug, SourceWithoutDebug);
}

bool FAngelscriptStaticJITGenerateSourceTextRejectsNullModuleTest::RunTest(const FString& Parameters)
{
	FString SourceText;
	FString Error;
	const bool bGenerated = GenerateStaticJITSourceTextForTesting(nullptr, SourceText, true, &Error);
	if (!TestFalse(TEXT("GenerateStaticJITSourceTextForTesting should reject null modules"), bGenerated))
	{
		return false;
	}

	if (!TestTrue(TEXT("GenerateStaticJITSourceTextForTesting should explain null-module failures"), Error.Contains(TEXT("module was null"))))
	{
		return false;
	}

	return TestTrue(TEXT("GenerateStaticJITSourceTextForTesting should keep source output empty on failure"), SourceText.IsEmpty());
}

bool FAngelscriptStaticJITDatabaseClearResetsRegisteredStateTest::RunTest(const FString& Parameters)
{
	FScopedJITDatabaseSnapshot DatabaseSnapshot;

	FJITDatabase& Database = FJITDatabase::Get();
	if (!TestTrue(TEXT("FJITDatabase::Get should return a stable singleton"), &Database == &FJITDatabase::Get()))
	{
		return false;
	}

	FJITDatabase::Clear();

	FStaticJITFunction RegisteredFunction(RegisteredFunctionId, &StaticJITVMEntryStub, &StaticJITParmsEntryStub, &StaticJITRawStub);
	FJitRef_Function FunctionLookup(FunctionLookupToken);
	FJitRef_SystemFunctionPointer SystemFunctionLookup(SystemFunctionLookupToken);
	FJitRef_GlobalVar GlobalVarLookup(GlobalVarLookupToken);
	FJitRef_Type TypeLookup(TypeLookupToken);
	FJitRef_PropertyOffset PropertyOffsetLookup(PropertyLookupToken);
	(void)RegisteredFunction;
	(void)FunctionLookup;
	(void)SystemFunctionLookup;
	(void)GlobalVarLookup;
	(void)TypeLookup;
	(void)PropertyOffsetLookup;

	if (!TestEqual(TEXT("FJITDatabase should record registered JIT functions"), Database.Functions.Num(), 1))
	{
		return false;
	}

	if (!TestTrue(TEXT("FJITDatabase should preserve the registered function id"), Database.Functions.Contains(RegisteredFunctionId)))
	{
		return false;
	}

	if (!TestEqual(TEXT("FJITDatabase should track function lookups"), Database.FunctionLookups.Num(), 1)
		|| !TestEqual(TEXT("FJITDatabase should track system-function pointer lookups"), Database.SystemFunctionPointerLookups.Num(), 1)
		|| !TestEqual(TEXT("FJITDatabase should track global-variable lookups"), Database.GlobalVarLookups.Num(), 1)
		|| !TestEqual(TEXT("FJITDatabase should track type-info lookups"), Database.TypeInfoLookups.Num(), 1)
		|| !TestEqual(TEXT("FJITDatabase should track property-offset lookups"), Database.PropertyOffsetLookups.Num(), 1))
	{
		return false;
	}

	if (!TestEqual(TEXT("Property-offset lookups should preserve the tracked token"), Database.PropertyOffsetLookups[0].Key, PropertyLookupToken))
	{
		return false;
	}

	FJITDatabase::Clear();
	return TestTrue(
		TEXT("FJITDatabase::Clear should remove every registered StaticJIT container entry"),
		Database.Functions.IsEmpty()
			&& Database.FunctionLookups.IsEmpty()
			&& Database.SystemFunctionPointerLookups.IsEmpty()
			&& Database.GlobalVarLookups.IsEmpty()
			&& Database.TypeInfoLookups.IsEmpty()
			&& Database.PropertyOffsetLookups.IsEmpty());
}

#endif
