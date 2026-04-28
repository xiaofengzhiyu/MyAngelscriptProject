#include "Core/AngelscriptBinds.h"
#include "Binds/Helper_FunctionSignature.h"
#include "Shared/AngelscriptTestMacros.h"

#include "FunctionLibraries/AngelscriptMathLibrary.h"
#include "Misc/AutomationTest.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

class asIScriptGeneric;

namespace AngelscriptTest_Core_AngelscriptFunctionSignatureMetadataTests_Private
{
	void CDECL NoOpGeneric(asIScriptGeneric* Generic)
	{
		(void)Generic;
	}

	struct FScopedFunctionMetadataOverride
	{
		UFunction* Function = nullptr;
		FName Key;
		bool bHadValue = false;
		FString PreviousValue;

		FScopedFunctionMetadataOverride(UFunction& InFunction, const FName InKey, const TCHAR* NewValue = TEXT(""))
			: Function(&InFunction)
			, Key(InKey)
		{
			if (const FString* ExistingValue = InFunction.FindMetaData(Key))
			{
				bHadValue = true;
				PreviousValue = *ExistingValue;
			}

			Function->SetMetaData(Key, NewValue);
		}

		~FScopedFunctionMetadataOverride()
		{
			if (Function == nullptr)
			{
				return;
			}

			if (bHadValue)
			{
				Function->SetMetaData(Key, *PreviousValue);
			}
			else
			{
				Function->RemoveMetaData(Key);
			}
		}
	};

	struct FScopedFunctionFlagOverride
	{
		UFunction* Function = nullptr;
		EFunctionFlags PreviousFlags = FUNC_None;

		FScopedFunctionFlagOverride(UFunction& InFunction, const EFunctionFlags FlagsToAdd)
			: Function(&InFunction)
			, PreviousFlags(InFunction.FunctionFlags)
		{
			Function->FunctionFlags |= FlagsToAdd;
		}

		~FScopedFunctionFlagOverride()
		{
			if (Function != nullptr)
			{
				Function->FunctionFlags = PreviousFlags;
			}
		}
	};

	TSharedPtr<FAngelscriptType> ResolveCoverageHostType(UClass* OwnerClass)
	{
		return OwnerClass != nullptr ? FAngelscriptType::GetByClass(OwnerClass) : nullptr;
	}

	UFunction* FindCoverageMethod(FAutomationTestBase& Test, UClass* OwnerClass, const TCHAR* FunctionName)
	{
		UFunction* Function = OwnerClass != nullptr ? OwnerClass->FindFunctionByName(FName(FunctionName)) : nullptr;
		Test.TestNotNull(
			*FString::Printf(TEXT("FunctionSignature metadata tests should find %s on %s"), FunctionName, OwnerClass != nullptr ? *OwnerClass->GetName() : TEXT("<null>")),
			Function);
		return Function;
	}

	int32 BindCoverageMethodForInspection(const FAngelscriptFunctionSignature& Signature)
	{
		if (Signature.bStaticInScript)
		{
			return FAngelscriptBinds::BindGlobalGenericFunction(
				Signature.Declaration,
				&NoOpGeneric);
		}

		return FAngelscriptBinds::BindMethodDirect(
			Signature.ClassName,
			Signature.Declaration,
			asFUNCTION(NoOpGeneric),
			asCALL_GENERIC,
			ASAutoCaller::FunctionCaller::Make());
	}

	asCScriptFunction* GetBoundScriptFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const int32 FunctionId,
		const TCHAR* Context)
	{
		asCScriptFunction* ScriptFunction = reinterpret_cast<asCScriptFunction*>(Engine.GetScriptEngine()->GetFunctionById(FunctionId));
		Test.TestNotNull(
			*FString::Printf(TEXT("%s should resolve the bound script function"), Context),
			ScriptFunction);
		return ScriptFunction;
	}

	FString GetDeprecationMessage(const asCScriptFunction& ScriptFunction)
	{
		return FString(ANSI_TO_TCHAR(ScriptFunction.deprecationMessage.AddressOf()));
	}
}

