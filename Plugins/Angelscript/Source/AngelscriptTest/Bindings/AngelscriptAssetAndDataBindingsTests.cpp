#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/AssetManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Templates/Function.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptAssetAndDataBindingsTests_Private
{
	static constexpr ANSICHAR AssetManagerDirectModuleName[] = "ASAssetManagerDirectCompat";
	static constexpr ANSICHAR PackageDirtyModuleName[] = "ASPackageDirtyCompat";

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	const TCHAR* BoolToScriptLiteral(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	IAssetRegistry& GetAssetRegistryChecked()
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::Get().LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		return AssetRegistryModule.Get();
	}

	bool SetArgAddressChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Address,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind address argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgAddress(ArgumentIndex, Address),
			static_cast<int32>(asSUCCESS));
	}

	bool SetArgDWordChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		const uint32 Value,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind dword argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgDWord(ArgumentIndex, Value),
			static_cast<int32>(asSUCCESS));
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	bool TryResolveValidPrimaryAsset(
		UAssetManager& AssetManager,
		FPrimaryAssetId& OutPrimaryAssetId,
		FSoftObjectPath& OutPrimaryAssetPath,
		FAssetData& OutPrimaryAssetData)
	{
		IAssetRegistry& AssetRegistry = GetAssetRegistryChecked();

		TArray<FPrimaryAssetTypeInfo> AssetTypeInfoList;
		AssetManager.GetPrimaryAssetTypeInfoList(AssetTypeInfoList);
		for (const FPrimaryAssetTypeInfo& AssetTypeInfo : AssetTypeInfoList)
		{
			if (!AssetTypeInfo.PrimaryAssetType.IsValid())
			{
				continue;
			}

			TArray<FPrimaryAssetId> PrimaryAssetIds;
			if (!AssetManager.GetPrimaryAssetIdList(AssetTypeInfo.PrimaryAssetType, PrimaryAssetIds))
			{
				continue;
			}

			for (const FPrimaryAssetId& PrimaryAssetId : PrimaryAssetIds)
			{
				if (!PrimaryAssetId.IsValid())
				{
					continue;
				}

				const FSoftObjectPath PrimaryAssetPath = AssetManager.GetPrimaryAssetPath(PrimaryAssetId);
				if (!PrimaryAssetPath.IsValid())
				{
					continue;
				}

				const FAssetData PrimaryAssetData = AssetRegistry.GetAssetByObjectPath(PrimaryAssetPath, false);
				if (!PrimaryAssetData.IsValid())
				{
					continue;
				}

				if (!(AssetManager.GetPrimaryAssetIdForPath(PrimaryAssetPath) == PrimaryAssetId))
				{
					continue;
				}

				if (!(AssetManager.GetPrimaryAssetIdForData(PrimaryAssetData) == PrimaryAssetId))
				{
					continue;
				}

				OutPrimaryAssetId = PrimaryAssetId;
				OutPrimaryAssetPath = PrimaryAssetPath;
				OutPrimaryAssetData = PrimaryAssetData;
				return true;
			}
		}

		return false;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptAssetAndDataBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAssetManagerDirectCompatTest,
	"Angelscript.TestModule.Bindings.AssetManagerDirectCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPackageDirtyCompatTest,
	"Angelscript.TestModule.Bindings.PackageDirtyCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptAssetManagerDirectCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASAssetManagerDirectCompat"));
	};

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!TestNotNull(TEXT("Asset manager direct compat test should resolve an initialized asset manager"), AssetManager))
	{
		return false;
	}

	if (!TestTrue(TEXT("Asset manager direct compat test requires the asset manager initial scan to be complete"), AssetManager->HasInitialScanCompleted()))
	{
		return false;
	}

	FPrimaryAssetId InvalidPrimaryAssetId(TEXT("MissingType"), TEXT("MissingName"));
	FSoftObjectPath InvalidPrimaryAssetPath;
	FAssetData EmptyAssetData;

	FPrimaryAssetId ExpectedPrimaryAssetId;
	FSoftObjectPath ExpectedPrimaryAssetPath;
	FAssetData ExpectedPrimaryAssetData;
	const uint32 bHasValidPrimaryAsset = TryResolveValidPrimaryAsset(
		*AssetManager,
		ExpectedPrimaryAssetId,
		ExpectedPrimaryAssetPath,
		ExpectedPrimaryAssetData) ? 1u : 0u;

	const FString Script = TEXT(R"(
void ProbeLoadMethods(UAssetManager AssetManager, const FPrimaryAssetId& AssetToLoad)
{
	TArray<FPrimaryAssetId> AssetsToLoad;
	AssetsToLoad.Add(AssetToLoad);
	TArray<FName> LoadBundles;
	AssetManager.LoadPrimaryAsset(AssetToLoad, LoadBundles);
	AssetManager.LoadPrimaryAssets(AssetsToLoad, LoadBundles);
}

int ProbeUnloadSingle(UAssetManager AssetManager, const FPrimaryAssetId& AssetToUnload)
{
	return AssetManager.UnloadPrimaryAsset(AssetToUnload);
}

int VerifyAssetManagerDirect(
	UAssetManager AssetManager,
	const FPrimaryAssetId& InvalidPrimaryAssetId,
	const FSoftObjectPath& InvalidPrimaryAssetPath,
	const FAssetData& EmptyAssetData,
	uint HasValidPrimaryAsset,
	const FPrimaryAssetId& ExpectedPrimaryAssetId,
	const FSoftObjectPath& ExpectedPrimaryAssetPath,
	const FAssetData& ExpectedPrimaryAssetData)
{
	if (AssetManager == null)
		return 10;
	if (AssetManager.GetPrimaryAssetIdForPath(InvalidPrimaryAssetPath).IsValid())
		return 20;
	if (AssetManager.GetPrimaryAssetPath(InvalidPrimaryAssetId).IsValid())
		return 30;
	if (AssetManager.GetPrimaryAssetIdForData(EmptyAssetData).IsValid())
		return 40;

	TArray<FPrimaryAssetId> EmptyPrimaryAssetIds;
	if (AssetManager.UnloadPrimaryAssets(EmptyPrimaryAssetIds) != 0)
		return 50;

	if (HasValidPrimaryAsset != 0)
	{
		if (!(AssetManager.GetPrimaryAssetIdForPath(ExpectedPrimaryAssetPath) == ExpectedPrimaryAssetId))
			return 60;
		if (!(AssetManager.GetPrimaryAssetPath(ExpectedPrimaryAssetId) == ExpectedPrimaryAssetPath))
			return 70;
		if (!(AssetManager.GetPrimaryAssetIdForData(ExpectedPrimaryAssetData) == ExpectedPrimaryAssetId))
			return 80;
	}

	return 1;
}
)");

	asIScriptModule* Module = BuildModule(*this, Engine, AssetManagerDirectModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyAssetManagerDirect(UAssetManager AssetManager, const FPrimaryAssetId& InvalidPrimaryAssetId, const FSoftObjectPath& InvalidPrimaryAssetPath, const FAssetData& EmptyAssetData, uint HasValidPrimaryAsset, const FPrimaryAssetId& ExpectedPrimaryAssetId, const FSoftObjectPath& ExpectedPrimaryAssetPath, const FAssetData& ExpectedPrimaryAssetData)"),
			[this, AssetManager, &InvalidPrimaryAssetId, &InvalidPrimaryAssetPath, &EmptyAssetData, bHasValidPrimaryAsset, &ExpectedPrimaryAssetId, &ExpectedPrimaryAssetPath, &ExpectedPrimaryAssetData](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, AssetManager, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 1, &InvalidPrimaryAssetId, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 2, &InvalidPrimaryAssetPath, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 3, &EmptyAssetData, TEXT("VerifyAssetManagerDirect"))
					&& SetArgDWordChecked(*this, Context, 4, bHasValidPrimaryAsset, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 5, &ExpectedPrimaryAssetId, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 6, &ExpectedPrimaryAssetPath, TEXT("VerifyAssetManagerDirect"))
					&& SetArgAddressChecked(*this, Context, 7, &ExpectedPrimaryAssetData, TEXT("VerifyAssetManagerDirect"));
			},
			TEXT("VerifyAssetManagerDirect"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UAssetManager direct bindings should preserve invalid-path semantics, valid primary-asset round-trips when available, and keep load/unload overloads script-compilable"),
		Result,
		1);
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptPackageDirtyCompatTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASPackageDirtyCompat"));
	};

	const FName CleanPackageName = MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass(), TEXT("ASPackageClean"));
	const FName DirtyPackageName = MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass(), TEXT("ASPackageDirty"));

	UPackage* CleanPackage = NewObject<UPackage>(GetTransientPackage(), CleanPackageName, RF_Transient);
	UPackage* DirtyPackage = NewObject<UPackage>(GetTransientPackage(), DirtyPackageName, RF_Transient);
	if (!TestNotNull(TEXT("Package dirty compat test should create the clean package"), CleanPackage)
		|| !TestNotNull(TEXT("Package dirty compat test should create the dirty package"), DirtyPackage))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		if (DirtyPackage != nullptr)
		{
			DirtyPackage->SetDirtyFlag(false);
			DirtyPackage->MarkAsGarbage();
		}

		if (CleanPackage != nullptr)
		{
			CleanPackage->SetDirtyFlag(false);
			CleanPackage->MarkAsGarbage();
		}
	};

	CleanPackage->SetDirtyFlag(false);
	DirtyPackage->SetDirtyFlag(false);
	DirtyPackage->SetDirtyFlag(true);
	const bool bExpectedCleanPackageDirty = CleanPackage->IsDirty();
	const bool bExpectedDirtyPackageDirty = DirtyPackage->IsDirty();

	FString Script = TEXT(R"(
int VerifyPackageDirty(UPackage CleanPackage, UPackage DirtyPackage)
{
	if (CleanPackage == null || DirtyPackage == null)
		return 10;
	if (CleanPackage.IsDirty() != __EXPECTED_CLEAN_PACKAGE_DIRTY__)
		return 20;
	if (DirtyPackage.IsDirty() != __EXPECTED_DIRTY_PACKAGE_DIRTY__)
		return 30;

	return 1;
}
)");
	ReplaceToken(Script, TEXT("__EXPECTED_CLEAN_PACKAGE_DIRTY__"), BoolToScriptLiteral(bExpectedCleanPackageDirty));
	ReplaceToken(Script, TEXT("__EXPECTED_DIRTY_PACKAGE_DIRTY__"), BoolToScriptLiteral(bExpectedDirtyPackageDirty));

	asIScriptModule* Module = BuildModule(*this, Engine, PackageDirtyModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyPackageDirty(UPackage CleanPackage, UPackage DirtyPackage)"),
			[this, CleanPackage, DirtyPackage](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, CleanPackage, TEXT("VerifyPackageDirty"))
					&& SetArgObjectChecked(*this, Context, 1, DirtyPackage, TEXT("VerifyPackageDirty"));
			},
			TEXT("VerifyPackageDirty"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UPackage::IsDirty should match the native dirty-flag baselines for transient packages through the script binding"),
		Result,
		1);
	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
