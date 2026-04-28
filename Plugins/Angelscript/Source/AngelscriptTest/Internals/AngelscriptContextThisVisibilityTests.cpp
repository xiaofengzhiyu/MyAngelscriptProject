#include "Shared/AngelscriptScenarioTestUtils.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "ClassGenerator/ASClass.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/GarbageCollection.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;
using namespace AngelscriptScenarioTestUtils;

namespace AngelscriptTest_Internals_AngelscriptContextThisVisibilityTests_Private
{
	static const FName ModuleName(TEXT("ASContextThisVisibility"));
	static const FString ScriptFilename(TEXT("ASContextThisVisibility.as"));
	static const FName GeneratedClassName(TEXT("UContextThisVisibilityProbe"));

	FString ToFString(const char* Value)
	{
		return Value != nullptr ? UTF8_TO_TCHAR(Value) : FString();
	}

	struct FThisVisibilityFrameSnapshot
	{
		void Reset()
		{
			bFound = false;
			Declaration.Reset();
			TypeName.Reset();
			ThisTypeId = asINVALID_ARG;
			ThisPointer = nullptr;
			bTypeMatchesExpected = false;
			bPointerMatchesExpected = false;
			bLocalValueInScope = false;
			bLocalValueAddressable = false;
		}

		bool bFound = false;
		FString Declaration;
		FString TypeName;
		int32 ThisTypeId = asINVALID_ARG;
		void* ThisPointer = nullptr;
		bool bTypeMatchesExpected = false;
		bool bPointerMatchesExpected = false;
		bool bLocalValueInScope = false;
		bool bLocalValueAddressable = false;
	};

	struct FThisVisibilityProbe
	{
		void Reset()
		{
			InvocationCount = 0;
			CallstackSize = 0;
			MethodFrame.Reset();
		}

		int32 InvocationCount = 0;
		asUINT CallstackSize = 0;
		FThisVisibilityFrameSnapshot MethodFrame;
	};

	thread_local FThisVisibilityProbe* GActiveThisVisibilityProbe = nullptr;
	thread_local void* GExpectedThisPointer = nullptr;
	thread_local asITypeInfo* GExpectedTypeInfo = nullptr;

	struct FScopedThisVisibilityProbeBinding
	{
		FScopedThisVisibilityProbeBinding(FThisVisibilityProbe& InProbe, void* InExpectedThisPointer, asITypeInfo* InExpectedTypeInfo)
			: PreviousProbe(GActiveThisVisibilityProbe)
			, PreviousExpectedThisPointer(GExpectedThisPointer)
			, PreviousExpectedTypeInfo(GExpectedTypeInfo)
		{
			GActiveThisVisibilityProbe = &InProbe;
			GExpectedThisPointer = InExpectedThisPointer;
			GExpectedTypeInfo = InExpectedTypeInfo;
			GActiveThisVisibilityProbe->Reset();
		}

		~FScopedThisVisibilityProbeBinding()
		{
			GActiveThisVisibilityProbe = PreviousProbe;
			GExpectedThisPointer = PreviousExpectedThisPointer;
			GExpectedTypeInfo = PreviousExpectedTypeInfo;
		}

		FThisVisibilityProbe* PreviousProbe = nullptr;
		void* PreviousExpectedThisPointer = nullptr;
		asITypeInfo* PreviousExpectedTypeInfo = nullptr;
	};

	UASClass* CompileContextThisVisibilityProbeClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const FString ScriptSource = TEXT(R"AS(
UCLASS()
class UContextThisVisibilityProbe : UObject
{
	UFUNCTION()
	int Inspect()
	{
		int LocalValue = 40;
		return CaptureThisFrame(LocalValue);
	}
}
)AS");

		UClass* GeneratedClass = CompileScriptModule(
			Test,
			Engine,
			ModuleName,
			ScriptFilename,
			ScriptSource,
			GeneratedClassName);
		return Cast<UASClass>(GeneratedClass);
	}

	bool InvokeIntFunctionThroughProcessEvent(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		UObject* Object,
		UFunction* Function,
		int32& OutReturnValue)
	{
		FIntProperty* ReturnProperty = FindFProperty<FIntProperty>(Function, TEXT("ReturnValue"));
		if (!Test.TestNotNull(TEXT("Context.ThisVisibility should expose a reflected ReturnValue property"), ReturnProperty))
		{
			return false;
		}

		FStructOnScope Params(Function);
		void* ParamsMemory = Params.GetStructMemory();
		if (!Test.TestNotNull(TEXT("Context.ThisVisibility should allocate a reflected parameter buffer"), ParamsMemory))
		{
			return false;
		}

		FAngelscriptEngineScope FunctionScope(Engine, Object);
		Object->ProcessEvent(Function, ParamsMemory);

		OutReturnValue = ReturnProperty->GetPropertyValue_InContainer(ParamsMemory);
		return true;
	}

	void CaptureLocalVisibility(asCContext& Context, asUINT StackLevel, FThisVisibilityFrameSnapshot& Snapshot)
	{
		const int32 VarCount = Context.GetVarCount(StackLevel);
		for (int32 VarIndex = 0; VarIndex < VarCount; ++VarIndex)
		{
			if (ToFString(Context.GetVarName(VarIndex, StackLevel)) == TEXT("LocalValue"))
			{
				Snapshot.bLocalValueInScope = Context.IsVarInScope(VarIndex, StackLevel);
				Snapshot.bLocalValueAddressable = Context.GetAddressOfVar(VarIndex, StackLevel) != nullptr;
			}
		}
	}

	int32 CaptureThisFrame(int32 LocalValue)
	{
		FThisVisibilityProbe* Probe = GActiveThisVisibilityProbe;
		asCContext* Context = static_cast<asCContext*>(asGetActiveContext());
		if (Probe == nullptr || Context == nullptr)
		{
			return -9200;
		}

		++Probe->InvocationCount;
		Probe->CallstackSize = Context->GetCallstackSize();

		for (asUINT StackLevel = 0; StackLevel < Probe->CallstackSize; ++StackLevel)
		{
			asIScriptFunction* Function = Context->GetFunction(StackLevel);
			const FString FunctionName = ToFString(Function != nullptr ? Function->GetName() : nullptr);
			if (FunctionName != TEXT("Inspect"))
			{
				continue;
			}

			FThisVisibilityFrameSnapshot& Snapshot = Probe->MethodFrame;
			Snapshot.bFound = true;
			Snapshot.Declaration = ToFString(Function->GetDeclaration());
			Snapshot.ThisTypeId = Context->GetThisTypeId(StackLevel);
			Snapshot.ThisPointer = Context->GetThisPointer(StackLevel);

			asITypeInfo* TypeInfo = Snapshot.ThisTypeId > 0 ? Context->GetEngine()->GetTypeInfoById(Snapshot.ThisTypeId) : nullptr;
			Snapshot.TypeName = TypeInfo != nullptr ? ToFString(TypeInfo->GetName()) : FString();
			Snapshot.bTypeMatchesExpected = TypeInfo != nullptr && TypeInfo == GExpectedTypeInfo;
			Snapshot.bPointerMatchesExpected = Snapshot.ThisPointer == GExpectedThisPointer;
			CaptureLocalVisibility(*Context, StackLevel, Snapshot);
			break;
		}

		return LocalValue + 5;
	}
}

