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

namespace AngelscriptTest_Bindings_AngelscriptDelegateTraitBindingsTests_Private
{
	static constexpr ANSICHAR DelegateConstructorDeclarationsModuleName[] = "ASDelegateConstructorDeclarations";
	static constexpr ANSICHAR DelegateConstructorTraitsModuleName[] = "ASDelegateConstructorTraits";

	struct FConstructorLookupSpec
	{
		TArray<const TCHAR*> RequiredFragments;
		TArray<const TCHAR*> ForbiddenFragments;
	};

	FString DescribeFunction(const asIScriptFunction& Function)
	{
		return UTF8_TO_TCHAR(Function.GetDeclaration(true, false, true, true));
	}

	void CollectConstructors(asITypeInfo& TypeInfo, TArray<asIScriptFunction*>& OutConstructors)
	{
		OutConstructors.Reset();

		for (asUINT BehaviourIndex = 0; BehaviourIndex < TypeInfo.GetBehaviourCount(); ++BehaviourIndex)
		{
			asEBehaviours Behaviour = asBEHAVE_CONSTRUCT;
			asIScriptFunction* BehaviourFunction = TypeInfo.GetBehaviourByIndex(BehaviourIndex, &Behaviour);
			if (Behaviour == asBEHAVE_CONSTRUCT && BehaviourFunction != nullptr)
			{
				OutConstructors.Add(BehaviourFunction);
			}
		}
	}

	FString DescribeAvailableConstructors(asITypeInfo& TypeInfo)
	{
		TArray<asIScriptFunction*> Constructors;
		CollectConstructors(TypeInfo, Constructors);

		FString Available;
		for (asIScriptFunction* Constructor : Constructors)
		{
			if (Constructor == nullptr)
			{
				continue;
			}

			if (!Available.IsEmpty())
			{
				Available += TEXT("\n");
			}

			Available += DescribeFunction(*Constructor);
		}

		return Available.IsEmpty() ? TEXT("<none>") : Available;
	}

	asCScriptFunction* FindConstructorBySpec(
		FAutomationTestBase& Test,
		asITypeInfo& TypeInfo,
		const FConstructorLookupSpec& Spec,
		const TCHAR* Context)
	{
		TArray<asIScriptFunction*> Constructors;
		CollectConstructors(TypeInfo, Constructors);

		for (asIScriptFunction* Constructor : Constructors)
		{
			if (Constructor == nullptr)
			{
				continue;
			}

			const FString Declaration = DescribeFunction(*Constructor);
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
				return reinterpret_cast<asCScriptFunction*>(Constructor);
			}
		}

