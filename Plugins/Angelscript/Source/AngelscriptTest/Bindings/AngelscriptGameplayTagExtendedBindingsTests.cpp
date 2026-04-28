#include "../Core/AngelscriptGASTestTypes.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Components/ActorTestSpawner.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagsManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptGameplayTagExtendedBindingsTests_Private
{
	static constexpr ANSICHAR GameplayTagExtendedMixinModuleName[] = "ASGameplayTagExtendedMixinCompat";
	static constexpr ANSICHAR GameplayTagPropertyMapModuleName[] = "ASGameplayTagBlueprintPropertyMapApplyCompat";
	static constexpr TCHAR GameplayTagPropertyMapOwnerClassName[] = TEXT("UGameplayTagPropertyMapOwner");

	bool GetValidGameplayTag(FAutomationTestBase& Test, FGameplayTag& OutTag)
	{
		FGameplayTagContainer AllTags;
		UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false);
		if (!Test.TestTrue(TEXT("GameplayTag extended bindings test requires at least one registered gameplay tag"), AllTags.Num() > 0))
		{
			return false;
		}

		OutTag = AllTags.First();
		return Test.TestTrue(TEXT("GameplayTag extended bindings test should resolve a valid gameplay tag fixture"), OutTag.IsValid());
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		asUINT ArgumentIndex,
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

		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
				Context->Prepare(Function),
				static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		if (!Test.TestEqual(
				*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
				Context->Execute(),
				static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	bool AppendGameplayTagPropertyMapping(
		FAutomationTestBase& Test,
		UObject& Owner,
		FName MapPropertyName,
		const FGameplayTag& TagToMap,
		FName TargetPropertyName)
	{
		FStructProperty* MapProperty = FindFProperty<FStructProperty>(Owner.GetClass(), MapPropertyName);
		if (!Test.TestNotNull(
				*FString::Printf(TEXT("GameplayTag property-map fixture should expose struct property '%s'"), *MapPropertyName.ToString()),
				MapProperty))
		{
			return false;
		}

		FArrayProperty* PropertyMappingsProperty = FindFProperty<FArrayProperty>(MapProperty->Struct, TEXT("PropertyMappings"));
		if (!Test.TestNotNull(TEXT("GameplayTag property-map fixture should expose the internal PropertyMappings array"), PropertyMappingsProperty))
		{
			return false;
		}

		void* MapValuePtr = MapProperty->ContainerPtrToValuePtr<void>(&Owner);
		void* PropertyMappingsPtr = PropertyMappingsProperty->ContainerPtrToValuePtr<void>(MapValuePtr);
		FScriptArrayHelper PropertyMappingsHelper(PropertyMappingsProperty, PropertyMappingsPtr);
		const int32 NewIndex = PropertyMappingsHelper.AddValue();

		auto* Mapping = reinterpret_cast<FGameplayTagBlueprintPropertyMapping*>(PropertyMappingsHelper.GetRawPtr(NewIndex));
		if (!Test.TestNotNull(TEXT("GameplayTag property-map fixture should allocate a mapping entry"), Mapping))
		{
			return false;
		}

		Mapping->TagToMap = TagToMap;
		Mapping->PropertyName = TargetPropertyName;
		Mapping->PropertyToEdit = nullptr;
		Mapping->PropertyGuid = FGuid();
		return true;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptGameplayTagExtendedBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGameplayTagExtendedMixinCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GameplayTagExtended.MixinCompileCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGameplayTagBlueprintPropertyMapApplyCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GameplayTagExtended.PropertyMapApplyCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGameplayTagExtendedMixinCompatBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FGameplayTag ValidTag;
	if (!GetValidGameplayTag(*this, ValidTag))
	{
		return false;
	}

	const FString TagName = ValidTag.ToString().ReplaceCharWithEscapedChar();
	const FString Script = FString::Printf(TEXT(R"(
void ProbeGameplayTagQueryMixins()
{
	FGameplayTag ValidTag = FGameplayTag::RequestGameplayTag(FName("%s"), true);
	if (!ValidTag.IsValid())
		return;

	FGameplayTagContainer SingleTagContainer;
	SingleTagContainer.AddTag(ValidTag);
	if (!(SingleTagContainer.First() == ValidTag))
		return;

	FGameplayTagQuery MatchTag = FGameplayTagQuery::MakeQuery_MatchTag(ValidTag);
	if (MatchTag.IsEmpty())
		return;

	bool bMatches = MatchTag.Matches(SingleTagContainer);

	FString Description = MatchTag.GetDescription();
	if (Description.Len() == 0 && bMatches)
	{
		Print("Unreachable gameplay-tag query mixin branch");
	}

}

int Entry()
{
	return 1;
}
)"), *TagName);

	asIScriptModule* Module = BuildModule(*this, Engine, GameplayTagExtendedMixinModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	TestEqual(TEXT("GameplayTag extended mixin helpers should remain script-compilable"), Result, 1);

	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptGameplayTagBlueprintPropertyMapApplyCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASGameplayTagBlueprintPropertyMapApplyCompat"));
		ResetSharedCloneEngine(Engine);
	};

	FGameplayTag ValidTag;
	if (!GetValidGameplayTag(*this, ValidTag))
	{
		return false;
	}

	FString Script = TEXT(R"AS(
UCLASS()
class __OWNER_CLASS__ : UObject
{
	UPROPERTY()
	FGameplayTagBlueprintPropertyMap TagPropertyMap;

	UPROPERTY()
	bool bHasTrackedTag = false;

	UPROPERTY()
	int TrackedTagCount = -1;
}

int ApplyPropertyMap(__OWNER_CLASS__ Owner, UAbilitySystemComponent ASC)
{
	Owner.TagPropertyMap.Initialize(Owner, ASC);
	Owner.TagPropertyMap.ApplyCurrentTags();

	int ResultMask = 0;
	if (Owner.bHasTrackedTag)
		ResultMask |= 1;
	if (Owner.TrackedTagCount == 1)
		ResultMask |= 2;
	return ResultMask;
}

int RefreshPropertyMap(__OWNER_CLASS__ Owner)
{
	Owner.TagPropertyMap.ApplyCurrentTags();

	int ResultMask = 0;
	if (!Owner.bHasTrackedTag)
		ResultMask |= 1;
	if (Owner.TrackedTagCount == 0)
		ResultMask |= 2;
	return ResultMask;
}
)AS");
	Script.ReplaceInline(TEXT("__OWNER_CLASS__"), GameplayTagPropertyMapOwnerClassName, ESearchCase::CaseSensitive);

	if (!TestTrue(
			TEXT("GameplayTagBlueprintPropertyMapApplyCompat should compile the script owner fixture"),
			CompileAnnotatedModuleFromMemory(
				&Engine,
				FName(GameplayTagPropertyMapModuleName),
				FString(GameplayTagPropertyMapModuleName) + TEXT(".as"),
				Script)))
	{
		return false;
	}

	UClass* OwnerClass = FindGeneratedClass(&Engine, FName(GameplayTagPropertyMapOwnerClassName));
	if (!TestNotNull(TEXT("GameplayTagBlueprintPropertyMapApplyCompat should publish the script owner class"), OwnerClass))
	{
		return false;
	}

	UObject* OwnerObject = NewObject<UObject>(GetTransientPackage(), OwnerClass);
	if (!TestNotNull(TEXT("GameplayTagBlueprintPropertyMapApplyCompat should instantiate the script owner object"), OwnerObject))
	{
		return false;
	}

	if (!AppendGameplayTagPropertyMapping(*this, *OwnerObject, TEXT("TagPropertyMap"), ValidTag, TEXT("bHasTrackedTag"))
		|| !AppendGameplayTagPropertyMapping(*this, *OwnerObject, TEXT("TagPropertyMap"), ValidTag, TEXT("TrackedTagCount")))
	{
		return false;
	}

	FActorTestSpawner Spawner;
	Spawner.InitializeGameSubsystems();

	AAngelscriptGASTestActor* TestActor = &Spawner.SpawnActor<AAngelscriptGASTestActor>();
	if (!TestNotNull(TEXT("GameplayTagBlueprintPropertyMapApplyCompat should spawn the ASC fixture actor"), TestActor))
	{
		return false;
	}

	UAngelscriptAbilitySystemComponent* AbilitySystemComponent = TestActor->AbilitySystemComponent;
	if (!TestNotNull(TEXT("GameplayTagBlueprintPropertyMapApplyCompat should expose an ability-system component"), AbilitySystemComponent))
	{
		return false;
	}

	AbilitySystemComponent->InitAbilityActorInfo(TestActor, TestActor);
	AbilitySystemComponent->AddLooseGameplayTag(ValidTag);

	asIScriptModule* Module = Engine.GetScriptEngine()->GetModule(GameplayTagPropertyMapModuleName, asGM_ONLY_IF_EXISTS);
	if (!TestNotNull(TEXT("GameplayTagBlueprintPropertyMapApplyCompat should register its script module"), Module))
	{
		return false;
	}

	int32 ApplyResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int ApplyPropertyMap(UGameplayTagPropertyMapOwner, UAbilitySystemComponent)"),
			[this, OwnerObject, AbilitySystemComponent](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, OwnerObject, TEXT("ApplyPropertyMap"))
					&& SetArgObjectChecked(*this, Context, 1, AbilitySystemComponent, TEXT("ApplyPropertyMap"));
			},
			TEXT("ApplyPropertyMap"),
			ApplyResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GameplayTagBlueprintPropertyMap.Initialize(valid, valid) should let script ApplyCurrentTags populate bool and count properties"),
		ApplyResultMask,
		3);

	AbilitySystemComponent->RemoveLooseGameplayTag(ValidTag);

	int32 RefreshResultMask = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int RefreshPropertyMap(UGameplayTagPropertyMapOwner)"),
			[this, OwnerObject](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, OwnerObject, TEXT("RefreshPropertyMap"));
			},
			TEXT("RefreshPropertyMap"),
			RefreshResultMask))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GameplayTagBlueprintPropertyMap.ApplyCurrentTags should refresh the mapped bool/count properties after the tracked tag is removed"),
		RefreshResultMask,
		3);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
