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

namespace AngelscriptPrecompiledFunctionImportRoundtripTests_Private
{
	static constexpr uint64 ExpectedCodeHash = 0x2468ACE13579BDFull;
	static constexpr TCHAR ExpectedRelativeFilename[] = TEXT("Script/Automation/PrecompiledFunctionImport.as");

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
		Test.TestNotNull(TEXT("Precompiled function-import tests should create an isolated testing engine"), Engine.Get());
		return Engine;
	}

	static asCModule* BuildModule(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const FString& Source)
	{
		FAngelscriptEngineScope EngineScope(Engine);

		asIScriptModule* Module = Engine.GetScriptEngine()->GetModule(TCHAR_TO_ANSI(*ModuleName), asGM_ALWAYS_CREATE);
		if (!Test.TestNotNull(*FString::Printf(TEXT("Function-import helper should create module '%s'"), *ModuleName), Module))
		{
			return nullptr;
		}

		const int32 AddSectionResult = Module->AddScriptSection(TCHAR_TO_ANSI(*ModuleName), TCHAR_TO_ANSI(*Source));
		if (!Test.TestEqual(*FString::Printf(TEXT("Function-import helper should add script text to '%s'"), *ModuleName), AddSectionResult, asSUCCESS))
		{
			return nullptr;
		}

		const int32 BuildResult = Module->Build();
		if (!Test.TestEqual(*FString::Printf(TEXT("Function-import helper should build '%s'"), *ModuleName), BuildResult, asSUCCESS))
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
		if (!Test.TestTrue(TEXT("Function-import roundtrip should write a serialized precompiled data file"), IFileManager::Get().FileExists(*Filename)))
		{
			return false;
		}

		OutLoadedData.Load(Filename);
		return true;
	}

	static FString GetImportedDeclaration(asCModule* Module, int32 ImportIndex)
	{
		const char* Declaration = Module->GetImportedFunctionDeclaration(static_cast<asUINT>(ImportIndex));
		return Declaration != nullptr ? FString(ANSI_TO_TCHAR(Declaration)) : FString();
	}

	static bool TestImportedFunctionSurface(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		asCModule* Module,
		const FString& ExpectedSourceModuleName)
	{
		const int32 ImportedFunctionCount = static_cast<int32>(Module->GetImportedFunctionCount());
		if (!Test.TestEqual(
				FString::Printf(TEXT("%s should preserve exactly one declared import"), Context),
				ImportedFunctionCount,
				1))
		{
			return false;
		}

		const FString ImportedSourceModuleName = ANSI_TO_TCHAR(Module->GetImportedFunctionSourceModule(0));
		const FString ImportedDeclaration = GetImportedDeclaration(Module, 0);
		return Test.TestEqual(
				FString::Printf(TEXT("%s should preserve the imported source module"), Context),
				ImportedSourceModuleName,
				ExpectedSourceModuleName)
			&& Test.TestTrue(
				FString::Printf(TEXT("%s should preserve the imported function name in the declaration"), Context),
				ImportedDeclaration.Contains(TEXT("SharedImportedValue")))
			&& Test.TestTrue(
				FString::Printf(TEXT("%s should preserve two integer parameters in the declaration"), Context),
				ImportedDeclaration.Contains(TEXT("int")) &&
					ImportedDeclaration.Contains(TEXT(",")));
	}
}

