#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "Misc/AutomationTest.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_datatype.h"
#include "source/as_module.h"
#include "source/as_scriptengine.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Internals_AngelscriptDataTypeTests_Private
{
	int GetPropertyTypeIdByName(FAutomationTestBase& Test, asITypeInfo& ScriptType, const char* PropertyName)
	{
		for (asUINT PropertyIndex = 0, PropertyCount = ScriptType.GetPropertyCount(); PropertyIndex < PropertyCount; ++PropertyIndex)
		{
			const char* ActualName = nullptr;
			int TypeId = asINVALID_TYPE;
			ScriptType.GetProperty(PropertyIndex, &ActualName, &TypeId);
			if (ActualName != nullptr && FCStringAnsi::Strcmp(ActualName, PropertyName) == 0)
			{
				return TypeId;
			}
		}

		Test.AddError(FString::Printf(
			TEXT("Compiled script type '%s' should expose property '%hs'"),
			UTF8_TO_TCHAR(ScriptType.GetName()),
			PropertyName));
		return asINVALID_TYPE;
	}
}

using namespace AngelscriptTest_Internals_AngelscriptDataTypeTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypePrimitiveTest,
	"Angelscript.TestModule.Internals.DataType.Primitives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypeComparisonTest,
	"Angelscript.TestModule.Internals.DataType.Comparisons",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypeHandleQualifierMatrixTest,
	"Angelscript.TestModule.Internals.DataType.Comparisons.HandleQualifierMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypeTemplateSubtypeMatrixTest,
	"Angelscript.TestModule.Internals.DataType.TemplateSubtypeMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypeObjectHandleTest,
	"Angelscript.TestModule.Internals.DataType.ObjectHandles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptDataTypeSizeAlignmentTest,
	"Angelscript.TestModule.Internals.DataType.SizeAndAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptDataTypePrimitiveTest::RunTest(const FString& Parameters)
{
	asCDataType IntType = asCDataType::CreatePrimitive(ttInt, false);
	asCDataType FloatType = asCDataType::CreatePrimitive(ttFloat32, false);
	asCDataType BoolType = asCDataType::CreatePrimitive(ttBool, false);
	asCDataType VoidType = asCDataType::CreatePrimitive(ttVoid, false);
	asCDataType NullHandleType = asCDataType::CreateNullHandle();

	TestTrue(TEXT("int data type should be valid and primitive"), IntType.IsValid() && IntType.IsPrimitive() && IntType.IsIntegerType());
	TestTrue(TEXT("float32 data type should report float semantics"), FloatType.IsPrimitive() && FloatType.IsFloat32Type() && FloatType.IsMathType());
	TestTrue(TEXT("bool data type should report boolean semantics"), BoolType.IsPrimitive() && BoolType.IsBooleanType());
	TestFalse(TEXT("void data type should not be instantiable"), VoidType.CanBeInstantiated());
	TestTrue(TEXT("null handle data type should report object-handle semantics"), NullHandleType.IsNullHandle() && NullHandleType.IsObjectHandle());
	return true;
}

bool FAngelscriptDataTypeComparisonTest::RunTest(const FString& Parameters)
{
	asCDataType MutableInt = asCDataType::CreatePrimitive(ttInt, false);
	asCDataType ConstInt = asCDataType::CreatePrimitive(ttInt, true);
	asCDataType RefInt = asCDataType::CreatePrimitive(ttInt, false);
	RefInt.MakeReference(true);

	TestTrue(TEXT("Constness should be ignored by IsEqualExceptConst"), MutableInt.IsEqualExceptConst(ConstInt));
	TestFalse(TEXT("Constness should still matter for exact equality"), MutableInt == ConstInt);
	TestTrue(TEXT("Reference-ness should be ignored by IsEqualExceptRef"), MutableInt.IsEqualExceptRef(RefInt));
	TestFalse(TEXT("Reference-ness should still matter for exact equality"), MutableInt == RefInt);
	return true;
}

