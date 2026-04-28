#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_module.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptModuleTests_Private
{
	FString ToFString(const char* Value)
	{
		return Value != nullptr ? UTF8_TO_TCHAR(Value) : FString();
	}

	bool ContainsFragment(const TArray<FString>& Values, const TCHAR* Fragment)
	{
		return Values.ContainsByPredicate(
			[Fragment](const FString& Value)
			{
				return Value.Contains(Fragment);
			});
	}

	void CollectFunctionDeclarations(asIScriptModule& Module, TArray<FString>& OutDeclarations)
	{
		OutDeclarations.Reset();

		for (asUINT FunctionIndex = 0; FunctionIndex < Module.GetFunctionCount(); ++FunctionIndex)
		{
			asIScriptFunction* Function = Module.GetFunctionByIndex(FunctionIndex);
			if (Function != nullptr)
			{
				OutDeclarations.Add(ToFString(Function->GetDeclaration()));
			}
		}
	}

	void CollectObjectTypeNames(asIScriptModule& Module, TArray<FString>& OutTypeNames)
	{
		OutTypeNames.Reset();

		for (asUINT TypeIndex = 0; TypeIndex < Module.GetObjectTypeCount(); ++TypeIndex)
		{
			asITypeInfo* TypeInfo = Module.GetObjectTypeByIndex(TypeIndex);
			if (TypeInfo != nullptr)
			{
				OutTypeNames.Add(ToFString(TypeInfo->GetName()));
			}
		}
	}

	bool ExecuteIntFunctionExpectingUnboundDiagnostic(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptFunction& Function,
		const TCHAR* ExpectedExceptionFragment)
	{
		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(TEXT("Module import failure probe should create a script context"), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(&Function);
		bool bPassed = true;
		bPassed &= Test.TestEqual(
			TEXT("Module import failure probe should prepare the entry function successfully"),
			PrepareResult,
			static_cast<int32>(asSUCCESS));
		if (PrepareResult != asSUCCESS)
		{
			return false;
		}

		Test.AddExpectedErrorPlain(ExpectedExceptionFragment, EAutomationExpectedErrorFlags::Contains, 1);

		const FString ModuleName = ToFString(Function.GetModuleName());
		if (!ModuleName.IsEmpty())
		{
			Test.AddExpectedErrorPlain(ModuleName, EAutomationExpectedErrorFlags::Contains, 1);
		}

		const FString FunctionDeclaration = ToFString(Function.GetDeclaration());
		if (!FunctionDeclaration.IsEmpty())
		{
			Test.AddExpectedErrorPlain(FunctionDeclaration, EAutomationExpectedErrorFlags::Contains, 1);
		}

		const int ExecuteResult = Context->Execute();
		bPassed &= Test.TestTrue(
			TEXT("Module import failure probe should either raise a script exception or complete after emitting the unbound-import diagnostic"),
			ExecuteResult == asEXECUTION_EXCEPTION || ExecuteResult == asEXECUTION_FINISHED);
		const FString ExceptionString = ToFString(Context->GetExceptionString());
		bPassed &= Test.TestTrue(
			TEXT("Module import failure probe should either keep no exception string or preserve the unbound-function fragment"),
			ExceptionString.IsEmpty() || ExceptionString.Contains(ExpectedExceptionFragment));

		return bPassed;
	}
}

using namespace AngelscriptTest_Internals_AngelscriptModuleTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptModuleNamespaceAndEnumerationTest,
	"Angelscript.TestModule.Internals.Module.NamespaceAndEnumeration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptModuleImportBindingLifecycleTest,
	"Angelscript.TestModule.Internals.Module.ImportBindingLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptModuleNamespaceAndEnumerationTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asCModule* ScriptModule = static_cast<asCModule*>(BuildModule(
		*this,
		Engine,
		"ASModuleNamespaceEnumeration",
		TEXT(R"AS(
class GlobalType
{
	GlobalType() {}
}

namespace Outer
{
	class Widget
	{
		Widget() {}
	}

	const int Value = 7;

	int Compute(int Input)
	{
		return Input + Value;
	}
}

int Entry()
{
	return Outer::Compute(5);
}
)AS")));
	if (!TestNotNull(
			TEXT("Module.NamespaceAndEnumeration should compile a backing asCModule"),
			ScriptModule))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should start in the global namespace"),
		ToFString(ScriptModule->GetDefaultNamespace()),
		FString());
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should expose at least the declared namespaced script global"),
		static_cast<int32>(ScriptModule->GetGlobalVarCount()) >= 1,
		true);
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should not resolve a namespaced global from the root namespace"),
		ScriptModule->GetGlobalVarIndexByName("Value"),
		static_cast<int32>(asNO_GLOBAL_VAR));
	bPassed &= TestNull(
		TEXT("Module.NamespaceAndEnumeration should not resolve a namespaced function from the root namespace"),
		ScriptModule->GetFunctionByDecl("int Compute(int)"));
	bPassed &= TestNull(
		TEXT("Module.NamespaceAndEnumeration should not resolve a namespaced type from the root namespace"),
		ScriptModule->GetTypeInfoByName("Widget"));

	const int SetOuterNamespaceResult = ScriptModule->SetDefaultNamespace("Outer");
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should accept a module default namespace override"),
		SetOuterNamespaceResult,
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should report the active module default namespace"),
		ToFString(ScriptModule->GetDefaultNamespace()),
		FString(TEXT("Outer")));

	const int ValueIndexByName = ScriptModule->GetGlobalVarIndexByName("Value");
	const int ValueIndexByQualifiedName = ScriptModule->GetGlobalVarIndexByName("Outer::Value");
	const int ValueIndexByDecl = ScriptModule->GetGlobalVarIndexByDecl("const int Value");
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced global by unqualified name once the namespace is active"),
		ValueIndexByName >= 0);
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced global by fully qualified name"),
		ValueIndexByQualifiedName >= 0);
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced global by declaration"),
		ValueIndexByDecl >= 0);
	if (ValueIndexByName >= 0 && ValueIndexByQualifiedName >= 0 && ValueIndexByDecl >= 0)
	{
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should keep global-name and qualified-name lookups aligned"),
			ValueIndexByQualifiedName,
			ValueIndexByName);
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should keep declaration lookup aligned with name lookup"),
			ValueIndexByDecl,
			ValueIndexByName);

		const char* RawGlobalName = nullptr;
		const char* RawGlobalNamespace = nullptr;
		int GlobalTypeId = 0;
		bool bIsConst = true;
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should expose global metadata for the resolved index"),
			ScriptModule->GetGlobalVar(ValueIndexByName, &RawGlobalName, &RawGlobalNamespace, &GlobalTypeId, &bIsConst),
			static_cast<int32>(asSUCCESS));
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should preserve the global variable name"),
			ToFString(RawGlobalName),
			FString(TEXT("Value")));
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should preserve the global variable namespace"),
			ToFString(RawGlobalNamespace),
			FString(TEXT("Outer")));
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should preserve the global variable type"),
			GlobalTypeId,
			static_cast<int32>(asTYPEID_INT32));
		bPassed &= TestTrue(
			TEXT("Module.NamespaceAndEnumeration should preserve the namespaced global const qualifier"),
			bIsConst);
		bPassed &= TestTrue(
			TEXT("Module.NamespaceAndEnumeration should preserve the qualified global declaration text"),
			ToFString(ScriptModule->GetGlobalVarDeclaration(ValueIndexByName, true)).Contains(TEXT("Outer::Value")));
		bPassed &= TestTrue(
			TEXT("Module.NamespaceAndEnumeration should preserve the const qualifier in the global declaration text"),
			ToFString(ScriptModule->GetGlobalVarDeclaration(ValueIndexByName, true)).Contains(TEXT("const int")));
	}

	asIScriptFunction* ComputeFunction = ScriptModule->GetFunctionByDecl("int Outer::Compute(int)");
	if (ComputeFunction == nullptr)
	{
		ComputeFunction = ScriptModule->GetFunctionByName("Compute");
	}
	asIScriptFunction* EntryFunction = ScriptModule->GetFunctionByDecl("int Entry()");
	asITypeInfo* WidgetTypeByName = ScriptModule->GetTypeInfoByName("Widget");
	asITypeInfo* WidgetTypeByDecl = ScriptModule->GetTypeInfoByDecl("Widget");
	bPassed &= TestNotNull(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced function once the namespace is active"),
		ComputeFunction);
	bPassed &= TestNotNull(
		TEXT("Module.NamespaceAndEnumeration should keep the global entry function accessible from the parent namespace"),
		EntryFunction);
	bPassed &= TestNotNull(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced type by name once the namespace is active"),
		WidgetTypeByName);
	bPassed &= TestNotNull(
		TEXT("Module.NamespaceAndEnumeration should resolve the namespaced type by declaration once the namespace is active"),
		WidgetTypeByDecl);
	if (WidgetTypeByName != nullptr && WidgetTypeByDecl != nullptr)
	{
		bPassed &= TestEqual(
			TEXT("Module.NamespaceAndEnumeration should keep type-name and type-declaration lookup aligned"),
			WidgetTypeByDecl,
			WidgetTypeByName);
	}

	TArray<FString> FunctionDeclarations;
	CollectFunctionDeclarations(*ScriptModule, FunctionDeclarations);
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should enumerate at least the declared global and namespaced functions"),
		FunctionDeclarations.Num() >= 2);
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should enumerate the global Entry() function"),
		ContainsFragment(FunctionDeclarations, TEXT("Entry")));
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should enumerate the namespaced Compute(int) function"),
		ContainsFragment(FunctionDeclarations, TEXT("Compute")));

	TArray<FString> ObjectTypeNames;
	CollectObjectTypeNames(*ScriptModule, ObjectTypeNames);
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should enumerate both script object types"),
		ObjectTypeNames.Num(),
		2);
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should enumerate the global script type"),
		ObjectTypeNames.Contains(TEXT("GlobalType")));
	bPassed &= TestTrue(
		TEXT("Module.NamespaceAndEnumeration should enumerate the namespaced script type"),
		ObjectTypeNames.Contains(TEXT("Widget")));

	int32 EntryResult = 0;
	if (EntryFunction != nullptr)
	{
		const bool bExecuted = ExecuteIntFunction(*this, Engine, *EntryFunction, EntryResult);
		bPassed &= TestTrue(
			TEXT("Module.NamespaceAndEnumeration should execute the global entry point successfully"),
			bExecuted);
		if (bExecuted)
		{
			bPassed &= TestEqual(
				TEXT("Module.NamespaceAndEnumeration should keep namespace-qualified global execution semantics intact"),
				EntryResult,
				12);
		}
	}

	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should restore the root namespace on demand"),
		ScriptModule->SetDefaultNamespace(""),
		static_cast<int32>(asSUCCESS));
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should report the restored root namespace"),
		ToFString(ScriptModule->GetDefaultNamespace()),
		FString());
	bPassed &= TestEqual(
		TEXT("Module.NamespaceAndEnumeration should keep fully qualified global lookup stable after restoring the root namespace"),
		ScriptModule->GetGlobalVarIndexByName("Outer::Value"),
		ValueIndexByQualifiedName);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptModuleImportBindingLifecycleTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_CLONE();
	ASTEST_BEGIN_CLONE

	static constexpr ANSICHAR SourceModuleName[] = "ASModuleImportSource";
	static constexpr ANSICHAR ConsumerModuleName[] = "ASModuleImportConsumer";

	asCModule* SourceModule = static_cast<asCModule*>(BuildModule(
		*this,
		Engine,
		SourceModuleName,
		TEXT(R"AS(
int SharedValue()
{
	return 77;
}
)AS")));
	asCModule* ConsumerModule = static_cast<asCModule*>(BuildModule(
		*this,
		Engine,
		ConsumerModuleName,
		TEXT(R"AS(
import int SharedValue() from "ASModuleImportSource";

int Entry()
{
	return SharedValue();
}
)AS")));
	if (!TestNotNull(
			TEXT("Module.ImportBindingLifecycle should compile the source module"),
			SourceModule)
		|| !TestNotNull(
			TEXT("Module.ImportBindingLifecycle should compile the consumer module"),
			ConsumerModule))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should preserve one declared imported function on the consumer module"),
		static_cast<int32>(ConsumerModule->GetImportedFunctionCount()),
		1);

	const int ImportedFunctionIndex = ConsumerModule->GetImportedFunctionIndexByDecl("int SharedValue()");
	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should resolve the imported function by declaration"),
		ImportedFunctionIndex,
		0);
	if (ImportedFunctionIndex >= 0)
	{
		bPassed &= TestEqual(
			TEXT("Module.ImportBindingLifecycle should preserve the source-module metadata for the import"),
			ToFString(ConsumerModule->GetImportedFunctionSourceModule(ImportedFunctionIndex)),
			FString(TEXT("ASModuleImportSource")));
		bPassed &= TestEqual(
			TEXT("Module.ImportBindingLifecycle should preserve the imported function declaration text"),
			ToFString(ConsumerModule->GetImportedFunctionDeclaration(ImportedFunctionIndex)),
			FString(TEXT("int SharedValue()")));
	}

	asIScriptFunction* SourceFunction = SourceModule->GetFunctionByDecl("int SharedValue()");
	asIScriptFunction* EntryFunction = ConsumerModule->GetFunctionByDecl("int Entry()");
	if (!TestNotNull(
			TEXT("Module.ImportBindingLifecycle should resolve the source function for manual binding"),
			SourceFunction)
		|| !TestNotNull(
			TEXT("Module.ImportBindingLifecycle should resolve the consumer entry function"),
			EntryFunction))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should report that stock BindAllImportedFunctions cannot resolve the wrapper-managed provider module in this branch"),
		ConsumerModule->BindAllImportedFunctions(),
		static_cast<int32>(asCANT_BIND_ALL_FUNCTIONS));
	bPassed &= ExecuteIntFunctionExpectingUnboundDiagnostic(
		*this,
		Engine,
		*EntryFunction,
		TEXT("Unbound function called"));

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should allow manual binding of the imported function"),
		ConsumerModule->BindImportedFunction(ImportedFunctionIndex, SourceFunction),
		static_cast<int32>(asSUCCESS));

	int32 FirstResult = 0;
	const bool bFirstExecuteSucceeded = ExecuteIntFunction(*this, Engine, *EntryFunction, FirstResult);
	bPassed &= TestTrue(
		TEXT("Module.ImportBindingLifecycle should execute the consumer entry point after manual binding"),
		bFirstExecuteSucceeded);
	if (bFirstExecuteSucceeded)
	{
		bPassed &= TestEqual(
			TEXT("Module.ImportBindingLifecycle should route the consumer through the manually bound imported function"),
			FirstResult,
			77);
	}

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should unbind an imported function by index"),
		ConsumerModule->UnbindImportedFunction(ImportedFunctionIndex),
		static_cast<int32>(asSUCCESS));
	bPassed &= ExecuteIntFunctionExpectingUnboundDiagnostic(
		*this,
		Engine,
		*EntryFunction,
		TEXT("Unbound function called"));

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should allow manual rebind of the imported function"),
		ConsumerModule->BindImportedFunction(ImportedFunctionIndex, SourceFunction),
		static_cast<int32>(asSUCCESS));

	int32 ReboundResult = 0;
	const bool bReboundExecuteSucceeded = ExecuteIntFunction(*this, Engine, *EntryFunction, ReboundResult);
	bPassed &= TestTrue(
		TEXT("Module.ImportBindingLifecycle should execute again after manual rebind"),
		bReboundExecuteSucceeded);
	if (bReboundExecuteSucceeded)
	{
		bPassed &= TestEqual(
			TEXT("Module.ImportBindingLifecycle should preserve the imported-call result after manual rebind"),
			ReboundResult,
			77);
	}

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should unbind all imported functions in one call"),
		ConsumerModule->UnbindAllImportedFunctions(),
		static_cast<int32>(asSUCCESS));
	bPassed &= ExecuteIntFunctionExpectingUnboundDiagnostic(
		*this,
		Engine,
		*EntryFunction,
		TEXT("Unbound function called"));

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should keep reporting unresolved imports from stock BindAllImportedFunctions after a bulk unbind"),
		ConsumerModule->BindAllImportedFunctions(),
		static_cast<int32>(asCANT_BIND_ALL_FUNCTIONS));
	bPassed &= ExecuteIntFunctionExpectingUnboundDiagnostic(
		*this,
		Engine,
		*EntryFunction,
		TEXT("Unbound function called"));

	bPassed &= TestEqual(
		TEXT("Module.ImportBindingLifecycle should still allow manual rebind after a failed BindAllImportedFunctions attempt"),
		ConsumerModule->BindImportedFunction(ImportedFunctionIndex, SourceFunction),
		static_cast<int32>(asSUCCESS));

	int32 FinalResult = 0;
	const bool bFinalExecuteSucceeded = ExecuteIntFunction(*this, Engine, *EntryFunction, FinalResult);
	bPassed &= TestTrue(
		TEXT("Module.ImportBindingLifecycle should execute successfully after the final manual rebind"),
		bFinalExecuteSucceeded);
	if (bFinalExecuteSucceeded)
	{
		bPassed &= TestEqual(
			TEXT("Module.ImportBindingLifecycle should preserve the imported-call result after the final manual rebind"),
			FinalResult,
			77);
	}

	ASTEST_END_CLONE
	return bPassed;
}

#endif
