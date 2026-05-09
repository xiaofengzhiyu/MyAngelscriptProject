// AngelscriptGASExtendedBindingsTests.cpp
// CQTest coverage for AngelscriptGASLibrary, FGameplayAbilitySpec,
// FGameplayAttribute bindings.
// Automation IDs: Angelscript.GAS.Bindings.GASExtended.*

#include "CQTest.h"
#include "Shared/AngelscriptTestMacros.h"
#include "Shared/AngelscriptBindingsCoverage.h"
#include "Shared/AngelscriptBindingsModuleBuilder.h"
#include "Shared/AngelscriptBindingsAssertions.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptTestBindings;

static const FBindingsCoverageProfile GGASExtProfile{
	TEXT("GASExt"), TEXT(""), TEXT("ASGASExt"), TEXT("GASExt"), TEXT("GASExtBindings"),
};

TEST_CLASS_WITH_FLAGS(FAngelscriptGASExtendedBindingsTest,
	"Angelscript.GAS.Bindings.GASExtended",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
{
	BEFORE_ALL() { ASTEST_CREATE_ENGINE(); }
	AFTER_ALL() { FAngelscriptEngine& E = ASTEST_GET_ENGINE(); ASTEST_RESET_ENGINE(E); }

	TEST_METHOD(FGameplayAttributeDefault)
	{
		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		FCoverageModuleScope Mod(*TestRunner, Engine, GGASExtProfile, TEXT("Attribute"), TEXT(R"(
int Attribute_DefaultInvalid()
{
	FGameplayAttribute Attr;
	return Attr.IsValid() ? 0 : 1;
}
)"));
		if (!Mod.IsValid())
		{
			TestRunner->AddInfo(TEXT("FGameplayAttribute not available, skipping"));
			return;
		}
		ExpectGlobalInt(*TestRunner, Engine, Mod.GetModule(), GGASExtProfile,
			TEXT("int Attribute_DefaultInvalid()"), TEXT("Default FGameplayAttribute is invalid"), 1);
	}

	TEST_METHOD(FGameplayAbilitySpecDefault)
	{
		FAngelscriptEngine& Engine = ASTEST_GET_ENGINE();
		FAngelscriptEngineScope Scope(Engine);
		FCoverageModuleScope Mod(*TestRunner, Engine, GGASExtProfile, TEXT("AbilitySpec"), TEXT(R"(
int AbilitySpec_DefaultLevel()
{
	FGameplayAbilitySpec Spec;
	return Spec.Level;
}
)"));
		if (!Mod.IsValid())
		{
			TestRunner->AddInfo(TEXT("FGameplayAbilitySpec not available, skipping"));
			return;
		}
		// UE 5.7: FGameplayAbilitySpec default Level changed from 0 to 1
		ExpectGlobalInt(*TestRunner, Engine, Mod.GetModule(), GGASExtProfile,
			TEXT("int AbilitySpec_DefaultLevel()"), TEXT("Default ability spec level"), 1);
	}
};

#endif