using namespace AngelscriptTest_Core_AngelscriptFunctionSignatureMetadataTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFunctionSignatureReturnValueTraitsTest,
	"Angelscript.TestModule.Engine.FunctionSignature.ReturnValueTraitsPreserveNoDiscardAndAllowDiscard",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFunctionSignatureMethodMutationMetadataTest,
	"Angelscript.TestModule.Engine.FunctionSignature.MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptFunctionSignatureReturnValueTraitsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	UClass* OwnerClass = UAngelscriptMathLibrary::StaticClass();
	TSharedPtr<FAngelscriptType> HostType = ResolveCoverageHostType(OwnerClass);
	if (!TestTrue(
			TEXT("FunctionSignature return-value trait test should resolve the UAngelscriptMathLibrary host type"),
			HostType.IsValid()))
	{
		return false;
	}

	UFunction* NoDiscardFunction = FindCoverageMethod(*this, OwnerClass, TEXT("WrapDouble"));
	UFunction* AllowDiscardFunction = FindCoverageMethod(*this, OwnerClass, TEXT("LineBoxIntersection"));
	if (NoDiscardFunction == nullptr || AllowDiscardFunction == nullptr)
	{
		return false;
	}

	FScopedFunctionMetadataOverride AllowDiscardMetadata(*AllowDiscardFunction, TEXT("ScriptAllowDiscard"));

	FAngelscriptFunctionSignature NoDiscardSignature(
		HostType.ToSharedRef(),
		NoDiscardFunction,
		TEXT("Wrap_ReturnValueTraits_NoDiscard"));
	FAngelscriptFunctionSignature AllowDiscardSignature(
		HostType.ToSharedRef(),
		AllowDiscardFunction,
		TEXT("LineBoxIntersection_ReturnValueTraits_AllowDiscard"));

	const int32 NoDiscardFunctionId = BindCoverageMethodForInspection(NoDiscardSignature);
	const int32 AllowDiscardFunctionId = BindCoverageMethodForInspection(AllowDiscardSignature);
	asCScriptFunction* NoDiscardScriptFunction = GetBoundScriptFunction(
		*this,
		Engine,
		NoDiscardFunctionId,
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard no-discard case"));
	asCScriptFunction* AllowDiscardScriptFunction = GetBoundScriptFunction(
		*this,
		Engine,
		AllowDiscardFunctionId,
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard allow-discard case"));
	if (NoDiscardScriptFunction == nullptr || AllowDiscardScriptFunction == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should append no_discard when ScriptNoDiscard metadata is present"),
		NoDiscardSignature.Declaration.Contains(TEXT("no_discard")));
	bPassed &= TestFalse(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should not append allow_discard to the ScriptNoDiscard declaration"),
		NoDiscardSignature.Declaration.Contains(TEXT("allow_discard")));
	bPassed &= TestTrue(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should propagate the no_discard trait into the bound script function"),
		NoDiscardScriptFunction->traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should keep the ScriptNoDiscard bind free of the allow_discard trait"),
		NoDiscardScriptFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));

	bPassed &= TestTrue(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should append allow_discard when ScriptAllowDiscard metadata is present"),
		AllowDiscardSignature.Declaration.Contains(TEXT("allow_discard")));
	bPassed &= TestFalse(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should not append no_discard to the ScriptAllowDiscard declaration"),
		AllowDiscardSignature.Declaration.Contains(TEXT("no_discard")));
	bPassed &= TestTrue(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should propagate the allow_discard trait into the bound script function"),
		AllowDiscardScriptFunction->traits.GetTrait(asTRAIT_ALLOWDISCARD));
	bPassed &= TestFalse(
		TEXT("ReturnValueTraitsPreserveNoDiscardAndAllowDiscard should keep the ScriptAllowDiscard bind free of the no_discard trait"),
		AllowDiscardScriptFunction->traits.GetTrait(asTRAIT_NODISCARD));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptFunctionSignatureMethodMutationMetadataTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	UClass* OwnerClass = UAngelscriptMathLibrary::StaticClass();
	TSharedPtr<FAngelscriptType> HostType = ResolveCoverageHostType(OwnerClass);
	if (!TestTrue(
			TEXT("FunctionSignature method-mutation metadata test should resolve the UAngelscriptMathLibrary host type"),
			HostType.IsValid()))
	{
		return false;
	}

	UFunction* SetterFunction = FindCoverageMethod(*this, OwnerClass, TEXT("LineBoxIntersection"));
	UFunction* ValueFunction = FindCoverageMethod(*this, OwnerClass, TEXT("WrapDouble"));
	if (SetterFunction == nullptr || ValueFunction == nullptr)
	{
		return false;
	}

	FScopedFunctionMetadataOverride NotPropertyMetadata(*SetterFunction, TEXT("NotAngelscriptProperty"));
	FScopedFunctionMetadataOverride ProtectedMetadata(*SetterFunction, TEXT("BlueprintProtected"), TEXT("true"));
	FScopedFunctionMetadataOverride DeprecatedMetadata(*ValueFunction, TEXT("DeprecatedFunction"));
	static const TCHAR* ExpectedDeprecationMessage = TEXT("Use NativeIntEventWithParameter instead.");
	FScopedFunctionMetadataOverride DeprecationMessageMetadata(*ValueFunction, TEXT("DeprecationMessage"), ExpectedDeprecationMessage);
	FScopedFunctionFlagOverride EditorOnlyFlag(*ValueFunction, FUNC_EditorOnly);

	FAngelscriptFunctionSignature SetterSignature(
		HostType.ToSharedRef(),
		SetterFunction,
		TEXT("LineBoxIntersection_MetadataMutation"));
	FAngelscriptFunctionSignature ValueSignature(
		HostType.ToSharedRef(),
		ValueFunction,
		TEXT("Wrap_MetadataMutation"));

	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should bind the math helper as a global script function"),
		SetterSignature.bStaticInScript);
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should detect NotAngelscriptProperty metadata on the math helper"),
		SetterSignature.bNotAngelscriptProperty);
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should detect BlueprintProtected metadata on the math helper"),
		SetterSignature.bBlueprintProtected);
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should detect DeprecatedFunction metadata on the math helper"),
		ValueSignature.bDeprecated);
	bPassed &= TestEqual(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should preserve the deprecation message on the signature"),
		ValueSignature.DeprecationMessage,
		FString(ExpectedDeprecationMessage));
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should treat the flagged value getter as editor-only"),
		ValueSignature.IsFunctionEditorOnly());

	const int32 SetterFunctionId = BindCoverageMethodForInspection(SetterSignature);
	const int32 ValueFunctionId = BindCoverageMethodForInspection(ValueSignature);
	asCScriptFunction* SetterScriptFunction = GetBoundScriptFunction(
		*this,
		Engine,
		SetterFunctionId,
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits setter case"));
	asCScriptFunction* ValueScriptFunction = GetBoundScriptFunction(
		*this,
		Engine,
		ValueFunctionId,
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits getter case"));
	if (SetterScriptFunction == nullptr || ValueScriptFunction == nullptr)
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should start with implicit property accessors enabled on the bound math helper"),
		SetterScriptFunction->IsProperty());
	bPassed &= TestFalse(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should not mark the math helper protected before mutation"),
		SetterScriptFunction->IsProtected());
	bPassed &= TestFalse(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should not mark the deprecated math helper before mutation"),
		ValueScriptFunction->traits.GetTrait(asTRAIT_DEPRECATED));
	bPassed &= TestFalse(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should not mark the deprecated math helper editor-only before mutation"),
		ValueScriptFunction->traits.GetTrait(asTRAIT_EDITOR_ONLY));

	SetterSignature.ModifyScriptFunction(SetterFunctionId);
	ValueSignature.ModifyScriptFunction(ValueFunctionId);

	bPassed &= TestFalse(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should clear the implicit property trait when NotAngelscriptProperty is present on the math helper"),
		SetterScriptFunction->IsProperty());
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should mark the math helper protected when BlueprintProtected is present"),
		SetterScriptFunction->IsProtected());
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should mark the deprecated math helper after mutation"),
		ValueScriptFunction->traits.GetTrait(asTRAIT_DEPRECATED));
	bPassed &= TestTrue(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should mark the deprecated math helper editor-only after mutation"),
		ValueScriptFunction->traits.GetTrait(asTRAIT_EDITOR_ONLY));
	bPassed &= TestEqual(
		TEXT("MethodMetadataMutationAppliesPropertyProtectionDeprecationAndEditorOnlyTraits should copy the deprecation message onto the bound script function"),
		GetDeprecationMessage(*ValueScriptFunction),
		FString(ExpectedDeprecationMessage));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