		Test.AddError(FString::Printf(
			TEXT("%s failed to resolve a matching constructor. Available constructors:\n%s"),
			Context,
			*DescribeAvailableConstructors(TypeInfo)));
		return nullptr;
	}

	asITypeInfo* ResolveDelegateType(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const ANSICHAR* ModuleName)
	{
		asIScriptModule* Module = BuildModule(
			Test,
			Engine,
			ModuleName,
			TEXT(R"AS(
delegate int FNativeCallback(int Value, const FString& Label);

int TouchDelegateType()
{
	FNativeCallback Callback;
	return Callback.IsBound() ? 1 : 0;
}
)AS"));
		if (Module == nullptr)
		{
			return nullptr;
		}

		asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
		if (!Test.TestNotNull(TEXT("Delegate constructor trait tests should expose a live script engine"), ScriptEngine))
		{
			return nullptr;
		}

		const auto DelegateDeclAnsi = StringCast<ANSICHAR>(TEXT("FNativeCallback"));
		asITypeInfo* DelegateType = Module->GetTypeInfoByDecl(DelegateDeclAnsi.Get());
		if (DelegateType != nullptr)
		{
			return DelegateType;
		}

		const int DelegateTypeId = ScriptEngine->GetTypeIdByDecl(DelegateDeclAnsi.Get());
		if (!Test.TestTrue(TEXT("Delegate constructor trait tests should resolve a valid FNativeCallback type id"), DelegateTypeId >= 0))
		{
			return nullptr;
		}

		DelegateType = ScriptEngine->GetTypeInfoById(DelegateTypeId);
		Test.TestNotNull(TEXT("Delegate constructor trait tests should resolve FNativeCallback type info"), DelegateType);
		return DelegateType;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptDelegateTraitBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateConstructorDeclarationBindingsTest,
	"Angelscript.TestModule.Bindings.Delegate.ConstructorDeclarations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDelegateConstructorTraitBindingsTest,
	"Angelscript.TestModule.Bindings.Delegate.ConstructorTraits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDelegateConstructorDeclarationBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(DelegateConstructorDeclarationsModuleName));
	};

	asITypeInfo* DelegateType = ResolveDelegateType(*this, Engine, DelegateConstructorDeclarationsModuleName);
	if (DelegateType == nullptr)
	{
		return false;
	}

	TArray<asIScriptFunction*> Constructors;
	CollectConstructors(*DelegateType, Constructors);

	bPassed &= TestEqual(
		TEXT("Concrete script-declared delegate types should expose exactly three constructor behaviours"),
		Constructors.Num(),
		3);

	const FConstructorLookupSpec DefaultConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback()")},
		{TEXT("Other"), TEXT("UObject"), TEXT("BindFunctionName")}
	};
	const FConstructorLookupSpec CopyConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback("), TEXT("FNativeCallback Other")},
		{TEXT("UObject"), TEXT("BindFunctionName")}
	};
	const FConstructorLookupSpec BoundConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback("), TEXT("UObject Object"), TEXT("FName BindFunctionName")},
		{TEXT("Other")}
	};

	asCScriptFunction* DefaultConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		DefaultConstructorSpec,
		TEXT("Delegate constructor declaration lookup for default constructor"));
	asCScriptFunction* CopyConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		CopyConstructorSpec,
		TEXT("Delegate constructor declaration lookup for copy constructor"));
	asCScriptFunction* BoundConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		BoundConstructorSpec,
		TEXT("Delegate constructor declaration lookup for UObject/FName constructor"));
	if (DefaultConstructor == nullptr || CopyConstructor == nullptr || BoundConstructor == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate types should preserve the default constructor declaration"),
		DescribeFunction(*DefaultConstructor).Contains(TEXT("FNativeCallback::FNativeCallback()")));
	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate types should preserve the copy constructor declaration"),
		DescribeFunction(*CopyConstructor).Contains(TEXT("FNativeCallback Other")));
	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate types should preserve the UObject/FName constructor declaration"),
		DescribeFunction(*BoundConstructor).Contains(TEXT("BindFunctionName")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptDelegateConstructorTraitBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(DelegateConstructorTraitsModuleName));
	};

	asITypeInfo* DelegateType = ResolveDelegateType(*this, Engine, DelegateConstructorTraitsModuleName);
	if (DelegateType == nullptr)
	{
		return false;
	}

	const FConstructorLookupSpec DefaultConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback()")},
		{TEXT("Other"), TEXT("UObject"), TEXT("BindFunctionName")}
	};
	const FConstructorLookupSpec CopyConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback("), TEXT("FNativeCallback Other")},
		{TEXT("UObject"), TEXT("BindFunctionName")}
	};
	const FConstructorLookupSpec BoundConstructorSpec
	{
		{TEXT("FNativeCallback::FNativeCallback("), TEXT("UObject Object"), TEXT("FName BindFunctionName")},
		{TEXT("Other")}
	};

	asCScriptFunction* DefaultConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		DefaultConstructorSpec,
		TEXT("Delegate constructor trait lookup for default constructor"));
	asCScriptFunction* CopyConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		CopyConstructorSpec,
		TEXT("Delegate constructor trait lookup for copy constructor"));
	asCScriptFunction* BoundConstructor = FindConstructorBySpec(
		*this,
		*DelegateType,
		BoundConstructorSpec,
		TEXT("Delegate constructor trait lookup for UObject/FName constructor"));
	if (DefaultConstructor == nullptr || CopyConstructor == nullptr || BoundConstructor == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate default constructors should be marked no_discard"),
		DefaultConstructor->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("Concrete script-declared delegate default constructors should not be marked allow_discard"),
		DefaultConstructor->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate copy constructors should be marked no_discard"),
		CopyConstructor->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("Concrete script-declared delegate copy constructors should not be marked allow_discard"),
		CopyConstructor->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("Concrete script-declared delegate UObject/FName constructors should be marked no_discard"),
		BoundConstructor->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("Concrete script-declared delegate UObject/FName constructors should not be marked allow_discard"),
		BoundConstructor->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