using namespace AngelscriptPrecompiledFunctionImportRoundtripTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledFunctionImportSignatureRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.FunctionImport.SignatureRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPrecompiledFunctionImportSignatureRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("Function-import roundtrip should expose the underlying script engine"), ScriptEngine))
	{
		return false;
	}

	const bool bPreviousUseAutomaticImportMethod = OwnedEngine->bUseAutomaticImportMethod;
	OwnedEngine->bUseAutomaticImportMethod = false;
	ON_SCOPE_EXIT
	{
		OwnedEngine->bUseAutomaticImportMethod = bPreviousUseAutomaticImportMethod;
	};

	const asPWORD PreviousAutomaticImports = ScriptEngine->GetEngineProperty(asEP_AUTOMATIC_IMPORTS);
	ScriptEngine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, 0);
	ON_SCOPE_EXIT
	{
		ScriptEngine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, PreviousAutomaticImports);
	};

	const FString SourceModuleName = MakeUniqueName(TEXT("PrecompiledImportSource"));
	const FString ConsumerModuleName = MakeUniqueName(TEXT("PrecompiledImportConsumer"));
	const FString RecreatedModuleName = MakeUniqueName(TEXT("PrecompiledImportRecreated"));

	asCModule* SourceModule = BuildModule(
		*this,
		*OwnedEngine,
		SourceModuleName,
		TEXT(R"AS(
int SharedImportedValue(int Value, int Scale)
{
	return Value * Scale;
}
)AS"));
	if (SourceModule == nullptr)
	{
		return false;
	}

	const FString ConsumerSource = FString::Printf(
		TEXT(R"AS(
import int SharedImportedValue(int Value, int Scale) from "%s";

int Entry()
{
	return SharedImportedValue(7, 2);
}
)AS"),
		*SourceModuleName);

	asCModule* ConsumerModule = BuildModule(*this, *OwnedEngine, ConsumerModuleName, ConsumerSource);
	if (ConsumerModule == nullptr)
	{
		return false;
	}

	if (!TestEqual(
			TEXT("Function-import source module should expose the native import target function"),
			SourceModule->GetFunctionByName("SharedImportedValue") != nullptr,
			true)
		|| !TestImportedFunctionSurface(
			*this,
			TEXT("Function-import consumer module"),
			ConsumerModule,
			SourceModuleName))
	{
		return false;
	}

	FAngelscriptPrecompiledData SourceData(ScriptEngine);
	SourceData.BuildIdentifier = SourceData.GetCurrentBuildIdentifier();
	SourceData.ModuleDesc = MakeShared<FAngelscriptModuleDesc>();
	SourceData.ModuleDesc->ModuleName = ConsumerModuleName;
	SourceData.ModuleDesc->CodeHash = static_cast<int64>(ExpectedCodeHash);

	FAngelscriptModuleDesc::FCodeSection& CodeSection = SourceData.ModuleDesc->Code.AddDefaulted_GetRef();
	CodeSection.RelativeFilename = ExpectedRelativeFilename;
	CodeSection.AbsoluteFilename = FPaths::Combine(FPaths::ProjectDir(), ExpectedRelativeFilename);
	CodeSection.Code = ConsumerSource;
	CodeSection.CodeHash = static_cast<int64>(ExpectedCodeHash);

	SourceData.Modules.FindOrAdd(ConsumerModuleName).InitFrom(SourceData, ConsumerModule);
	const FAngelscriptPrecompiledModule* StoredModule = SourceData.Modules.Find(ConsumerModuleName);
	if (!TestNotNull(TEXT("Function-import roundtrip should store the consumer module"), StoredModule)
		|| !TestEqual(TEXT("Function-import roundtrip should store one function import"), StoredModule->FunctionImports.Num(), 1))
	{
		return false;
	}

	const FAngelscriptPrecompiledFunctionImport& StoredImport = StoredModule->FunctionImports[0];
	if (!TestEqual(TEXT("Function-import storage should preserve the source module name"), StoredImport.ImportedFromModule.UnrealString(), SourceModuleName)
		|| !TestEqual(TEXT("Function-import storage should preserve the imported function name"), StoredImport.Signature.Name.UnrealString(), FString(TEXT("SharedImportedValue")))
		|| !TestEqual(TEXT("Function-import storage should preserve the imported parameter count"), StoredImport.Signature.ParameterTypes.Num(), 2)
		|| !TestEqual(TEXT("Function-import storage should preserve the first default argument as empty"), StoredImport.Signature.ParameterDefaultArgs[0].Len(), 0)
		|| !TestEqual(TEXT("Function-import storage should preserve the second default argument as empty"), StoredImport.Signature.ParameterDefaultArgs[1].Len(), 0))
	{
		return false;
	}

	FAngelscriptPrecompiledData LoadedData(ScriptEngine);
	if (!SaveAndLoad(*this, SourceData, LoadedData, TEXT("PrecompiledFunctionImportRoundtrip")))
	{
		return false;
	}

	const FAngelscriptPrecompiledModule* LoadedModule = LoadedData.Modules.Find(ConsumerModuleName);
	if (!TestNotNull(TEXT("Function-import roundtrip should reload the consumer module"), LoadedModule)
		|| !TestEqual(TEXT("Function-import roundtrip should reload one function import"), LoadedModule->FunctionImports.Num(), 1))
	{
		return false;
	}

	const FAngelscriptPrecompiledFunctionImport& LoadedImport = LoadedModule->FunctionImports[0];
	if (!TestEqual(TEXT("Function-import roundtrip should preserve the loaded source module name"), LoadedImport.ImportedFromModule.UnrealString(), SourceModuleName)
		|| !TestEqual(TEXT("Function-import roundtrip should preserve the loaded imported function name"), LoadedImport.Signature.Name.UnrealString(), FString(TEXT("SharedImportedValue")))
		|| !TestEqual(TEXT("Function-import roundtrip should preserve the loaded parameter count"), LoadedImport.Signature.ParameterTypes.Num(), 2))
	{
		return false;
	}

	asCModule* RecreatedModule = static_cast<asCModule*>(ScriptEngine->GetModule(TCHAR_TO_ANSI(*RecreatedModuleName), asGM_ALWAYS_CREATE));
	if (!TestNotNull(TEXT("Function-import roundtrip should create a module for import recreation"), RecreatedModule))
	{
		return false;
	}

	LoadedImport.AddToModule(LoadedData, RecreatedModule);
	if (!TestImportedFunctionSurface(
			*this,
			TEXT("Function-import recreation"),
			RecreatedModule,
			SourceModuleName))
	{
		return false;
	}

	const FString RecreatedDeclaration = GetImportedDeclaration(RecreatedModule, 0);
	return TestTrue(
		TEXT("Function-import recreation should preserve the imported declaration surface"),
		RecreatedDeclaration.Contains(TEXT("SharedImportedValue")) &&
			RecreatedDeclaration.Contains(TEXT("int")));
}

#endif
