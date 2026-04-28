#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_builder.h"
#include "source/as_module.h"
#include "source/as_scriptengine.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Internals_AngelscriptBuilderDeclarationParsingTests_Private
{
	FString ToFString(const char* Value)
	{
		return Value != nullptr ? UTF8_TO_TCHAR(Value) : FString();
	}

	asCModule* CreateBuilderModule(asCScriptEngine* ScriptEngine, const char* ModuleName)
	{
		return static_cast<asCModule*>(ScriptEngine->GetModule(ModuleName, asGM_ALWAYS_CREATE));
	}
}

using namespace AngelscriptTest_Internals_AngelscriptBuilderDeclarationParsingTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBuilderFunctionDeclarationMetadataTest,
	"Angelscript.TestModule.Internals.Builder.DeclarationParsing.FunctionMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptBuilderVariableAndPropertyDeclarationParsingTest,
	"Angelscript.TestModule.Internals.Builder.DeclarationParsing.VariableAndPropertyValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptBuilderFunctionDeclarationMetadataTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	asCModule* Module = CreateBuilderModule(ScriptEngine, "BuilderDeclarationParsingFunctionMetadata");
	if (!TestNotNull(TEXT("Builder declaration metadata test should create a backing module"), Module))
	{
		return false;
	}

	asITypeInfo* FVectorType = ScriptEngine->GetTypeInfoByName("FVector");
	asITypeInfo* ActorType = ScriptEngine->GetTypeInfoByName("AActor");
	if (!TestNotNull(TEXT("Builder declaration metadata test should resolve FVector"), FVectorType) ||
		!TestNotNull(TEXT("Builder declaration metadata test should resolve AActor"), ActorType))
	{
		return false;
	}

	asSNameSpace* FunctionNamespace = ScriptEngine->AddNameSpace("BuilderFallback074");
	if (!TestNotNull(TEXT("Builder declaration metadata test should create the dedicated namespace"), FunctionNamespace))
	{
		return false;
	}

	asCBuilder Builder(ScriptEngine, Module);
	asCScriptFunction Function(ScriptEngine, Module, asFUNC_SCRIPT);
	asCArray<bool> ParamAutoHandles;
	bool bReturnAutoHandle = true;

	const int ParseResult = Builder.ParseFunctionDeclaration(
		nullptr,
		"int BuilderFallback074::Compute(FVector Value, int Count = 2, AActor Actor = nullptr) no_discard",
		&Function,
		false,
		&ParamAutoHandles,
		&bReturnAutoHandle,
		ScriptEngine->defaultNamespace);
	if (!TestEqual(
			TEXT("Builder declaration metadata test should parse the function declaration successfully"),
			ParseResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the function name"),
		ToFString(Function.GetName()),
		FString(TEXT("Compute")));
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the parsed namespace"),
		ToFString(Function.GetNamespace()),
		FString(TEXT("BuilderFallback074")));
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep an int return type"),
		Function.GetReturnTypeId(),
		static_cast<int32>(asTYPEID_INT32));
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should expose three parameters"),
		static_cast<int32>(Function.GetParamCount()),
		3);
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep three internal parameter type entries"),
		static_cast<int32>(Function.parameterTypes.GetLength()),
		3);
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep three internal parameter-name entries"),
		static_cast<int32>(Function.parameterNames.GetLength()),
		3);
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep three default-argument slots"),
		static_cast<int32>(Function.defaultArgs.GetLength()),
		3);
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should report one auto-handle flag slot per parameter"),
		static_cast<int32>(ParamAutoHandles.GetLength()),
		3);
	bPassed &= TestFalse(
		TEXT("Builder declaration metadata test should not mark the return type as an auto handle"),
		bReturnAutoHandle);

	if (ParamAutoHandles.GetLength() == 3)
	{
		bPassed &= TestFalse(
			TEXT("Builder declaration metadata test should keep the FVector parameter off the auto-handle path"),
			ParamAutoHandles[0]);
		bPassed &= TestFalse(
			TEXT("Builder declaration metadata test should keep the int parameter off the auto-handle path"),
			ParamAutoHandles[1]);
		bPassed &= TestFalse(
			TEXT("Builder declaration metadata test should keep the handle parameter off the auto-handle path"),
			ParamAutoHandles[2]);
	}

	const asCDataType& ValueType = Function.parameterTypes[0];
	const asCDataType& CountType = Function.parameterTypes[1];
	const asCDataType& ActorHandleType = Function.parameterTypes[2];

	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the FVector parameter type"),
		static_cast<const asITypeInfo*>(ValueType.GetTypeInfo()),
		static_cast<const asITypeInfo*>(FVectorType));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should normalize value-type parameters to references for script functions"),
		ValueType.IsReference());
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should normalize value-type parameters to read-only references"),
		ValueType.IsReadOnly());
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should tag the normalized value-type parameter as inoutref"),
		static_cast<int32>(Function.inOutFlags[0]),
		static_cast<int32>(asTM_INOUTREF));

	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should preserve the primitive parameter as primitive"),
		CountType.IsPrimitive());
	bPassed &= TestFalse(
		TEXT("Builder declaration metadata test should keep primitive parameters passed by value"),
		CountType.IsReference());
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should mark primitive parameters read-only"),
		CountType.IsReadOnly());
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep the primitive parameter free of reference direction flags"),
		static_cast<int32>(Function.inOutFlags[1]),
		static_cast<int32>(asTM_NONE));

	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the object-handle target type"),
		static_cast<const asITypeInfo*>(ActorHandleType.GetTypeInfo()),
		static_cast<const asITypeInfo*>(ActorType));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should keep the object parameter as a handle"),
		ActorHandleType.IsObjectHandle());
	bPassed &= TestFalse(
		TEXT("Builder declaration metadata test should keep the object-handle parameter off the reference path"),
		ActorHandleType.IsReference());
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should keep the object-handle parameter free of reference direction flags"),
		static_cast<int32>(Function.inOutFlags[2]),
		static_cast<int32>(asTM_NONE));

	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the first parameter name"),
		ToFString(Function.parameterNames[0].AddressOf()),
		FString(TEXT("Value")));
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the second parameter name"),
		ToFString(Function.parameterNames[1].AddressOf()),
		FString(TEXT("Count")));
	bPassed &= TestEqual(
		TEXT("Builder declaration metadata test should preserve the third parameter name"),
		ToFString(Function.parameterNames[2].AddressOf()),
		FString(TEXT("Actor")));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should keep the first parameter without a default argument"),
		Function.defaultArgs[0] == nullptr);
	bPassed &= TestNotNull(
		TEXT("Builder declaration metadata test should keep the int default argument"),
		Function.defaultArgs[1]);
	bPassed &= TestNotNull(
		TEXT("Builder declaration metadata test should keep the handle default argument"),
		Function.defaultArgs[2]);
	if (Function.defaultArgs[1] != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("Builder declaration metadata test should preserve the integer default argument text"),
			ToFString(Function.defaultArgs[1]->AddressOf()),
			FString(TEXT("2")));
	}
	if (Function.defaultArgs[2] != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("Builder declaration metadata test should preserve the handle default argument text"),
			ToFString(Function.defaultArgs[2]->AddressOf()),
			FString(TEXT("nullptr")));
	}

	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should preserve the no_discard trait"),
		Function.traits.GetTrait(asTRAIT_NODISCARD));
	bPassed &= TestFalse(
		TEXT("Builder declaration metadata test should not mark the parsed global function as a property"),
		Function.IsProperty());

	const FString Declaration = ToFString(Function.GetDeclaration(false, true, true, false));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should expose the namespaced declaration surface"),
		Declaration.Contains(TEXT("BuilderFallback074::Compute")));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should expose parameter names in the declaration surface"),
		Declaration.Contains(TEXT("Value")) && Declaration.Contains(TEXT("Count")) && Declaration.Contains(TEXT("Actor")));

	const asCString DeclarationWithDefaultsAnsi = Function.GetDeclarationStr(false, true, true, false, true);
	const FString DeclarationWithDefaults = ToFString(DeclarationWithDefaultsAnsi.AddressOf());
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should preserve the integer default argument in the internal declaration string"),
		DeclarationWithDefaults.Contains(TEXT("= 2")));
	bPassed &= TestTrue(
		TEXT("Builder declaration metadata test should preserve the handle default argument in the internal declaration string"),
		DeclarationWithDefaults.Contains(TEXT("= nullptr")));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptBuilderVariableAndPropertyDeclarationParsingTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	asCModule* Module = CreateBuilderModule(ScriptEngine, "BuilderDeclarationParsingVariableProperty");
	if (!TestNotNull(TEXT("Builder variable/property parsing test should create a backing module"), Module))
	{
		return false;
	}

	asITypeInfo* FVectorType = ScriptEngine->GetTypeInfoByName("FVector");
	if (!TestNotNull(TEXT("Builder variable/property parsing test should resolve FVector"), FVectorType))
	{
		return false;
	}

	asSNameSpace* ExplicitNamespace = ScriptEngine->AddNameSpace("BuilderFallback074Decls");
	if (!TestNotNull(TEXT("Builder variable/property parsing test should create the dedicated declaration namespace"), ExplicitNamespace))
	{
		return false;
	}

	asCBuilder Builder(ScriptEngine, Module);

	asCString ScopedVariableName;
	asSNameSpace* ScopedVariableNamespace = nullptr;
	asCDataType ScopedVariableType;
	const int ScopedVariableResult = Builder.ParseVariableDeclaration(
		"FVector BuilderFallback074Decls::Origin",
		ScriptEngine->defaultNamespace,
		ScopedVariableName,
		ScopedVariableNamespace,
		ScopedVariableType);
	if (!TestEqual(
			TEXT("Builder variable/property parsing test should parse an explicitly namespaced variable declaration"),
			ScopedVariableResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the explicitly scoped variable name"),
		ToFString(ScopedVariableName.AddressOf()),
		FString(TEXT("Origin")));
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the explicitly scoped variable namespace"),
		ScopedVariableNamespace,
		ExplicitNamespace);
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the explicitly scoped variable type"),
		static_cast<const asITypeInfo*>(ScopedVariableType.GetTypeInfo()),
		static_cast<const asITypeInfo*>(FVectorType));
	bPassed &= TestFalse(
		TEXT("Builder variable/property parsing test should keep the explicitly scoped variable off the reference path"),
		ScopedVariableType.IsReference());

	asCString ImplicitVariableName;
	asSNameSpace* ImplicitVariableNamespace = nullptr;
	asCDataType ImplicitVariableType;
	const int ImplicitVariableResult = Builder.ParseVariableDeclaration(
		"FVector Pivot",
		ExplicitNamespace,
		ImplicitVariableName,
		ImplicitVariableNamespace,
		ImplicitVariableType);
	if (!TestEqual(
			TEXT("Builder variable/property parsing test should parse an implicitly namespaced variable declaration"),
			ImplicitVariableResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the implicitly scoped variable name"),
		ToFString(ImplicitVariableName.AddressOf()),
		FString(TEXT("Pivot")));
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should inherit the implicit namespace"),
		ImplicitVariableNamespace,
		ExplicitNamespace);
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the implicitly scoped variable type"),
		static_cast<const asITypeInfo*>(ImplicitVariableType.GetTypeInfo()),
		static_cast<const asITypeInfo*>(FVectorType));

	asCString PropertyName;
	asCDataType PropertyType;
	const int PropertyResult = Builder.VerifyProperty(
		nullptr,
		"FVector CachedValue",
		PropertyName,
		PropertyType,
		ExplicitNamespace);
	if (!TestEqual(
			TEXT("Builder variable/property parsing test should verify a plain value property declaration"),
			PropertyResult,
			static_cast<int32>(asSUCCESS)))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the verified property name"),
		ToFString(PropertyName.AddressOf()),
		FString(TEXT("CachedValue")));
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should preserve the verified property type"),
		static_cast<const asITypeInfo*>(PropertyType.GetTypeInfo()),
		static_cast<const asITypeInfo*>(FVectorType));
	bPassed &= TestFalse(
		TEXT("Builder variable/property parsing test should keep the verified property off the reference path"),
		PropertyType.IsReference());

	Builder.silent = true;
	const int FuncdefRegistrationResult = ScriptEngine->RegisterFuncdef("void BuilderFallback074_Callback()");
	bPassed &= TestTrue(
		TEXT("Builder variable/property parsing test should register the dedicated funcdef"),
		FuncdefRegistrationResult >= 0);

	asCString InvalidFuncdefPropertyName;
	asCDataType InvalidFuncdefPropertyType;
	const int InvalidFuncdefPropertyResult = Builder.VerifyProperty(
		nullptr,
		"BuilderFallback074_Callback Callback",
		InvalidFuncdefPropertyName,
		InvalidFuncdefPropertyType,
		ScriptEngine->defaultNamespace);
	bPassed &= TestEqual(
		TEXT("Builder variable/property parsing test should reject funcdef properties that are not handles"),
		InvalidFuncdefPropertyResult,
		static_cast<int32>(asINVALID_DECLARATION));

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
