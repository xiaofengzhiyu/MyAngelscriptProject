#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptGASCoreBindingsTests_Private
{
	static constexpr TCHAR GASCoreValueModuleName[] = TEXT("ASGASCoreValueCompat");
	static constexpr TCHAR GASCoreTypeFixtureModuleName[] = TEXT("ASGASCoreTypeCompileCompat");
	static constexpr TCHAR GASCoreValueAbilityClassName[] = TEXT("UGASCoreValueBindingAbility");
	static constexpr TCHAR GASCoreValueAttributeSetClassName[] = TEXT("UGASCoreValueBindingAttributeSet");
	static constexpr TCHAR GASCoreTypeAbilityClassName[] = TEXT("UGASCoreTypeBindingAbility");
	static constexpr TCHAR GASCoreTypeAttributeSetClassName[] = TEXT("UGASCoreTypeBindingAttributeSet");

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

}

using namespace AngelscriptTest_Bindings_AngelscriptGASCoreBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGASCoreValueCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GASCoreValueCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGASCoreTypeCompileCompatBindingsTest,
	"Angelscript.TestModule.Bindings.GASCoreTypeCompileCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGASCoreValueCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(GASCoreValueModuleName);
	};

	FString Script = TEXT(R"AS(
UCLASS()
class __ABILITY_CLASS__ : UAngelscriptGASAbility
{
}

UCLASS()
class __ATTRIBUTE_SET_CLASS__ : UAngelscriptAttributeSet
{
	UPROPERTY()
	FAngelscriptGameplayAttributeData Health;
}

int VerifyGASCoreValueCompat()
{
	UClass AttributeSetClass = __ATTRIBUTE_SET_CLASS__::StaticClass();
	if (AttributeSetClass == null)
		return 10;

	FGameplayAttribute MissingAttribute;
	if (MissingAttribute.IsValid())
		return 20;
	if (MissingAttribute.GetAttributeSetClass() != null)
		return 30;

	FGameplayAttribute HealthAttribute;
	if (!UAngelscriptAttributeSet::TryGetGameplayAttribute(AttributeSetClass, n"Health", HealthAttribute))
		return 40;
	if (!HealthAttribute.IsValid())
		return 50;
	if (HealthAttribute.GetAttributeSetClass() != AttributeSetClass)
		return 60;

	FGameplayAttribute MissingNamedAttribute;
	if (UAngelscriptAttributeSet::TryGetGameplayAttribute(AttributeSetClass, n"MissingAttr", MissingNamedAttribute))
		return 70;
	if (MissingNamedAttribute.IsValid())
		return 80;

	UGameplayEffect EffectDef = Cast<UGameplayEffect>(UGameplayEffect::StaticClass().GetDefaultObject());
	if (EffectDef == null)
		return 85;

	FGameplayEffectContextHandle EffectContext;
	FGameplayEffectSpec EffectSpec(EffectDef, EffectContext, 2.0f);
	if (EffectSpec.Def != EffectDef)
		return 90;
	if (EffectSpec.SetByCallerNameMagnitudes.Num() != 0 || EffectSpec.SetByCallerTagMagnitudes.Num() != 0)
		return 100;

	EffectSpec.SetByCallerNameMagnitudes.Add(n"Damage", 42.0f);
	if (EffectSpec.SetByCallerNameMagnitudes.Num() != 1)
		return 110;

	FGameplayEffectSpec EffectCopy(EffectSpec, EffectContext);
	if (EffectCopy.Def != EffectDef)
		return 120;
	if (EffectCopy.SetByCallerNameMagnitudes.Num() != 1)
		return 130;

	TSubclassOf<UGameplayAbility> AbilityClass = __ABILITY_CLASS__::StaticClass();
	if (!AbilityClass.IsValid())
		return 140;

	UObject SourceObject = UObject::StaticClass().GetDefaultObject();
	if (SourceObject == null)
		return 145;

	FGameplayAbilitySpec AbilityClassSpec(AbilityClass, 3, 7, SourceObject);
	if (AbilityClassSpec.Level != 3 || AbilityClassSpec.InputID != 7)
		return 150;
	if (AbilityClassSpec.SourceObject.Get() != SourceObject)
		return 160;

	AbilityClassSpec.SetbInputPressed(true);
	AbilityClassSpec.SetbPendingRemove(true);
	AbilityClassSpec.SetbActivateOnce(true);
	if (!AbilityClassSpec.GetbInputPressed() || !AbilityClassSpec.GetbPendingRemove() || !AbilityClassSpec.GetbActivateOnce())
		return 170;

	UGameplayAbility AbilityTemplate = AbilityClass.GetDefaultObject();
	if (AbilityTemplate == null)
		return 180;

	FGameplayAbilitySpec AbilityObjectSpec(AbilityTemplate, 4, 9, SourceObject);
	if (AbilityObjectSpec.Ability != AbilityTemplate)
		return 190;
	if (AbilityObjectSpec.Level != 4 || AbilityObjectSpec.InputID != 9)
		return 200;
	if (AbilityObjectSpec.SourceObject.Get() != SourceObject)
		return 210;
	if (AbilityObjectSpec.GetDebugString().Len() == 0)
		return 220;

	return 1;
}
)AS");
	ReplaceToken(Script, TEXT("__ABILITY_CLASS__"), GASCoreValueAbilityClassName);
	ReplaceToken(Script, TEXT("__ATTRIBUTE_SET_CLASS__"), GASCoreValueAttributeSetClassName);

	if (!TestTrue(
			TEXT("GASCoreValueCompat should compile annotated GAS core value fixtures"),
			CompileAnnotatedModuleFromMemory(
				&Engine,
				FName(GASCoreValueModuleName),
				FString(GASCoreValueModuleName) + TEXT(".as"),
				Script)))
	{
		return false;
	}

	UClass* AbilityClass = FindGeneratedClass(&Engine, FName(GASCoreValueAbilityClassName));
	UClass* AttributeSetClass = FindGeneratedClass(&Engine, FName(GASCoreValueAttributeSetClassName));
	if (!TestNotNull(TEXT("GASCoreValueCompat should publish the scripted GAS ability class"), AbilityClass)
		|| !TestNotNull(TEXT("GASCoreValueCompat should publish the scripted GAS attribute-set class"), AttributeSetClass))
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!TestTrue(
			TEXT("GASCoreValueCompat should execute its annotated verification entry point"),
			ExecuteIntFunction(
				&Engine,
				FName(GASCoreValueModuleName),
				TEXT("int VerifyGASCoreValueCompat()"),
				Result)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GAS core binding values should preserve positive FGameplayAttribute, FGameplayEffectSpec, and FGameplayAbilitySpec compatibility"),
		Result,
		1);

	ASTEST_END_FULL
	return bPassed;
}

bool FAngelscriptGASCoreTypeCompileCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(GASCoreTypeFixtureModuleName);
	};

	FString Script = TEXT(R"AS(
UCLASS()
class __ABILITY_CLASS__ : UAngelscriptGASAbility
{
}

UCLASS()
class __ATTRIBUTE_SET_CLASS__ : UAngelscriptAttributeSet
{
	UPROPERTY()
	FAngelscriptGameplayAttributeData Health;
}

void ProbeGASCoreTypeSurface(
	UAngelscriptAbilitySystemComponent AbilitySystemComponent,
	UAngelscriptAttributeSet AttributeSet,
	UAngelscriptGASAbility Ability)
{
	if (AbilitySystemComponent != null || AttributeSet != null || Ability != null)
	{
		Print("Unexpected non-null GAS core type surface");
	}
}

int VerifyGASCoreTypeCompileCompat()
{
	TSubclassOf<UGameplayAbility> AbilityClass = __ABILITY_CLASS__::StaticClass();
	UClass AttributeSetClass = __ATTRIBUTE_SET_CLASS__::StaticClass();
	if (!AbilityClass.IsValid())
		return 10;
	if (AttributeSetClass == null)
		return 20;

	FGameplayAttribute HealthAttribute;
	if (!UAngelscriptAttributeSet::TryGetGameplayAttribute(AttributeSetClass, n"Health", HealthAttribute))
		return 30;
	if (!HealthAttribute.IsValid())
		return 40;

	UAngelscriptAbilitySystemComponent AbilitySystemComponent;
	UAngelscriptAttributeSet AttributeSet;
	UAngelscriptGASAbility Ability;
	if (AbilitySystemComponent != null || AttributeSet != null || Ability != null)
		return 50;

	return 1;
}
)AS");
	ReplaceToken(Script, TEXT("__ABILITY_CLASS__"), GASCoreTypeAbilityClassName);
	ReplaceToken(Script, TEXT("__ATTRIBUTE_SET_CLASS__"), GASCoreTypeAttributeSetClassName);

	if (!TestTrue(
			TEXT("GASCoreTypeCompileCompat should compile annotated GAS core fixture classes and type-surface probes"),
			CompileAnnotatedModuleFromMemory(
				&Engine,
				FName(GASCoreTypeFixtureModuleName),
				FString(GASCoreTypeFixtureModuleName) + TEXT(".as"),
				Script)))
	{
		return false;
	}

	UClass* AbilityClass = FindGeneratedClass(&Engine, FName(GASCoreTypeAbilityClassName));
	UClass* AttributeSetClass = FindGeneratedClass(&Engine, FName(GASCoreTypeAttributeSetClassName));
	if (!TestNotNull(TEXT("GASCoreTypeCompileCompat should publish the scripted GAS ability class"), AbilityClass)
		|| !TestNotNull(TEXT("GASCoreTypeCompileCompat should publish the scripted GAS attribute-set class"), AttributeSetClass))
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!TestTrue(
			TEXT("GASCoreTypeCompileCompat should execute its annotated verification entry point"),
			ExecuteIntFunction(
				&Engine,
				FName(GASCoreTypeFixtureModuleName),
				TEXT("int VerifyGASCoreTypeCompileCompat()"),
				Result)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("GAS core type bindings should keep UAngelscript GAS types declarable and script-usable in generated fixture modules"),
		Result,
		1);

	ASTEST_END_FULL
	return bPassed;
}

#endif
