#include "AngelscriptEngine.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "StaticJIT/AngelscriptBytecodes.h"
#include "StaticJIT/StaticJITBinds.h"

#if WITH_DEV_AUTOMATION_TESTS && AS_CAN_GENERATE_JIT

namespace AngelscriptStaticJITNativeFormTests_Private
{
	static FString MakeUniqueModuleName(const TCHAR* Prefix)
	{
		return FString::Printf(TEXT("%s_%s"), Prefix, *FGuid::NewGuid().ToString(EGuidFormats::Digits));
	}

	static TUniquePtr<FAngelscriptEngine> CreateTestEngine(FAutomationTestBase& Test)
	{
		FAngelscriptEngineConfig Config;
		Config.bGeneratePrecompiledData = true;

		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateTestingFullEngine(Config, Dependencies);
		Test.TestNotNull(TEXT("StaticJIT native-form tests should create an isolated testing engine"), Engine.Get());
		return Engine;
	}

	static asIScriptFunction* CompileFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		const FString& ModuleName,
		const ANSICHAR* Source,
		const ANSICHAR* Declaration,
		asIScriptModule*& OutModule)
	{
		FAngelscriptEngineScope EngineScope(Engine);

		OutModule = Engine.GetScriptEngine()->GetModule(TCHAR_TO_ANSI(*ModuleName), asGM_ALWAYS_CREATE);
		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT native-form helper should create module '%s'"), *ModuleName), OutModule))
		{
			return nullptr;
		}

		asIScriptFunction* Function = nullptr;
		const int32 CompileResult = OutModule->CompileFunction(TCHAR_TO_ANSI(*ModuleName), Source, 0, 0, &Function);
		if (!Test.TestEqual(*FString::Printf(TEXT("StaticJIT native-form helper should compile '%s'"), *ModuleName), CompileResult, asSUCCESS))
		{
			return nullptr;
		}

		if (!Test.TestNotNull(*FString::Printf(TEXT("StaticJIT native-form helper should resolve '%s'"), ANSI_TO_TCHAR(Declaration)), Function))
		{
			return nullptr;
		}

		return Function;
	}

	static asIScriptFunction* FindBehaviourByParamCount(
		FAutomationTestBase& Test,
		asITypeInfo& TypeInfo,
		asEBehaviours Behaviour,
		asUINT ParamCount)
	{
		TArray<FString> MatchingDeclarations;

		for (asUINT BehaviourIndex = 0; BehaviourIndex < TypeInfo.GetBehaviourCount(); ++BehaviourIndex)
		{
			asEBehaviours ResolvedBehaviour = asBEHAVE_CONSTRUCT;
			asIScriptFunction* Candidate = TypeInfo.GetBehaviourByIndex(BehaviourIndex, &ResolvedBehaviour);
			if (ResolvedBehaviour != Behaviour || Candidate == nullptr)
			{
				continue;
			}

			const FString CandidateDeclaration = UTF8_TO_TCHAR(Candidate->GetDeclaration());
			MatchingDeclarations.Add(CandidateDeclaration);
			if (Candidate->GetParamCount() == ParamCount)
			{
				return Candidate;
			}
		}

		Test.AddError(FString::Printf(
			TEXT("StaticJIT native-form helper could not resolve a behaviour with %u params on type '%hs'. Seen: %s"),
			ParamCount,
			TypeInfo.GetName(),
			MatchingDeclarations.IsEmpty() ? TEXT("<none>") : *FString::Join(MatchingDeclarations, TEXT(" | "))));
		return nullptr;
	}
}

using namespace AngelscriptStaticJITNativeFormTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITNativeFormFIntPointBindingsTest,
	"Angelscript.CppTests.StaticJIT.NativeForm.FIntPointBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITNativeFormRejectsPureScriptFunctionTest,
	"Angelscript.CppTests.StaticJIT.NativeForm.RejectsPureScriptFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStaticJITNativeFormFIntPointBindingsTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	FAngelscriptEngineScope EngineScope(*OwnedEngine);
	asIScriptEngine* ScriptEngine = OwnedEngine->GetScriptEngine();
	if (!TestNotNull(TEXT("Native-form tests should expose the backing script engine"), ScriptEngine))
	{
		return false;
	}

	asITypeInfo* IntPointType = ScriptEngine->GetTypeInfoByDecl("FIntPoint");
	if (!TestNotNull(TEXT("Native-form tests should resolve the bound FIntPoint type"), IntPointType))
	{
		return false;
	}

	asIScriptFunction* DefaultConstructor = FindBehaviourByParamCount(*this, *IntPointType, asBEHAVE_CONSTRUCT, 0);
	asIScriptFunction* SizeMethod = IntPointType->GetMethodByDecl("int32 Size() const");
	if (!TestNotNull(TEXT("Native-form tests should resolve the trivial FIntPoint default constructor"), DefaultConstructor)
		|| !TestNotNull(TEXT("Native-form tests should resolve the trivial FIntPoint::Size binding"), SizeMethod))
	{
		return false;
	}

	FScriptFunctionNativeForm* ConstructorNativeForm = FScriptFunctionNativeForm::GetNativeForm(DefaultConstructor);
	FScriptFunctionNativeForm* SizeMethodNativeForm = FScriptFunctionNativeForm::GetNativeForm(SizeMethod);
	if (!TestNotNull(TEXT("Native-form tests should resolve a native form for the FIntPoint default constructor"), ConstructorNativeForm)
		|| !TestNotNull(TEXT("Native-form tests should resolve a native form for FIntPoint::Size"), SizeMethodNativeForm))
	{
		return false;
	}

	if (!TestTrue(TEXT("Constructor native forms should ignore the script object argument"), ConstructorNativeForm->ShouldIgnoreObjectArgument())
		|| !TestTrue(TEXT("Constructor native forms should be trivial for native calls"), ConstructorNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::NativeCall))
		|| !TestTrue(TEXT("Constructor native forms should be trivial for custom calls"), ConstructorNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::CustomCall))
		|| !TestFalse(TEXT("Constructor native forms should not report pointer-call triviality"), ConstructorNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::PointerCall)))
	{
		return false;
	}

	FNativeFunctionContext ConstructorContext;
	ConstructorContext.ObjectAddress = TEXT("Dest");
	const FNativeFunctionCall ConstructorCall = ConstructorNativeForm->GenerateCall(ConstructorContext);
	if (!TestTrue(TEXT("Constructor native forms should remain callable as direct native binds"), ConstructorNativeForm->CanCallNative(ConstructorContext))
		|| !TestEqual(TEXT("Constructor native forms should emit the trivial placement-new call"), ConstructorCall.CallCode, FString(TEXT("new (Dest) FIntPoint(0)")))
		|| !TestTrue(TEXT("Constructor native forms should not need extra include headers"), ConstructorCall.Header.IsEmpty()))
	{
		return false;
	}

	if (!TestTrue(TEXT("Method native forms should ignore the script object argument"), SizeMethodNativeForm->ShouldIgnoreObjectArgument())
		|| !TestTrue(TEXT("Method native forms should be trivial for native calls"), SizeMethodNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::NativeCall))
		|| !TestTrue(TEXT("Method native forms should be trivial for custom calls"), SizeMethodNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::CustomCall))
		|| !TestTrue(TEXT("Method native forms should be trivial for pointer calls"), SizeMethodNativeForm->IsTrivialFunction(EScriptFunctionCallMethod::PointerCall)))
	{
		return false;
	}

	FNativeFunctionContext MethodContext;
	MethodContext.ObjectAddress = TEXT("PointPtr");
	const FNativeFunctionCall MethodCall = SizeMethodNativeForm->GenerateCall(MethodContext);
	return TestTrue(TEXT("Method native forms should remain callable as direct native binds"), SizeMethodNativeForm->CanCallNative(MethodContext))
		&& TestEqual(TEXT("Method native forms should emit the expected member call"), MethodCall.CallCode, FString(TEXT("PointPtr->Size()")))
		&& TestTrue(TEXT("Method native forms should not need extra include headers"), MethodCall.Header.IsEmpty());
}

bool FAngelscriptStaticJITNativeFormRejectsPureScriptFunctionTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asIScriptModule* Module = nullptr;
	const FString ModuleName = MakeUniqueModuleName(TEXT("StaticJITNativeFormScriptOnly"));
	asIScriptFunction* ScriptFunction = CompileFunction(
		*this,
		*OwnedEngine,
		ModuleName,
		"int ScriptOnly(int Value) { return Value + 3; }",
		"int ScriptOnly(int)",
		Module);
	if (ScriptFunction == nullptr)
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		ScriptFunction->Release();
	};

	if (!TestEqual(TEXT("Pure script functions should keep the expected function kind"), static_cast<int32>(ScriptFunction->GetFuncType()), static_cast<int32>(asFUNC_SCRIPT)))
	{
		return false;
	}

	return TestNull(
		TEXT("Pure script functions should not resolve to any StaticJIT native form"),
		FScriptFunctionNativeForm::GetNativeForm(ScriptFunction));
}

#endif