bool FAngelscriptDataTypeHandleQualifierMatrixTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	asCTypeInfo* ActorType = static_cast<asCTypeInfo*>(ScriptEngine->GetTypeInfoByName("AActor"));
	if (!TestNotNull(TEXT("AActor should exist in the script type system for handle qualifier comparisons"), ActorType))
	{
		return false;
	}

	asCDataType ActorValueType = asCDataType::CreateType(ActorType, false);
	asCDataType ActorHandleType = asCDataType::CreateObjectHandle(ActorType, false);
	asCDataType ConstActorHandleType = asCDataType::CreateObjectHandle(ActorType, true);
	asCDataType RefConstActorHandleType = ConstActorHandleType;
	RefConstActorHandleType.MakeReference(true);
	asCDataType NullHandleType = asCDataType::CreateNullHandle();

	if (!TestTrue(TEXT("Object handle matrix should preserve the target type info"), ActorHandleType.GetTypeInfo() == ActorType))
	{
		return false;
	}
	if (!TestFalse(TEXT("Exact equality should distinguish mutable and const handles"), ActorHandleType == ConstActorHandleType))
	{
		return false;
	}
	if (!TestTrue(TEXT("IsEqualExceptConst should ignore handle constness"), ActorHandleType.IsEqualExceptConst(ConstActorHandleType)))
	{
		return false;
	}
	if (!TestTrue(TEXT("IsEqualExceptRefAndConst should ignore both reference and const on handles"), ActorHandleType.IsEqualExceptRefAndConst(RefConstActorHandleType)))
	{
		return false;
	}
	if (!TestFalse(TEXT("Null handle should not be exactly equal to a typed object handle"), NullHandleType == ActorHandleType))
	{
		return false;
	}
	if (!TestTrue(TEXT("Null handle should still report object-handle semantics"), NullHandleType.IsObjectHandle() && NullHandleType.IsNullHandle()))
	{
		return false;
	}
	if (!TestTrue(TEXT("Value type and object handle should keep different kind semantics"), ActorValueType.IsObject() && ActorHandleType.IsObjectHandle()))
	{
		return false;
	}

	ASTEST_END_SHARE_CLEAN
	return true;
}

