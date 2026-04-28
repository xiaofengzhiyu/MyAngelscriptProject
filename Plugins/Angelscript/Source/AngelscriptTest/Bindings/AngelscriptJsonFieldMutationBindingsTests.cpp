#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptJsonFieldMutationCompatBindingsTest,
	"Angelscript.TestModule.Bindings.JsonFieldMutationCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptJsonCopyAndArrayCompatBindingsTest,
	"Angelscript.TestModule.Bindings.JsonCopyAndArrayCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptJsonFieldMutationCompatBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASJsonFieldMutationCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASJsonFieldMutationCompat",
		TEXT(R"(
int Entry()
{
	FJsonObject Root;
	if (!Root.LoadFromString("{\"Name\":\"Alice\",\"Score\":12.5,\"Count\":42,\"Enabled\":true,\"Child\":{\"Flag\":true},\"Values\":[\"First\",42],\"NullValue\":null}"))
		return 10;
	if (!Root.IsValid())
		return 20;
	if (!Root.HasField("Name") || !Root.HasField("Values") || !Root.HasField("NullValue"))
		return 30;

	FJsonObject DirectChild;
	if (!Root.TryGetObjectField("Child", DirectChild) || !DirectChild.GetBoolField("Flag"))
		return 40;

	FJsonArray DirectValues;
	if (!Root.TryGetArrayField("Values", DirectValues) || DirectValues.Num() != 2)
		return 50;

	FString DirectFirstValue;
	if (!DirectValues.GetValueAt(0).TryGetString(DirectFirstValue) || DirectFirstValue != "First")
		return 60;

	int64 DirectSecondValue = 0;
	if (!DirectValues.GetValueAt(1).TryGetNumber(DirectSecondValue) || DirectSecondValue != 42)
		return 70;

	bool bSawName = false;
	bool bSawScore = false;
	bool bSawCount = false;
	bool bSawEnabled = false;
	bool bSawChild = false;
	bool bSawValues = false;
	bool bSawNull = false;

	{
		FJsonObjectFieldIterator Iterator = Root.Iterator();
		while (Iterator.CanProceed)
		{
			Iterator.Proceed();
			FString FieldName = Iterator.GetFieldName();
			FJsonValue Value = Iterator.GetValue();

			if (FieldName == "Name")
			{
				FString Name;
				if (!Value.TryGetString(Name) || Name != "Alice")
					return 80;
				bSawName = true;
			}
			else if (FieldName == "Score")
			{
				float64 Score64 = 0.0;
				float32 Score32 = 0.0f;
				if (!Value.TryGetNumber(Score64) || Score64 != 12.5)
					return 90;
				if (!Value.TryGetNumber(Score32) || Score32 != 12.5f)
					return 100;
				bSawScore = true;
			}
			else if (FieldName == "Count")
			{
				int64 Count64 = 0;
				if (!Value.TryGetNumber(Count64) || Count64 != 42)
					return 110;
				bSawCount = true;
			}
			else if (FieldName == "Enabled")
			{
				bool bEnabled = false;
				if (!Value.TryGetBool(bEnabled) || !bEnabled)
					return 120;
				bSawEnabled = true;
			}
			else if (FieldName == "Child")
			{
				FJsonObject Child;
				if (!Value.TryGetObject(Child) || !Child.GetBoolField("Flag"))
					return 130;
				bSawChild = true;
			}
			else if (FieldName == "Values")
			{
				FJsonArray Values;
				if (!Value.TryGetArray(Values) || Values.Num() != 2)
					return 140;
				bSawValues = true;
			}
			else if (FieldName == "NullValue")
			{
				if (Iterator.GetType() != EJsonType::Null || !Value.IsNull())
					return 150;
				bSawNull = true;
			}
		}
	}

	if (!bSawName || !bSawScore || !bSawCount || !bSawEnabled || !bSawChild || !bSawValues || !bSawNull)
		return 160;

	Root.RemoveField("Name");
	if (Root.HasField("Name"))
		return 170;

	Root.SetBoolField("Temporary", true);
	Root.RemoveAllFields();
	if (Root.HasField("Temporary") || Root.HasField("Child") || Root.HasField("Values"))
		return 180;

	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	TestEqual(TEXT("Json field mutation bindings should preserve typed value extraction and field copy/remove semantics"), Result, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptJsonCopyAndArrayCompatBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASJsonCopyAndArrayCompat"));
	};

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASJsonCopyAndArrayCompat",
		TEXT(R"(
int Entry()
{
	FJsonArray Values;
	Values.AddString("Before");
	Values.AddNumber(7);
	if (Values.Num() != 2)
		return 10;

	Values.Empty();
	if (Values.Num() != 0)
		return 20;

	Values.AddNumber(42);
	FJsonObject Root;
	Root.SetArrayField("Values", Values);

	FJsonArray StoredValues = Root.GetArrayField("Values");
	if (StoredValues.Num() != 1)
		return 30;

	int32 StoredNumber = 0;
	if (!StoredValues.GetValueAt(0).TryGetNumber(StoredNumber) || StoredNumber != 42)
		return 40;

	FJsonObject Source;
	Source.SetStringField("Name", "Original");

	FJsonObject Alias(Source);
	if (Alias.GetStringField("Name") != "Original")
		return 50;

	Alias.SetStringField("Name", "Alias");
	if (Source.GetStringField("Name") != "Alias")
		return 60;

	FJsonObject Child = Source.CreateObjectField("Child");
	Child.SetBoolField("Flag", true);
	if (!Alias.GetObjectField("Child").GetBoolField("Flag"))
		return 70;

	Alias.RemoveField("Name");
	if (Source.HasField("Name"))
		return 80;

	return 1;
}
)"));
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	TestEqual(TEXT("Json copy and array bindings should preserve array emptying and object alias-copy semantics"), Result, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
