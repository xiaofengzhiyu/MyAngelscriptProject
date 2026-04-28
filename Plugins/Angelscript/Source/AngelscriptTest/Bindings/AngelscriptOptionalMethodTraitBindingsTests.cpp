#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Containers/StringConv.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptOptionalMethodTraitBindingsTests_Private
{
	static constexpr ANSICHAR OptionalMethodMutationModuleName[] = "ASOptionalMethodMutationCompat";
	static constexpr ANSICHAR OptionalMethodTraitsModuleName[] = "ASOptionalMethodTraits";

	struct FMethodLookupSpec
	{
		const ANSICHAR* Name = nullptr;
		TArray<const TCHAR*> RequiredFragments;
		TArray<const TCHAR*> ForbiddenFragments;
	};

	FString DescribeMethod(const asIScriptFunction& Function)
	{
		return UTF8_TO_TCHAR(Function.GetDeclaration(true, false, true, true));
	}

	FString DescribeAvailableMethods(asITypeInfo& TypeInfo)
	{
		FString Available;
		const asUINT MethodCount = TypeInfo.GetMethodCount();
		for (asUINT MethodIndex = 0; MethodIndex < MethodCount; ++MethodIndex)
		{
			asIScriptFunction* Candidate = TypeInfo.GetMethodByIndex(MethodIndex);
			if (Candidate == nullptr)
			{
				continue;
			}

			if (!Available.IsEmpty())
			{
				Available += TEXT("\n");
			}

			Available += DescribeMethod(*Candidate);
		}

		return Available;
	}

	asCScriptFunction* FindMethodBySpec(
		FAutomationTestBase& Test,
		asITypeInfo& TypeInfo,
		const FMethodLookupSpec& Spec,
		const TCHAR* Context)
	{
		const asUINT MethodCount = TypeInfo.GetMethodCount();
		for (asUINT MethodIndex = 0; MethodIndex < MethodCount; ++MethodIndex)
		{
			asIScriptFunction* Candidate = TypeInfo.GetMethodByIndex(MethodIndex);
			if (Candidate == nullptr || FCStringAnsi::Strcmp(Candidate->GetName(), Spec.Name) != 0)
			{
				continue;
			}

			const FString Declaration = DescribeMethod(*Candidate);
			bool bMatches = true;
			for (const TCHAR* Fragment : Spec.RequiredFragments)
			{
				bMatches &= Declaration.Contains(Fragment);
			}
			for (const TCHAR* Fragment : Spec.ForbiddenFragments)
			{
				bMatches &= !Declaration.Contains(Fragment);
			}

			if (bMatches)
			{
				return reinterpret_cast<asCScriptFunction*>(Candidate);
			}
		}

		const FString AvailableMethods = DescribeAvailableMethods(TypeInfo);
		Test.AddError(FString::Printf(
			TEXT("%s failed to resolve a matching method named '%hs'. Available methods:\n%s"),
			Context,
			Spec.Name,
			*AvailableMethods));
		return nullptr;
	}

	asITypeInfo* ResolveOptionalIntType(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const ANSICHAR* ModuleName)
	{
		asIScriptModule* Module = BuildModule(
			Test,
			Engine,
			ModuleName,
			TEXT(R"AS(
int TouchOptionalType()
{
	TOptional<int> Value(1);
	return Value.GetValue();
}
)AS"));
		if (Module == nullptr)
		{
			return nullptr;
		}

		asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
		if (!Test.TestNotNull(TEXT("Optional method trait test should expose a live script engine"), ScriptEngine))
		{
			return nullptr;
		}

		const auto OptionalDeclAnsi = StringCast<ANSICHAR>(TEXT("TOptional<int>"));
		const int OptionalTypeId = ScriptEngine->GetTypeIdByDecl(OptionalDeclAnsi.Get());
		if (!Test.TestTrue(TEXT("Optional method trait test should resolve a valid TOptional<int> type id"), OptionalTypeId >= 0))
		{
			return nullptr;
		}

		asITypeInfo* OptionalType = ScriptEngine->GetTypeInfoById(OptionalTypeId);
		Test.TestNotNull(TEXT("Optional method trait test should resolve TOptional<int> type info"), OptionalType);
		return OptionalType;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptOptionalMethodTraitBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptOptionalMethodMutationBindingsTest,
	"Angelscript.TestModule.Bindings.Optional.MethodMutationCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptOptionalMethodTraitBindingsTest,
	"Angelscript.TestModule.Bindings.Optional.MethodTraits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptOptionalMethodMutationBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(OptionalMethodMutationModuleName));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		OptionalMethodMutationModuleName,
		TEXT(R"AS(
int Entry()
{
	TOptional<int> Value(12);
	Value.GetValue() = 34;
	if (!Value.IsSet())
		return 10;
	if (Value.GetValue() != 34)
		return 20;
	if (Value.Get(99) != 34)
		return 30;

	Value.Reset();
	if (Value.IsSet())
		return 40;
	if (Value.Get(77) != 77)
		return 50;

	Value.Set(-5);
	if (Value.GetValue() != -5)
		return 60;

	return 1;
}
)AS"));
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

	bPassed &= TestEqual(
		TEXT("Optional method bindings should preserve mutable GetValue(), fallback Get(DefaultValue), and post-Reset reassignment semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptOptionalMethodTraitBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(OptionalMethodTraitsModuleName));
	};

	asITypeInfo* OptionalType = ResolveOptionalIntType(*this, Engine, OptionalMethodTraitsModuleName);
	if (OptionalType == nullptr)
	{
		return false;
	}

	const FMethodLookupSpec IsSetSpec
	{
		"IsSet",
		{TEXT("bool"), TEXT("IsSet()"), TEXT(") const")},
		{}
	};
	const FMethodLookupSpec ConstGetValueSpec
	{
		"GetValue",
		{TEXT("const int&"), TEXT("GetValue()"), TEXT(") const")},
		{}
	};
	const FMethodLookupSpec MutableGetValueSpec
	{
		"GetValue",
		{TEXT("int&"), TEXT("GetValue()")},
		{TEXT("const int&"), TEXT(") const")}
	};
	const FMethodLookupSpec GetWithDefaultSpec
	{
		"Get",
		{TEXT("const int&"), TEXT("Get("), TEXT("DefaultValue"), TEXT(") const")},
		{}
	};

	asCScriptFunction* IsSetFunction = FindMethodBySpec(
		*this,
		*OptionalType,
		IsSetSpec,
		TEXT("Optional method trait lookup for IsSet"));
	asCScriptFunction* ConstGetValueFunction = FindMethodBySpec(
		*this,
		*OptionalType,
		ConstGetValueSpec,
		TEXT("Optional method trait lookup for const GetValue"));
	asCScriptFunction* MutableGetValueFunction = FindMethodBySpec(
		*this,
		*OptionalType,
		MutableGetValueSpec,
		TEXT("Optional method trait lookup for mutable GetValue"));
	asCScriptFunction* GetWithDefaultFunction = FindMethodBySpec(
		*this,
		*OptionalType,
		GetWithDefaultSpec,
		TEXT("Optional method trait lookup for Get(DefaultValue)"));
	if (IsSetFunction == nullptr || ConstGetValueFunction == nullptr || MutableGetValueFunction == nullptr || GetWithDefaultFunction == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("TOptional<int>::IsSet should be marked no_discard"),
		IsSetFunction->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("TOptional<int>::IsSet should not be marked allow_discard"),
		IsSetFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("TOptional<int>::GetValue const overload should be marked no_discard"),
		ConstGetValueFunction->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("TOptional<int>::GetValue const overload should not be marked allow_discard"),
		ConstGetValueFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("TOptional<int>::GetValue mutable overload should be marked no_discard"),
		MutableGetValueFunction->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("TOptional<int>::GetValue mutable overload should not be marked allow_discard"),
		MutableGetValueFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("TOptional<int>::Get(DefaultValue) should be marked no_discard"),
		GetWithDefaultFunction->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("TOptional<int>::Get(DefaultValue) should not be marked allow_discard"),
		GetWithDefaultFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