bool FAngelscriptDataTypeTemplateSubtypeMatrixTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = AngelscriptTestSupport::BuildModule(
		*this,
		Engine,
		"ASDataTypeTemplateSubtypeMatrix",
		TEXT(R"ANGELSCRIPT(
class FDataTypeSubtypeCarrier
{
	TArray<int> Numbers;
	TMap<FName, int> NameCounts;
}
)ANGELSCRIPT"));

	asIScriptEngine* ScriptEngine = Engine.GetScriptEngine();
	bPassed &= TestNotNull(TEXT("Template subtype matrix test should expose a script engine"), ScriptEngine);
	bPassed &= TestNotNull(TEXT("Template subtype matrix test should compile the carrier module"), Module);

	if (Module != nullptr && ScriptEngine != nullptr)
	{
		asITypeInfo* CarrierType = Module->GetTypeInfoByName("FDataTypeSubtypeCarrier");
		bPassed &= TestNotNull(TEXT("Template subtype matrix test should expose the carrier type"), CarrierType);

		if (CarrierType != nullptr)
		{
			const int NumbersTypeId = GetPropertyTypeIdByName(*this, *CarrierType, "Numbers");
			const int NameCountsTypeId = GetPropertyTypeIdByName(*this, *CarrierType, "NameCounts");

			asITypeInfo* NumbersTypeInfo = (NumbersTypeId != asINVALID_TYPE) ? ScriptEngine->GetTypeInfoById(NumbersTypeId) : nullptr;
			asITypeInfo* NameCountsTypeInfo = (NameCountsTypeId != asINVALID_TYPE) ? ScriptEngine->GetTypeInfoById(NameCountsTypeId) : nullptr;
			asCTypeInfo* FNameTypeInfo = static_cast<asCTypeInfo*>(ScriptEngine->GetTypeInfoByName("FName"));

			bPassed &= TestNotNull(TEXT("Template subtype matrix test should resolve the TArray<int> type"), NumbersTypeInfo);
			bPassed &= TestNotNull(TEXT("Template subtype matrix test should resolve the TMap<FName, int> type"), NameCountsTypeInfo);
			bPassed &= TestNotNull(TEXT("Template subtype matrix test should resolve the native FName type"), FNameTypeInfo);

			if (NumbersTypeInfo != nullptr && NameCountsTypeInfo != nullptr && FNameTypeInfo != nullptr)
			{
				asCDataType ExpectedIntType = asCDataType::CreatePrimitive(ttInt, false);

				asCDataType DynamicArrayType = asCDataType::CreatePrimitive(ttInt, false);
				const int MakeArrayResult = DynamicArrayType.MakeArray(
					static_cast<asCScriptEngine*>(ScriptEngine),
					static_cast<asCModule*>(Module));

				bPassed &= TestEqual(TEXT("MakeArray should build an instantiated TArray<int> data type"), MakeArrayResult, static_cast<int>(asSUCCESS));
				bPassed &= TestEqual(TEXT("MakeArray should resolve to the same TArray<int> type instance used by script properties"), DynamicArrayType.GetTypeInfo(), static_cast<asCTypeInfo*>(NumbersTypeInfo));
				bPassed &= TestEqual(TEXT("The instantiated TArray<int> type should expose exactly one template subtype"), static_cast<int32>(NumbersTypeInfo->GetSubTypeCount()), 1);
				bPassed &= TestTrue(TEXT("The TArray<int> subtype should stay the original int primitive"), DynamicArrayType.GetSubType() == ExpectedIntType);

				asCDataType MapType = asCDataType::CreateType(static_cast<asCTypeInfo*>(NameCountsTypeInfo), false);
				asCDataType QualifiedMapType = MapType;
				QualifiedMapType.MakeReference(true);
				QualifiedMapType.MakeReadOnly(true);

				const asCDataType MapKeyType = MapType.GetSubType(0);
				const asCDataType MapValueType = MapType.GetSubType(1);

				bPassed &= TestEqual(TEXT("The instantiated TMap<FName, int> type should expose exactly two template subtypes"), static_cast<int32>(NameCountsTypeInfo->GetSubTypeCount()), 2);
				bPassed &= TestEqual(TEXT("The TMap key subtype should resolve to FName"), MapKeyType.GetTypeInfo(), FNameTypeInfo);
				bPassed &= TestTrue(TEXT("The TMap value subtype should stay the original int primitive"), MapValueType == ExpectedIntType);
				bPassed &= TestTrue(TEXT("Outer map qualifiers should preserve subtype identity"), QualifiedMapType.IsEqualExceptRefAndConst(MapType));
				bPassed &= TestEqual(TEXT("Qualified map key subtype should match the unqualified subtype"), QualifiedMapType.GetSubType(0).GetTypeInfo(), MapKeyType.GetTypeInfo());
				bPassed &= TestTrue(TEXT("Qualified map value subtype should match the unqualified subtype"), QualifiedMapType.GetSubType(1) == MapValueType);
				bPassed &= TestTrue(TEXT("Qualified map type should preserve the requested reference and const qualifiers"), QualifiedMapType.IsReference() && QualifiedMapType.IsReadOnly());
			}
			else
			{
				bPassed = false;
			}
		}
		else
		{
			bPassed = false;
		}
	}
	else
	{
		bPassed = false;
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptDataTypeObjectHandleTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	asCTypeInfo* ActorType = static_cast<asCTypeInfo*>(ScriptEngine->GetTypeInfoByName("AActor"));
	if (!TestNotNull(TEXT("AActor should exist in the script type system for data-type handle tests"), ActorType))
	{
		return false;
	}

	asCDataType ActorValueType = asCDataType::CreateType(ActorType, false);
	TestTrue(TEXT("AActor value type should be recognized as an object type"), ActorValueType.IsObject());
	TestTrue(TEXT("AActor value type should support handles"), ActorValueType.SupportHandles());

	asCDataType ActorHandleType = asCDataType::CreateObjectHandle(ActorType, false);
	TestTrue(TEXT("CreateObjectHandle should mark the type as an object handle"), ActorHandleType.IsObjectHandle());
	TestTrue(TEXT("Object handle should still be considered instantiable as a handle slot"), ActorHandleType.CanBeInstantiated());
	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptDataTypeSizeAlignmentTest::RunTest(const FString& Parameters)
{
	asCDataType IntType = asCDataType::CreatePrimitive(ttInt, false);
	asCDataType Float64Type = asCDataType::CreatePrimitive(ttFloat64, false);
	asCDataType BoolType = asCDataType::CreatePrimitive(ttBool, false);

	TestEqual(TEXT("int should occupy one dword in memory"), IntType.GetSizeInMemoryDWords(), 1);
	TestEqual(TEXT("float64 should occupy eight bytes in memory"), Float64Type.GetSizeInMemoryBytes(), 8);
	TestEqual(TEXT("bool alignment should stay byte-sized"), BoolType.GetAlignment(), 1);
	return true;
}

#endif
