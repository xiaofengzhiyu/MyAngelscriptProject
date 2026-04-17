#include "AngelscriptGASTestTypes.h"
#include "Shared/AngelscriptTestMacros.h"

#include "../../AngelscriptRuntime/Core/AngelscriptGameplayEffectUtils.h"

#include "GameplayEffect.h"
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const FName HealthAttributeName = GET_MEMBER_NAME_CHECKED(UAngelscriptGASTestAttributeSet, Health);
	const FName MissingAttributeName(TEXT("MissingAttr"));
	const float ModifierMagnitude = 12.5f;

	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		AngelscriptGameplayEffectUtilsSourceSpecTag,
		TEXT("Angelscript.Tests.RuntimeCore.GameplayEffectUtils.Source.Spec"));
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		AngelscriptGameplayEffectUtilsSourceActorTag,
		TEXT("Angelscript.Tests.RuntimeCore.GameplayEffectUtils.Source.Actor"));
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		AngelscriptGameplayEffectUtilsTargetSpecTag,
		TEXT("Angelscript.Tests.RuntimeCore.GameplayEffectUtils.Target.Spec"));
	UE_DEFINE_GAMEPLAY_TAG_STATIC(
		AngelscriptGameplayEffectUtilsTargetActorTag,
		TEXT("Angelscript.Tests.RuntimeCore.GameplayEffectUtils.Target.Actor"));

	bool ResolveHealthAttribute(FAutomationTestBase& Test, FGameplayAttribute& OutHealthAttribute)
	{
		return Test.TestTrue(
			TEXT("GameplayEffectUtils test should resolve the reflected Health gameplay attribute"),
			UAngelscriptAttributeSet::TryGetGameplayAttribute(
				UAngelscriptGASTestAttributeSet::StaticClass(),
				HealthAttributeName,
				OutHealthAttribute));
	}

	bool ExpectTagState(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const FGameplayTagContainer* ActualTags,
		const FGameplayTagContainer* ExpectedTags,
		const FGameplayTag ExpectedSpecTag,
		const FGameplayTag ExpectedActorTag,
		const FGameplayTag UnexpectedSpecTag,
		const FGameplayTag UnexpectedActorTag)
	{
		bool bPassed = true;
		bPassed &= Test.TestNotNull(
			*FString::Printf(TEXT("%s should populate an aggregated tag container"), Context),
			ActualTags);
		bPassed &= Test.TestNotNull(
			*FString::Printf(TEXT("%s should expose the expected aggregated tag container"), Context),
			ExpectedTags);
		if (!bPassed)
		{
			return false;
		}

		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should forward the exact aggregated-tag pointer from the gameplay-effect spec"), Context),
			ActualTags == ExpectedTags);
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should match the aggregated tags from the gameplay-effect spec"), Context),
			*ActualTags == *ExpectedTags);
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should include the expected spec tag"), Context),
			ActualTags->HasTagExact(ExpectedSpecTag));
		bPassed &= Test.TestTrue(
			*FString::Printf(TEXT("%s should include the expected actor tag"), Context),
			ActualTags->HasTagExact(ExpectedActorTag));
		bPassed &= Test.TestFalse(
			*FString::Printf(TEXT("%s should not contain the other side's spec tag"), Context),
			ActualTags->HasTagExact(UnexpectedSpecTag));
		bPassed &= Test.TestFalse(
			*FString::Printf(TEXT("%s should not contain the other side's actor tag"), Context),
			ActualTags->HasTagExact(UnexpectedActorTag));
		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGameplayEffectUtilsCapturesAttributesAndTagsTest,
	"Angelscript.TestModule.Engine.GAS.GameplayEffectUtils.CapturesAttributesAndTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGameplayEffectUtilsCapturesAttributesAndTagsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FGameplayAttribute HealthAttribute;
	if (!ResolveHealthAttribute(*this, HealthAttribute))
	{
		return false;
	}

	const FGameplayEffectAttributeCaptureDefinition ValidCapture =
		UAngelscriptGameplayEffectUtils::CaptureGameplayAttribute(
			UAngelscriptGASTestAttributeSet::StaticClass(),
			HealthAttributeName,
			EGameplayEffectAttributeCaptureSource::Target,
			true);

	TestTrue(
		TEXT("CaptureGameplayAttribute should return a valid attribute capture definition for Health"),
		ValidCapture.AttributeToCapture.IsValid());
	TestTrue(
		TEXT("CaptureGameplayAttribute should preserve the resolved Health gameplay attribute"),
		UAngelscriptAttributeSet::CompareGameplayAttributes(ValidCapture.AttributeToCapture, HealthAttribute));
	TestEqual(
		TEXT("CaptureGameplayAttribute should preserve the reflected Health attribute name"),
		FName(*ValidCapture.AttributeToCapture.GetName()),
		HealthAttributeName);
	TestEqual(
		TEXT("CaptureGameplayAttribute should preserve the requested capture source"),
		static_cast<uint8>(ValidCapture.AttributeSource),
		static_cast<uint8>(EGameplayEffectAttributeCaptureSource::Target));
	TestTrue(
		TEXT("CaptureGameplayAttribute should preserve the requested snapshot flag"),
		ValidCapture.bSnapshot);

	AddExpectedErrorPlain(TEXT("Ensure condition failed: AttributeProperty"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedErrorPlain(TEXT("LogOutputDevice:"), EAutomationExpectedErrorFlags::Contains, 0);
	const FGameplayEffectAttributeCaptureDefinition InvalidCapture =
		UAngelscriptGameplayEffectUtils::CaptureGameplayAttribute(
			UAngelscriptGASTestAttributeSet::StaticClass(),
			MissingAttributeName,
			EGameplayEffectAttributeCaptureSource::Source,
			false);

	TestFalse(
		TEXT("CaptureGameplayAttribute should fail closed for a missing attribute name"),
		InvalidCapture.AttributeToCapture.IsValid());

	const FGameplayModifierEvaluatedData EvaluatedData =
		UAngelscriptGameplayEffectUtils::MakeGameplayModifierEvaluationData(
			HealthAttribute,
			EGameplayModOp::Multiplicitive,
			ModifierMagnitude);

	TestTrue(
		TEXT("MakeGameplayModifierEvaluationData should preserve the input gameplay attribute"),
		UAngelscriptAttributeSet::CompareGameplayAttributes(EvaluatedData.Attribute, HealthAttribute));
	TestEqual(
		TEXT("MakeGameplayModifierEvaluationData should preserve the requested modifier op"),
		static_cast<uint8>(EvaluatedData.ModifierOp.GetValue()),
		static_cast<uint8>(EGameplayModOp::Multiplicitive));
	TestEqual(
		TEXT("MakeGameplayModifierEvaluationData should preserve the requested modifier magnitude"),
		EvaluatedData.Magnitude,
		ModifierMagnitude);
	TestFalse(
		TEXT("MakeGameplayModifierEvaluationData should default the originating active gameplay-effect handle to invalid"),
		EvaluatedData.Handle.IsValid());
	TestTrue(
		TEXT("MakeGameplayModifierEvaluationData should mark the evaluated-data payload as valid"),
		EvaluatedData.IsValid);

	UGameplayEffect* GameplayEffect = NewObject<UGameplayEffect>(GetTransientPackage(), NAME_None, RF_Transient);
	if (!TestNotNull(TEXT("GameplayEffectUtils test should create a transient gameplay effect"), GameplayEffect))
	{
		return false;
	}

	FGameplayEffectSpec EffectSpec(GameplayEffect, FGameplayEffectContextHandle(), 1.f);
	EffectSpec.CapturedSourceTags.GetSpecTags().AddTag(AngelscriptGameplayEffectUtilsSourceSpecTag);
	EffectSpec.CapturedSourceTags.GetActorTags().AddTag(AngelscriptGameplayEffectUtilsSourceActorTag);
	EffectSpec.CapturedTargetTags.GetSpecTags().AddTag(AngelscriptGameplayEffectUtilsTargetSpecTag);
	EffectSpec.CapturedTargetTags.GetActorTags().AddTag(AngelscriptGameplayEffectUtilsTargetActorTag);

	FGameplayEffectExecutionParameters ExecutionParameters;
	TestTrue(
		TEXT("Execution parameters should start with no captured source tags"),
		ExecutionParameters.WrappedParams.SourceTags == nullptr);
	TestTrue(
		TEXT("Execution parameters should start with no captured target tags"),
		ExecutionParameters.WrappedParams.TargetTags == nullptr);

	UGameplayEffectExecutionParametersMixinLibrary::SetCapturedSourceTagsFromSpec(ExecutionParameters, EffectSpec);

	const FGameplayTagContainer* ExpectedSourceTags = EffectSpec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* ExpectedTargetTags = EffectSpec.CapturedTargetTags.GetAggregatedTags();

	if (!ExpectTagState(
			*this,
			TEXT("SetCapturedSourceTagsFromSpec source tag copy"),
			ExecutionParameters.WrappedParams.SourceTags,
			ExpectedSourceTags,
			AngelscriptGameplayEffectUtilsSourceSpecTag,
			AngelscriptGameplayEffectUtilsSourceActorTag,
			AngelscriptGameplayEffectUtilsTargetSpecTag,
			AngelscriptGameplayEffectUtilsTargetActorTag))
	{
		return false;
	}

	if (!ExpectTagState(
			*this,
			TEXT("SetCapturedSourceTagsFromSpec target tag copy"),
			ExecutionParameters.WrappedParams.TargetTags,
			ExpectedTargetTags,
			AngelscriptGameplayEffectUtilsTargetSpecTag,
			AngelscriptGameplayEffectUtilsTargetActorTag,
			AngelscriptGameplayEffectUtilsSourceSpecTag,
			AngelscriptGameplayEffectUtilsSourceActorTag))
	{
		return false;
	}

	TestTrue(
		TEXT("SetCapturedSourceTagsFromSpec should keep source and target tag pointers distinct"),
		ExecutionParameters.WrappedParams.SourceTags != ExecutionParameters.WrappedParams.TargetTags);

	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