using namespace AngelscriptTest_Internals_AngelscriptContextThisVisibilityTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptContextThisVisibilityTest,
	"Angelscript.TestModule.Internals.Context.ThisVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptContextThisVisibilityTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_FRESH();
	ASTEST_BEGIN_SHARE_FRESH

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
		ResetSharedCloneEngine(Engine);
		CollectGarbage(RF_NoFlags, true);
	};

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	if (!TestNotNull(TEXT("Context.ThisVisibility should expose the backing script engine"), ScriptEngine))
	{
		return false;
	}

	const ASAutoCaller::FunctionCaller Caller = ASAutoCaller::MakeFunctionCaller(CaptureThisFrame);
	const int RegisterResult = ScriptEngine->RegisterGlobalFunction(
		"int CaptureThisFrame(int LocalValue)",
		asFUNCTION(CaptureThisFrame),
		asCALL_CDECL,
		*(asFunctionCaller*)&Caller);
	if (!TestTrue(TEXT("Context.ThisVisibility should register the native callstack-inspection helper"), RegisterResult >= 0))
	{
		return false;
	}

	UASClass* ScriptClass = CompileContextThisVisibilityProbeClass(*this, Engine);
	asITypeInfo* ScriptType = ScriptClass != nullptr ? reinterpret_cast<asITypeInfo*>(ScriptClass->ScriptTypePtr) : nullptr;
	UFunction* InspectFunction = ScriptClass != nullptr ? FindGeneratedFunction(ScriptClass, TEXT("Inspect")) : nullptr;
	UObject* Instance = ScriptClass != nullptr
		? NewObject<UObject>(GetTransientPackage(), ScriptClass, TEXT("ContextThisVisibilityInstance"))
		: nullptr;

	if (!TestNotNull(TEXT("Context.ThisVisibility should compile to a generated UASClass"), ScriptClass) ||
		!TestNotNull(TEXT("Context.ThisVisibility should publish a script type for the generated class"), ScriptType) ||
		!TestNotNull(TEXT("Context.ThisVisibility should generate the Inspect function"), InspectFunction) ||
		!TestNotNull(TEXT("Context.ThisVisibility should instantiate the generated UObject"), Instance))
	{
		return false;
	}

	FThisVisibilityProbe Probe;
	int32 ReturnValue = INDEX_NONE;
	{
		FScopedThisVisibilityProbeBinding ProbeBinding(Probe, Instance, ScriptType);
		bPassed &= TestTrue(
			TEXT("Context.ThisVisibility should execute the generated Inspect method through ProcessEvent"),
			InvokeIntFunctionThroughProcessEvent(*this, Engine, Instance, InspectFunction, ReturnValue));
	}

	bPassed &= TestEqual(
		TEXT("Context.ThisVisibility should preserve the helper-computed return value"),
		ReturnValue,
		45);
	bPassed &= TestEqual(
		TEXT("Context.ThisVisibility should invoke the native helper exactly once"),
		Probe.InvocationCount,
		1);
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should expose at least one active frame while the method calls the native helper"),
		Probe.CallstackSize >= 1);
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should find the generated Inspect method frame in the active callstack"),
		Probe.MethodFrame.bFound);
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should keep a matching this type id on the generated method frame"),
		Probe.MethodFrame.bTypeMatchesExpected);
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should keep the exact generated UObject pointer as the active this pointer"),
		Probe.MethodFrame.bPointerMatchesExpected);
	bPassed &= TestEqual(
		TEXT("Context.ThisVisibility should resolve the method frame to the generated script type"),
		Probe.MethodFrame.TypeName,
		ScriptClass->GetName());
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should keep the LocalValue local visible and in scope on the method frame"),
		Probe.MethodFrame.bLocalValueInScope);
	bPassed &= TestTrue(
		TEXT("Context.ThisVisibility should keep the LocalValue local addressable on the method frame"),
		Probe.MethodFrame.bLocalValueAddressable);

	ASTEST_END_SHARE_FRESH
	return bPassed;
}

#endif
