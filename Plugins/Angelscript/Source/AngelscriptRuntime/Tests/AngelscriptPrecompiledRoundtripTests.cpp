#include "AngelscriptEngine.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "StaticJIT/PrecompiledData.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_module.h"
#include "source/as_scriptengine.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptPrecompiledRoundtripTests_Private
{
	static constexpr ANSICHAR ModuleRoundtripSource[] =
R"(enum ERoundtripState
{
	Idle = 0,
	Busy = 1
}

const int GSeed = 7;

class RoundtripWidget
{
	int Value;

	int DoubleIt() const
	{
		return Value * 2;
	}
}

int Compute(int InValue)
{
	return GSeed + InValue;
}
)";

	static constexpr ANSICHAR ModuleRoundtripFunctionName[] = "Compute";
	static constexpr uint64 ExpectedCodeHash = 0x1234ABCDEF987654ull;
	static constexpr TCHAR ExpectedRelativeFilename[] = TEXT("Script/Automation/PrecompiledRoundtrip.as");
	static constexpr TCHAR ExpectedImportedModule[] = TEXT("Tests.Precompiled.Dependency");
	static constexpr TCHAR ExpectedPostInitFunction[] = TEXT("PostInit_Roundtrip");
	static constexpr TCHAR FirstStaticName[] = TEXT("Roundtrip.StaticName");
	static constexpr TCHAR SecondStaticName[] = TEXT("Roundtrip.SecondStaticName");

	static FString MakeUniqueName(const TCHAR* Prefix)
	{
		return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	static FString MakeUniqueFilename(const TCHAR* Prefix)
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), FString::Printf(TEXT("%s_%s.bin"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	static TUniquePtr<FAngelscriptEngine> CreateTestEngine(FAutomationTestBase& Test)
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
		Test.TestNotNull(TEXT("Precompiled roundtrip tests should create an isolated testing engine"), Engine.Get());
		return Engine;
	}

	static asCModule* BuildModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const ANSICHAR* Source,
		const ANSICHAR* FunctionName)
	{
		FAngelscriptEngineScope EngineScope(Engine);

		asIScriptModule* Module = Engine.GetScriptEngine()->GetModule(TCHAR_TO_ANSI(*ModuleName), asGM_ALWAYS_CREATE);
		if (!Test.TestNotNull(*FString::Printf(TEXT("Precompiled roundtrip helper should create module '%s'"), *ModuleName), Module))
		{
			return nullptr;
		}

		const int32 AddSectionResult = Module->AddScriptSection(TCHAR_TO_ANSI(*ModuleName), Source);
		if (!Test.TestEqual(*FString::Printf(TEXT("Precompiled roundtrip helper should add script text to '%s'"), *ModuleName), AddSectionResult, asSUCCESS))
		{
			return nullptr;
		}

		const int32 BuildResult = Module->Build();
		if (!Test.TestEqual(*FString::Printf(TEXT("Precompiled roundtrip helper should build '%s'"), *ModuleName), BuildResult, asSUCCESS))
		{
			return nullptr;
		}

		asIScriptFunction* Function = Module->GetFunctionByName(FunctionName);
		if (!Test.TestNotNull(*FString::Printf(TEXT("Precompiled roundtrip helper should resolve '%s' in '%s'"), ANSI_TO_TCHAR(FunctionName), *ModuleName), Function))
		{
			return nullptr;
		}

		return static_cast<asCModule*>(Module);
	}

	static bool SaveAndLoad(
		FAutomationTestBase& Test,
		FAngelscriptPrecompiledData& SourceData,
		FAngelscriptPrecompiledData& OutLoadedData,
		const TCHAR* FilenamePrefix)
	{
		const FString Filename = MakeUniqueFilename(FilenamePrefix);
		ON_SCOPE_EXIT
		{
			IFileManager::Get().Delete(*Filename);
		};

		SourceData.Save(Filename);
		if (!Test.TestTrue(TEXT("Save should produce a serialized precompiled data file"), IFileManager::Get().FileExists(*Filename)))
		{
			return false;
		}

		OutLoadedData.Load(Filename);
		return true;
	}
}

using namespace AngelscriptPrecompiledRoundtripTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledEmptyDataRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.EmptyDataRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledModuleDataRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.ModuleDataRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPrecompiledEmptyDataRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("Empty-data roundtrip should expose the underlying script engine"), ScriptEngine))
	{
		return false;
	}

	FAngelscriptPrecompiledData SourceData(ScriptEngine);
	SourceData.DataGuid = FGuid::NewGuid();
	SourceData.BuildIdentifier = SourceData.GetCurrentBuildIdentifier();
	SourceData.StaticNames.Add(FStringInArchive(FString(FirstStaticName)));
	SourceData.StaticNames.Add(FStringInArchive(FString(SecondStaticName)));

	FAngelscriptPrecompiledData LoadedData(ScriptEngine);
	if (!SaveAndLoad(*this, SourceData, LoadedData, TEXT("PrecompiledEmptyRoundtrip")))
	{
		return false;
	}

	if (!TestEqual(TEXT("Empty-data roundtrip should preserve the data guid"), LoadedData.DataGuid, SourceData.DataGuid))
	{
		return false;
	}

	if (!TestEqual(TEXT("Empty-data roundtrip should preserve the build identifier"), LoadedData.BuildIdentifier, SourceData.BuildIdentifier))
	{
		return false;
	}

	if (!TestTrue(TEXT("Empty-data roundtrip should remain valid for the current build"), LoadedData.IsValidForCurrentBuild()))
	{
		return false;
	}

	if (!TestTrue(TEXT("Empty-data roundtrip should keep module storage empty"), LoadedData.Modules.IsEmpty())
		|| !TestTrue(TEXT("Empty-data roundtrip should keep type references empty"), LoadedData.TypeReferences.IsEmpty())
		|| !TestTrue(TEXT("Empty-data roundtrip should keep function references empty"), LoadedData.FunctionReferences.IsEmpty())
		|| !TestTrue(TEXT("Empty-data roundtrip should keep global references empty"), LoadedData.GlobalReferences.IsEmpty())
		|| !TestTrue(TEXT("Empty-data roundtrip should keep property references empty"), LoadedData.PropertyReferences.IsEmpty()))
	{
		return false;
	}

	if (!TestEqual(TEXT("Empty-data roundtrip should preserve static-name count"), LoadedData.StaticNames.Num(), 2))
	{
		return false;
	}

	if (!TestEqual(TEXT("Empty-data roundtrip should preserve the first static name"), LoadedData.StaticNames[0].UnrealString(), FString(FirstStaticName)))
	{
		return false;
	}

	return TestEqual(TEXT("Empty-data roundtrip should preserve the second static name"), LoadedData.StaticNames[1].UnrealString(), FString(SecondStaticName));
}

bool FAngelscriptPrecompiledModuleDataRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("Module-data roundtrip should expose the underlying script engine"), ScriptEngine))
	{
		return false;
	}

	const FString ModuleName = MakeUniqueName(TEXT("PrecompiledRoundtripModule"));
	asCModule* ScriptModule = BuildModule(*this, *OwnedEngine, ModuleName, ModuleRoundtripSource, ModuleRoundtripFunctionName);
	if (ScriptModule == nullptr)
	{
		return false;
	}

	FAngelscriptPrecompiledData SourceData(ScriptEngine);
	SourceData.BuildIdentifier = SourceData.GetCurrentBuildIdentifier();
	SourceData.ModuleDesc = MakeShared<FAngelscriptModuleDesc>();
	SourceData.ModuleDesc->ModuleName = ModuleName;
	SourceData.ModuleDesc->CodeHash = static_cast<int64>(ExpectedCodeHash);
	SourceData.ModuleDesc->ImportedModules.Add(ExpectedImportedModule);
	SourceData.ModuleDesc->PostInitFunctions.Add(ExpectedPostInitFunction);

	FAngelscriptModuleDesc::FCodeSection& CodeSection = SourceData.ModuleDesc->Code.AddDefaulted_GetRef();
	CodeSection.RelativeFilename = ExpectedRelativeFilename;
	CodeSection.AbsoluteFilename = FPaths::Combine(FPaths::ProjectDir(), ExpectedRelativeFilename);
	CodeSection.Code = UTF8_TO_TCHAR(ModuleRoundtripSource);
	CodeSection.CodeHash = static_cast<int64>(ExpectedCodeHash);

	SourceData.Modules.FindOrAdd(ModuleName).InitFrom(SourceData, ScriptModule);

	FAngelscriptPrecompiledData LoadedData(ScriptEngine);
	if (!SaveAndLoad(*this, SourceData, LoadedData, TEXT("PrecompiledModuleRoundtrip")))
	{
		return false;
	}

	if (!TestTrue(TEXT("Module-data roundtrip should preserve the serialized module map entry"), LoadedData.Modules.Contains(ModuleName)))
	{
		return false;
	}

	const FAngelscriptPrecompiledModule* LoadedModule = LoadedData.Modules.Find(ModuleName);
	if (!TestNotNull(TEXT("Module-data roundtrip should load the serialized module"), LoadedModule))
	{
		return false;
	}

	if (!TestEqual(TEXT("Module-data roundtrip should preserve the module name"), LoadedModule->ModuleName.UnrealString(), ModuleName)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the module code hash"), static_cast<uint64>(LoadedModule->CodeHash), ExpectedCodeHash)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve imported-module count"), LoadedModule->ImportedModules.Num(), 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve post-init function count"), LoadedModule->PostInitFunctions.Num(), 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the relative script filename"), LoadedModule->ScriptRelativeFilename.UnrealString(), FString(ExpectedRelativeFilename)))
	{
		return false;
	}

	if (!TestEqual(TEXT("Module-data roundtrip should preserve the imported module name"), LoadedModule->ImportedModules[0].UnrealString(), FString(ExpectedImportedModule))
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the post-init function name"), LoadedModule->PostInitFunctions[0].UnrealString(), FString(ExpectedPostInitFunction)))
	{
		return false;
	}

	const bool bFoundComputeFunction = LoadedModule->Functions.ContainsByPredicate([](const FAngelscriptPrecompiledFunction& Function)
	{
		return Function.FunctionName.UnrealString() == TEXT("Compute");
	});

	if (!TestTrue(TEXT("Module-data roundtrip should preserve at least one global function entry"), LoadedModule->Functions.Num() >= 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve class count"), LoadedModule->Classes.Num(), 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve enum count"), LoadedModule->Enums.Num(), 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve global-variable count"), LoadedModule->GlobalVariables.Num(), 1))
	{
		return false;
	}

	const bool bFoundDoubleItMethod = LoadedModule->Classes[0].Methods.ContainsByPredicate([](const FAngelscriptPrecompiledFunction& Function)
	{
		return Function.FunctionName.UnrealString() == TEXT("DoubleIt");
	});

	if (!TestTrue(TEXT("Module-data roundtrip should preserve the named global function"), bFoundComputeFunction)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the class name"), LoadedModule->Classes[0].ClassName.UnrealString(), FString(TEXT("RoundtripWidget")))
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the enum name"), LoadedModule->Enums[0].Name.UnrealString(), FString(TEXT("ERoundtripState")))
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the global-variable name"), LoadedModule->GlobalVariables[0].Name.UnrealString(), FString(TEXT("GSeed"))))
	{
		return false;
	}

	if (!TestTrue(TEXT("Module-data roundtrip should preserve pure-constant global metadata"), LoadedModule->GlobalVariables[0].bIsPureConstant)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the pure-constant global value"), LoadedModule->GlobalVariables[0].PureConstantValue, static_cast<uint64>(7)))
	{
		return false;
	}

	if (!TestEqual(TEXT("Module-data roundtrip should preserve class property count"), LoadedModule->Classes[0].Properties.Num(), 1)
		|| !TestTrue(TEXT("Module-data roundtrip should preserve at least one class method entry"), LoadedModule->Classes[0].Methods.Num() >= 1)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve enum value count"), LoadedModule->Enums[0].EnumNames.Num(), 2))
	{
		return false;
	}

	if (!TestEqual(TEXT("Module-data roundtrip should preserve the class property name"), LoadedModule->Classes[0].Properties[0].Name.UnrealString(), FString(TEXT("Value")))
		|| !TestTrue(TEXT("Module-data roundtrip should preserve the named class method"), bFoundDoubleItMethod)
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the first enum name"), LoadedModule->Enums[0].EnumNames[0].UnrealString(), FString(TEXT("Idle")))
		|| !TestEqual(TEXT("Module-data roundtrip should preserve the second enum name"), LoadedModule->Enums[0].EnumNames[1].UnrealString(), FString(TEXT("Busy"))))
	{
		return false;
	}

	if (!TestTrue(TEXT("Module-data roundtrip should serialize type references for module-owned types"), LoadedData.TypeReferences.Num() > 0)
		|| !TestTrue(TEXT("Module-data roundtrip should serialize type-id lookup metadata"), LoadedData.TypeIdReferenceToPointer.Num() > 0)
		|| !TestTrue(TEXT("Module-data roundtrip should serialize function references"), LoadedData.FunctionReferences.Num() > 0)
		|| !TestTrue(TEXT("Module-data roundtrip should serialize function-id lookup metadata"), LoadedData.FunctionIdReferenceToPointer.Num() > 0)
		|| !TestTrue(TEXT("Module-data roundtrip should serialize property references"), LoadedData.PropertyReferences.Num() > 0))
	{
		return false;
	}

	return TestTrue(TEXT("Module-data roundtrip should remain valid for the current build"), LoadedData.IsValidForCurrentBuild());
}

#endif
