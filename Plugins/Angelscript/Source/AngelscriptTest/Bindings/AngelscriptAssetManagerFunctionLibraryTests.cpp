#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestUtilities.h"
#include "FunctionLibraries/UAssetManagerMixinLibrary.h"

#include "Engine/AssetManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptAssetManagerFunctionLibraryTests_Private
{
	static constexpr TCHAR AssetManagerGuardFilename[] = TEXT("ASAssetManagerNullAndInvalidCallbackGuards.as");
	static constexpr TCHAR AssetManagerQueryFilename[] = TEXT("ASAssetManagerQueryAndScan.as");
	static constexpr TCHAR ValidReceiverClassName[] = TEXT("UAssetManagerValidScanReceiver");
	static constexpr TCHAR MissingReceiverClassName[] = TEXT("UAssetManagerMissingScanReceiver");
	static constexpr TCHAR QueryReceiverClassName[] = TEXT("UAssetManagerQueryScanReceiver");
	static constexpr TCHAR CallbackCountPropertyName[] = TEXT("CallbackCount");

	bool ReadIntPropertyChecked(
		FAutomationTestBase& Test,
		UObject& Object,
		FName PropertyName,
		const TCHAR* ContextLabel,
		int32& OutValue)
	{
		FIntProperty* Property = FindFProperty<FIntProperty>(Object.GetClass(), PropertyName);
		if (!Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose int property '%s'"), ContextLabel, *PropertyName.ToString()),
			Property))
		{
			return false;
		}

		OutValue = Property->GetPropertyValue_InContainer(&Object);
		return true;
	}

	bool DoesAssetDataMatchIdentity(const FAssetData& Candidate, const FAssetData& Expected)
	{
		return Candidate.IsValid()
			&& Expected.IsValid()
			&& Candidate.GetSoftObjectPath() == Expected.GetSoftObjectPath()
			&& Candidate.PackageName == Expected.PackageName
			&& Candidate.AssetName == Expected.AssetName
			&& Candidate.AssetClassPath == Expected.AssetClassPath;
	}

	bool ContainsAssetDataByIdentity(const TArray<FAssetData>& AssetDataList, const FAssetData& Expected)
	{
		for (const FAssetData& Candidate : AssetDataList)
		{
			if (DoesAssetDataMatchIdentity(Candidate, Expected))
			{
				return true;
			}
		}
		return false;
	}

	bool ContainsPrimaryAssetTypeInfo(const TArray<FPrimaryAssetTypeInfo>& AssetTypeInfoList, const FPrimaryAssetType& ExpectedType)
	{
		for (const FPrimaryAssetTypeInfo& AssetTypeInfo : AssetTypeInfoList)
		{
			if (AssetTypeInfo.PrimaryAssetType == ExpectedType)
			{
				return true;
			}
		}
		return false;
	}

	bool TryResolveValidPrimaryAsset(
		UAssetManager& AssetManager,
		FPrimaryAssetType& OutPrimaryAssetType,
		FPrimaryAssetId& OutPrimaryAssetId,
		FAssetData& OutPrimaryAssetData,
		FPrimaryAssetTypeInfo& OutPrimaryAssetTypeInfo)
	{
		TArray<FPrimaryAssetTypeInfo> AssetTypeInfoList;
		AssetManager.GetPrimaryAssetTypeInfoList(AssetTypeInfoList);
		for (const FPrimaryAssetTypeInfo& AssetTypeInfo : AssetTypeInfoList)
		{
			if (!AssetTypeInfo.PrimaryAssetType.IsValid())
			{
				continue;
			}

			TArray<FPrimaryAssetId> PrimaryAssetIdList;
			if (!AssetManager.GetPrimaryAssetIdList(AssetTypeInfo.PrimaryAssetType, PrimaryAssetIdList))
			{
				continue;
			}

			for (const FPrimaryAssetId& PrimaryAssetId : PrimaryAssetIdList)
			{
				if (!PrimaryAssetId.IsValid())
				{
					continue;
				}

				FAssetData PrimaryAssetData;
				if (!AssetManager.GetPrimaryAssetData(PrimaryAssetId, PrimaryAssetData) || !PrimaryAssetData.IsValid())
				{
					continue;
				}

				OutPrimaryAssetType = AssetTypeInfo.PrimaryAssetType;
				OutPrimaryAssetId = PrimaryAssetId;
				OutPrimaryAssetData = PrimaryAssetData;
				OutPrimaryAssetTypeInfo = AssetTypeInfo;
				return true;
			}
		}

		return false;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptAssetManagerFunctionLibraryTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAssetManagerNullAndInvalidCallbackGuardsTest,
	"Angelscript.TestModule.FunctionLibraries.AssetManagerNullAndInvalidCallbackGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptAssetManagerNullAndInvalidCallbackGuardsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASAssetManagerNullAndInvalidCallbackGuards"));
	};

	const FString ReceiverScript = TEXT(R"AS(
UCLASS()
class UAssetManagerValidScanReceiver : UObject
{
	UPROPERTY()
	int CallbackCount;

	UFUNCTION()
	void OnScanComplete()
	{
		CallbackCount += 1;
	}
}

UCLASS()
class UAssetManagerMissingScanReceiver : UObject
{
	UPROPERTY()
	int CallbackCount;

	UFUNCTION()
	void DifferentFunction()
	{
		CallbackCount += 1;
	}
}
)AS");

	if (!TestTrue(
			TEXT("AssetManager guard test should compile the callback receiver module"),
			CompileAnnotatedModuleFromMemory(&Engine, TEXT("ASAssetManagerNullAndInvalidCallbackGuards"), AssetManagerGuardFilename, ReceiverScript)))
	{
		return false;
	}

	UClass* ValidReceiverClass = FindGeneratedClass(&Engine, ValidReceiverClassName);
	UClass* MissingReceiverClass = FindGeneratedClass(&Engine, MissingReceiverClassName);
	if (!TestNotNull(TEXT("AssetManager guard test should generate the valid callback receiver class"), ValidReceiverClass)
		|| !TestNotNull(TEXT("AssetManager guard test should generate the missing-callback receiver class"), MissingReceiverClass))
	{
		return false;
	}

	UObject* ValidReceiver = NewObject<UObject>(GetTransientPackage(), ValidReceiverClass);
	UObject* MissingReceiver = NewObject<UObject>(GetTransientPackage(), MissingReceiverClass);
	if (!TestNotNull(TEXT("AssetManager guard test should instantiate the valid callback receiver"), ValidReceiver)
		|| !TestNotNull(TEXT("AssetManager guard test should instantiate the missing-callback receiver"), MissingReceiver))
	{
		return false;
	}

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!TestNotNull(TEXT("AssetManager guard test should resolve an initialized asset manager"), AssetManager))
	{
		return false;
	}

	if (!TestTrue(TEXT("AssetManager guard test requires the asset manager initial scan to be complete"), AssetManager->HasInitialScanCompleted()))
	{
		return false;
	}

	const FPrimaryAssetId DummyPrimaryAssetId(TEXT("MissingType"), TEXT("MissingName"));
	const FPrimaryAssetType DummyPrimaryAssetType(TEXT("MissingType"));

	FAssetData AssetData;
	TArray<FAssetData> AssetDataList;
	AssetDataList.AddDefaulted();
	TArray<FPrimaryAssetId> PrimaryAssetIdList;
	PrimaryAssetIdList.Add(DummyPrimaryAssetId);
	FPrimaryAssetTypeInfo AssetTypeInfo(FName(TEXT("DirtyType")), UObject::StaticClass(), false, false);
	TArray<FPrimaryAssetTypeInfo> AssetTypeInfoList;
	AssetTypeInfoList.Add(FPrimaryAssetTypeInfo(FName(TEXT("DirtyListType")), UObject::StaticClass(), false, false));
	const FPrimaryAssetRules DefaultRules;
	const FPrimaryAssetTypeInfo DefaultTypeInfo;

	if (!TestFalse(
			TEXT("Null asset manager should fail GetPrimaryAssetData"),
			UAssetManagerMixinLibrary::GetPrimaryAssetData(nullptr, DummyPrimaryAssetId, AssetData))
		|| !TestFalse(
			TEXT("Null asset manager should fail GetPrimaryAssetDataList"),
			UAssetManagerMixinLibrary::GetPrimaryAssetDataList(nullptr, DummyPrimaryAssetType, AssetDataList))
		|| !TestNull(
			TEXT("Null asset manager should return null from GetPrimaryAssetObject"),
			UAssetManagerMixinLibrary::GetPrimaryAssetObject(nullptr, DummyPrimaryAssetId))
		|| !TestFalse(
			TEXT("Null asset manager should return an invalid primary asset id for objects"),
			UAssetManagerMixinLibrary::GetPrimaryAssetIdForObject(nullptr, ValidReceiver).IsValid())
		|| !TestFalse(
			TEXT("Null asset manager should fail GetPrimaryAssetIdList"),
			UAssetManagerMixinLibrary::GetPrimaryAssetIdList(nullptr, DummyPrimaryAssetType, PrimaryAssetIdList))
		|| !TestFalse(
			TEXT("Null asset manager should fail GetPrimaryAssetTypeInfo"),
			UAssetManagerMixinLibrary::GetPrimaryAssetTypeInfo(nullptr, DummyPrimaryAssetType, AssetTypeInfo)))
	{
		return false;
	}

	UAssetManagerMixinLibrary::GetPrimaryAssetTypeInfoList(nullptr, AssetTypeInfoList);
	const FPrimaryAssetRules Rules = UAssetManagerMixinLibrary::GetPrimaryAssetRules(nullptr, DummyPrimaryAssetId);

	if (!TestFalse(TEXT("Null asset manager should leave asset data invalid"), AssetData.IsValid())
		|| !TestEqual(TEXT("Null asset manager should clear the asset data list"), AssetDataList.Num(), 0)
		|| !TestEqual(TEXT("Null asset manager should clear the primary asset id list"), PrimaryAssetIdList.Num(), 0)
		|| !TestEqual(TEXT("Null asset manager should reset the primary asset type info type"), AssetTypeInfo.PrimaryAssetType, DefaultTypeInfo.PrimaryAssetType)
		|| !TestTrue(TEXT("Null asset manager should reset the primary asset type info base class to UObject"), AssetTypeInfo.AssetBaseClassLoaded.Get() == UObject::StaticClass())
		|| !TestEqual(TEXT("Null asset manager should reset the primary asset type info dynamic flag"), AssetTypeInfo.bIsDynamicAsset, DefaultTypeInfo.bIsDynamicAsset)
		|| !TestEqual(TEXT("Null asset manager should reset the primary asset type info asset count"), AssetTypeInfo.NumberOfAssets, DefaultTypeInfo.NumberOfAssets)
		|| !TestEqual(TEXT("Null asset manager should reset the primary asset type info scan paths"), AssetTypeInfo.AssetScanPaths.Num(), DefaultTypeInfo.AssetScanPaths.Num())
		|| !TestEqual(TEXT("Null asset manager should reset the primary asset type info rules"), AssetTypeInfo.Rules, DefaultRules)
		|| !TestEqual(TEXT("Null asset manager should clear the asset type info list"), AssetTypeInfoList.Num(), 0)
		|| !TestEqual(TEXT("Null asset manager should return default primary asset rules"), Rules, DefaultRules))
	{
		return false;
	}

	int32 ValidCallbackCount = INDEX_NONE, MissingCallbackCount = INDEX_NONE;
	if (!ReadIntPropertyChecked(*this, *ValidReceiver, CallbackCountPropertyName, TEXT("Valid asset manager receiver"), ValidCallbackCount)
		|| !ReadIntPropertyChecked(*this, *MissingReceiver, CallbackCountPropertyName, TEXT("Missing asset manager receiver"), MissingCallbackCount))
	{
		return false;
	}

	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(nullptr, ValidReceiver, TEXT("OnScanComplete"));
	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, nullptr, TEXT("OnScanComplete"));
	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, MissingReceiver, TEXT("OnScanComplete"));
	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, ValidReceiver, TEXT("DoesNotExist"));

	if (!ReadIntPropertyChecked(*this, *ValidReceiver, CallbackCountPropertyName, TEXT("Valid asset manager receiver after invalid callbacks"), ValidCallbackCount)
		|| !ReadIntPropertyChecked(*this, *MissingReceiver, CallbackCountPropertyName, TEXT("Missing asset manager receiver after invalid callbacks"), MissingCallbackCount))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("Invalid asset manager callback inputs should not trigger the valid receiver"),
			ValidCallbackCount,
			0)
		|| !TestEqual(
			TEXT("Invalid asset manager callback inputs should not trigger the missing-callback receiver"),
			MissingCallbackCount,
			0))
	{
		return false;
	}

	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, ValidReceiver, TEXT("OnScanComplete"));
	if (!ReadIntPropertyChecked(*this, *ValidReceiver, CallbackCountPropertyName, TEXT("Valid asset manager receiver after baseline callback"), ValidCallbackCount))
	{
		return false;
	}

	TestEqual(TEXT("Valid asset manager callback path should still trigger exactly once"), ValidCallbackCount, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptAssetManagerQueryAndScanTest,
	"Angelscript.TestModule.FunctionLibraries.AssetManagerQueryAndScan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptAssetManagerQueryAndScanTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	bool bCallbackOnlyFiresOncePerExplicitRegistration = false;
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASAssetManagerQueryAndScan"));
	};

	const FString ReceiverScript = TEXT(R"AS(
UCLASS()
class UAssetManagerQueryScanReceiver : UObject
{
	UPROPERTY()
	int CallbackCount;

	UFUNCTION()
	void OnInitialScanComplete()
	{
		CallbackCount += 1;
	}
}
)AS");

	if (!TestTrue(
			TEXT("AssetManager query test should compile the callback receiver module"),
			CompileAnnotatedModuleFromMemory(&Engine, TEXT("ASAssetManagerQueryAndScan"), AssetManagerQueryFilename, ReceiverScript)))
	{
		return false;
	}

	UClass* QueryReceiverClass = FindGeneratedClass(&Engine, QueryReceiverClassName);
	if (!TestNotNull(TEXT("AssetManager query test should generate the scan receiver class"), QueryReceiverClass))
	{
		return false;
	}

	UObject* QueryReceiver = NewObject<UObject>(GetTransientPackage(), QueryReceiverClass);
	if (!TestNotNull(TEXT("AssetManager query test should instantiate the scan receiver"), QueryReceiver))
	{
		return false;
	}

	UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
	if (!TestNotNull(TEXT("AssetManager query test should resolve an initialized asset manager"), AssetManager))
	{
		return false;
	}

	if (!TestTrue(TEXT("AssetManager query test requires the asset manager initial scan to be complete"), AssetManager->HasInitialScanCompleted()))
	{
		return false;
	}

	const FPrimaryAssetId InvalidPrimaryAssetId(TEXT("InvalidType"), TEXT("MissingAsset"));
	const FPrimaryAssetType InvalidPrimaryAssetType(TEXT("InvalidType"));
	FAssetData InvalidAssetData;
	TArray<FAssetData> InvalidAssetDataList;
	InvalidAssetDataList.AddDefaulted();
	TArray<FPrimaryAssetId> InvalidPrimaryAssetIdList;
	InvalidPrimaryAssetIdList.Add(InvalidPrimaryAssetId);
	FPrimaryAssetTypeInfo InvalidAssetTypeInfo(FName(TEXT("DirtyType")), UObject::StaticClass(), false, false);
	const FPrimaryAssetRules DefaultRules;

	if (!TestFalse(
			TEXT("Invalid primary asset id should fail GetPrimaryAssetData"),
			UAssetManagerMixinLibrary::GetPrimaryAssetData(AssetManager, InvalidPrimaryAssetId, InvalidAssetData))
		|| !TestFalse(TEXT("Invalid primary asset id should leave asset data invalid"), InvalidAssetData.IsValid())
		|| !TestNull(
			TEXT("Invalid primary asset id should return null from GetPrimaryAssetObject"),
			UAssetManagerMixinLibrary::GetPrimaryAssetObject(AssetManager, InvalidPrimaryAssetId))
		|| !TestFalse(
			TEXT("Non-primary receiver object should not resolve to a valid primary asset id"),
			UAssetManagerMixinLibrary::GetPrimaryAssetIdForObject(AssetManager, QueryReceiver).IsValid())
		|| !TestFalse(
			TEXT("Invalid primary asset type should fail GetPrimaryAssetDataList"),
			UAssetManagerMixinLibrary::GetPrimaryAssetDataList(AssetManager, InvalidPrimaryAssetType, InvalidAssetDataList))
		|| !TestEqual(TEXT("Invalid primary asset type should clear the asset data list"), InvalidAssetDataList.Num(), 0)
		|| !TestFalse(
			TEXT("Invalid primary asset type should fail GetPrimaryAssetIdList"),
			UAssetManagerMixinLibrary::GetPrimaryAssetIdList(AssetManager, InvalidPrimaryAssetType, InvalidPrimaryAssetIdList))
		|| !TestEqual(TEXT("Invalid primary asset type should clear the primary asset id list"), InvalidPrimaryAssetIdList.Num(), 0)
		|| !TestFalse(
			TEXT("Invalid primary asset type should fail GetPrimaryAssetTypeInfo"),
			UAssetManagerMixinLibrary::GetPrimaryAssetTypeInfo(AssetManager, InvalidPrimaryAssetType, InvalidAssetTypeInfo))
		|| !TestEqual(
			TEXT("Invalid primary asset id should return default primary asset rules"),
			UAssetManagerMixinLibrary::GetPrimaryAssetRules(AssetManager, InvalidPrimaryAssetId),
			DefaultRules))
	{
		return false;
	}

	FPrimaryAssetType ExpectedPrimaryAssetType;
	FPrimaryAssetId ExpectedPrimaryAssetId;
	FAssetData ExpectedPrimaryAssetData;
	FPrimaryAssetTypeInfo ExpectedPrimaryAssetTypeInfo;
	if (TryResolveValidPrimaryAsset(
			*AssetManager,
			ExpectedPrimaryAssetType,
			ExpectedPrimaryAssetId,
			ExpectedPrimaryAssetData,
			ExpectedPrimaryAssetTypeInfo))
	{
		TArray<FPrimaryAssetId> WrapperPrimaryAssetIdList;
		TArray<FPrimaryAssetId> NativePrimaryAssetIdList;
		const bool bWrapperHasPrimaryAssetIdList =
			UAssetManagerMixinLibrary::GetPrimaryAssetIdList(AssetManager, ExpectedPrimaryAssetType, WrapperPrimaryAssetIdList);
		const bool bNativeHasPrimaryAssetIdList = AssetManager->GetPrimaryAssetIdList(ExpectedPrimaryAssetType, NativePrimaryAssetIdList);

		TArray<FAssetData> WrapperPrimaryAssetDataList;
		TArray<FAssetData> NativePrimaryAssetDataList;
		const bool bWrapperHasPrimaryAssetDataList =
			UAssetManagerMixinLibrary::GetPrimaryAssetDataList(AssetManager, ExpectedPrimaryAssetType, WrapperPrimaryAssetDataList);
		const bool bNativeHasPrimaryAssetDataList = AssetManager->GetPrimaryAssetDataList(ExpectedPrimaryAssetType, NativePrimaryAssetDataList);

		FPrimaryAssetTypeInfo WrapperPrimaryAssetTypeInfo;
		FPrimaryAssetTypeInfo NativePrimaryAssetTypeInfo;
		const bool bWrapperHasPrimaryAssetTypeInfo =
			UAssetManagerMixinLibrary::GetPrimaryAssetTypeInfo(AssetManager, ExpectedPrimaryAssetType, WrapperPrimaryAssetTypeInfo);
		const bool bNativeHasPrimaryAssetTypeInfo = AssetManager->GetPrimaryAssetTypeInfo(ExpectedPrimaryAssetType, NativePrimaryAssetTypeInfo);

		TArray<FPrimaryAssetTypeInfo> WrapperPrimaryAssetTypeInfoList;
		TArray<FPrimaryAssetTypeInfo> NativePrimaryAssetTypeInfoList;
		UAssetManagerMixinLibrary::GetPrimaryAssetTypeInfoList(AssetManager, WrapperPrimaryAssetTypeInfoList);
		AssetManager->GetPrimaryAssetTypeInfoList(NativePrimaryAssetTypeInfoList);

		const FPrimaryAssetRules WrapperPrimaryAssetRules =
			UAssetManagerMixinLibrary::GetPrimaryAssetRules(AssetManager, ExpectedPrimaryAssetId);
		const FPrimaryAssetRules NativePrimaryAssetRules = AssetManager->GetPrimaryAssetRules(ExpectedPrimaryAssetId);

		if (!TestEqual(
				TEXT("Wrapper primary asset id list query should match the native success result"),
				bWrapperHasPrimaryAssetIdList,
				bNativeHasPrimaryAssetIdList)
			|| !TestEqual(
				TEXT("Wrapper primary asset id list query should match the native count"),
				WrapperPrimaryAssetIdList.Num(),
				NativePrimaryAssetIdList.Num())
			|| !TestTrue(
				TEXT("Wrapper primary asset id list should contain the resolved sample asset id"),
				WrapperPrimaryAssetIdList.Contains(ExpectedPrimaryAssetId))
			|| !TestEqual(
				TEXT("Wrapper primary asset data list query should match the native success result"),
				bWrapperHasPrimaryAssetDataList,
				bNativeHasPrimaryAssetDataList)
			|| !TestEqual(
				TEXT("Wrapper primary asset data list query should match the native count"),
				WrapperPrimaryAssetDataList.Num(),
				NativePrimaryAssetDataList.Num())
			|| !TestTrue(
				TEXT("Wrapper primary asset data list should contain the resolved sample asset data"),
				ContainsAssetDataByIdentity(WrapperPrimaryAssetDataList, ExpectedPrimaryAssetData))
			|| !TestEqual(
				TEXT("Wrapper primary asset type info query should match the native success result"),
				bWrapperHasPrimaryAssetTypeInfo,
				bNativeHasPrimaryAssetTypeInfo)
			|| !TestTrue(
				TEXT("Wrapper primary asset type info should preserve the asset type"),
				WrapperPrimaryAssetTypeInfo.PrimaryAssetType == NativePrimaryAssetTypeInfo.PrimaryAssetType)
			|| !TestTrue(
				TEXT("Wrapper primary asset type info should preserve the asset base class"),
				WrapperPrimaryAssetTypeInfo.AssetBaseClassLoaded.Get() == NativePrimaryAssetTypeInfo.AssetBaseClassLoaded.Get())
			|| !TestEqual(
				TEXT("Wrapper primary asset type info should preserve the asset count"),
				WrapperPrimaryAssetTypeInfo.NumberOfAssets,
				NativePrimaryAssetTypeInfo.NumberOfAssets)
			|| !TestEqual(
				TEXT("Wrapper primary asset type info should preserve the scan-path count"),
				WrapperPrimaryAssetTypeInfo.AssetScanPaths.Num(),
				NativePrimaryAssetTypeInfo.AssetScanPaths.Num())
			|| !TestEqual(
				TEXT("Wrapper primary asset type info should preserve rules"),
				WrapperPrimaryAssetTypeInfo.Rules,
				NativePrimaryAssetTypeInfo.Rules)
			|| !TestEqual(
				TEXT("Wrapper primary asset type info list should match the native count"),
				WrapperPrimaryAssetTypeInfoList.Num(),
				NativePrimaryAssetTypeInfoList.Num())
			|| !TestTrue(
				TEXT("Wrapper primary asset type info list should contain the resolved sample asset type"),
				ContainsPrimaryAssetTypeInfo(WrapperPrimaryAssetTypeInfoList, ExpectedPrimaryAssetType))
			|| !TestEqual(
				TEXT("Wrapper primary asset rules should match the native rules for the resolved sample asset"),
				WrapperPrimaryAssetRules,
				NativePrimaryAssetRules))
		{
			return false;
		}

		UObject* WrapperPrimaryAssetObject = UAssetManagerMixinLibrary::GetPrimaryAssetObject(AssetManager, ExpectedPrimaryAssetId);
		UObject* NativePrimaryAssetObject = AssetManager->GetPrimaryAssetObject(ExpectedPrimaryAssetId);
		if (!TestTrue(
				TEXT("Wrapper primary asset object lookup should mirror the native in-memory object result"),
				WrapperPrimaryAssetObject == NativePrimaryAssetObject))
		{
			return false;
		}

		if (WrapperPrimaryAssetObject != nullptr
			&& !TestTrue(
				TEXT("Wrapper primary asset id lookup should round-trip the resolved in-memory primary asset object"),
				UAssetManagerMixinLibrary::GetPrimaryAssetIdForObject(AssetManager, WrapperPrimaryAssetObject) == ExpectedPrimaryAssetId))
		{
			return false;
		}
	}
	else
	{
		AddInfo(TEXT("AssetManager query test skipped happy-path parity because no scanned primary asset was available in this project configuration."));
	}

	int32 CallbackCount = INDEX_NONE;
	if (!ReadIntPropertyChecked(*this, *QueryReceiver, CallbackCountPropertyName, TEXT("AssetManager query receiver before callback"), CallbackCount))
	{
		return false;
	}

	if (!TestEqual(TEXT("AssetManager query receiver should start with zero callback invocations"), CallbackCount, 0))
	{
		return false;
	}

	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, QueryReceiver, TEXT("OnInitialScanComplete"));
	if (!ReadIntPropertyChecked(*this, *QueryReceiver, CallbackCountPropertyName, TEXT("AssetManager query receiver after first callback"), CallbackCount))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("Completed initial scan should invoke the receiver immediately and exactly once"),
			CallbackCount,
			1))
	{
		return false;
	}

	if (!ReadIntPropertyChecked(*this, *QueryReceiver, CallbackCountPropertyName, TEXT("AssetManager query receiver after repeated read"), CallbackCount))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("Repeated reads after the first callback should not reveal a latent duplicate invocation"),
			CallbackCount,
			1))
	{
		return false;
	}

	UAssetManagerMixinLibrary::CallOrRegister_OnCompletedInitialScan(AssetManager, QueryReceiver, TEXT("OnInitialScanComplete"));
	if (!ReadIntPropertyChecked(*this, *QueryReceiver, CallbackCountPropertyName, TEXT("AssetManager query receiver after second explicit callback"), CallbackCount))
	{
		return false;
	}

	bCallbackOnlyFiresOncePerExplicitRegistration = TestEqual(
		TEXT("Each explicit completed-scan registration should contribute at most one immediate callback"),
		CallbackCount,
		2);

	ASTEST_END_SHARE_CLEAN
	return bCallbackOnlyFiresOncePerExplicitRegistration;
}

#endif
