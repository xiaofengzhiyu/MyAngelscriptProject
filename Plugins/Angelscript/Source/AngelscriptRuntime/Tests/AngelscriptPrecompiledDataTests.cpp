#include "AngelscriptEngine.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "StaticJIT/PrecompiledData.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_module.h"
#include "source/as_objecttype.h"
#include "source/as_datatype.h"
#include "source/as_scriptengine.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptPrecompiledDataTests_Private
{
	static TUniquePtr<FAngelscriptEngine> CreateTestEngine(FAutomationTestBase& Test)
	{
		const FAngelscriptEngineConfig Config;
		const FAngelscriptEngineDependencies Dependencies = FAngelscriptEngineDependencies::CreateDefault();
		TUniquePtr<FAngelscriptEngine> Engine = FAngelscriptEngine::CreateForTesting(Config, Dependencies, EAngelscriptEngineCreationMode::Clone);
		Test.TestNotNull(TEXT("A test script engine wrapper should be created for precompiled data tests"), Engine.Get());
		return Engine;
	}

	static bool SerializeAndRecreateDataType(
		FAutomationTestBase& Test,
		FAngelscriptPrecompiledData& Context,
		const asCDataType& SourceType,
		FAngelscriptPrecompiledDataType& OutStoredType,
		asCDataType& OutRecreatedType)
	{
		asCDataType MutableSourceType = SourceType;
		FAngelscriptPrecompiledDataType StoredType;
		StoredType.InitFrom(Context, MutableSourceType);

		TArray<uint8> Bytes;
		{
			FMemoryWriter Writer(Bytes);
			Writer << StoredType;
		}

		if (!Test.TestTrue(TEXT("Precompiled data-type serialization should write bytes"), Bytes.Num() > 0))
		{
			return false;
		}

		FMemoryReader Reader(Bytes);
		Reader << OutStoredType;
		OutStoredType.Create(Context, OutRecreatedType);
		return true;
	}
}

using namespace AngelscriptPrecompiledDataTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledEditorOnlyFlagRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.EditorOnlyFlagRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptModuleDiffHighBitFlagTest,
	"Angelscript.CppTests.StaticJIT.ModuleDiff.HighBitFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledDataTypePrimitiveAndReferenceRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.DataType.PrimitiveAndReferenceRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPrecompiledDataTypeObjectHandleAndAutoRoundtripTest,
	"Angelscript.CppTests.StaticJIT.PrecompiledData.DataType.ObjectHandleAndAutoRoundtrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptPrecompiledEditorOnlyFlagRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (!TestNotNull(TEXT("A test script engine wrapper should be created for the precompiled flag roundtrip"), OwnedEngine.Get()))
	{
		return false;
	}

	FAngelscriptEngine& Engine = *OwnedEngine;
	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	if (!TestNotNull(TEXT("A test script engine should exist for the precompiled flag roundtrip"), ScriptEngine))
	{
		return false;
	}

	asCModule SourceModule("PrecompiledFlagSource", ScriptEngine);
	asCObjectType SourceType(ScriptEngine);
	SourceType.name = "UPrecompiledFlagSourceType";
	SourceType.nameSpace = ScriptEngine->defaultNamespace;
	SourceType.module = &SourceModule;
	SourceType.flags = asOBJ_REF | asOBJ_EDITOR_ONLY;
	const asQWORD ExpectedFlags = SourceType.flags;

	FAngelscriptPrecompiledData Context(ScriptEngine);
	FAngelscriptPrecompiledClass PrecompiledClass;
	PrecompiledClass.InitFrom(Context, &SourceModule, &SourceType);
	if (!TestEqual(TEXT("Precompiled class storage should preserve high-bit editor-only flags"), PrecompiledClass.Flags, ExpectedFlags))
	{
		return false;
	}

	asCModule RecreatedModule("PrecompiledFlagRecreated", ScriptEngine);
	asCObjectType* RecreatedType = PrecompiledClass.Create(Context, &RecreatedModule);
	if (!TestNotNull(TEXT("Precompiled class recreation should produce an object type"), RecreatedType))
	{
		return false;
	}

	TestEqual(TEXT("Precompiled class recreation should preserve high-bit editor-only flags"), RecreatedType->flags, ExpectedFlags);
	return true;
}

bool FAngelscriptModuleDiffHighBitFlagTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (!TestNotNull(TEXT("A test script engine wrapper should be created for the module-diff regression"), OwnedEngine.Get()))
	{
		return false;
	}

	FAngelscriptEngine& Engine = *OwnedEngine;
	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(Engine.GetScriptEngine());
	if (!TestNotNull(TEXT("A script engine should exist for the module-diff regression"), ScriptEngine))
	{
		return false;
	}

	auto MakeModuleClass = [ScriptEngine](const char* ModuleName, const char* TypeName, asQWORD Flags)
	{
		asCModule* Module = asNEW(asCModule)(ModuleName, ScriptEngine);
		asCObjectType* Type = asNEW(asCObjectType)(ScriptEngine);
		Type->name = TypeName;
		Type->nameSpace = ScriptEngine->defaultNamespace;
		Type->module = Module;
		Type->flags = Flags;
		Module->classTypes.PushLast(Type);
		Module->allLocalTypes.Add(Type);
		return TPair<asCModule*, asCObjectType*>(Module, Type);
	};

	auto [OldModule, OldType] = MakeModuleClass("DiffOld", "UDiffType", asOBJ_REF | asOBJ_EDITOR_ONLY);
	auto [SameModule, SameType] = MakeModuleClass("DiffSame", "UDiffType", asOBJ_REF | asOBJ_EDITOR_ONLY);
	auto [ChangedModule, ChangedType] = MakeModuleClass("DiffChanged", "UDiffType", asOBJ_REF);

	ON_SCOPE_EXIT
	{
		asDELETE(OldModule, asCModule);
		asDELETE(SameModule, asCModule);
		asDELETE(ChangedModule, asCModule);
	};

	asModuleReferenceUpdateMap UpdateMap;
	UpdateMap.Types.Add(OldType, SameType);
	bool bHadStructuralChanges = false;
	SameModule->DiffForReferenceUpdate(OldModule, UpdateMap, bHadStructuralChanges);
	if (!TestFalse(TEXT("Module diff should not report a structural change when high-bit flags are identical"), bHadStructuralChanges))
	{
		return false;
	}

	asModuleReferenceUpdateMap ChangedUpdateMap;
	ChangedUpdateMap.Types.Add(OldType, ChangedType);
	bHadStructuralChanges = false;
	ChangedModule->DiffForReferenceUpdate(OldModule, ChangedUpdateMap, bHadStructuralChanges);
	TestTrue(TEXT("Module diff should report a structural change when a high-bit private flag differs"), bHadStructuralChanges);
	return true;
}

bool FAngelscriptPrecompiledDataTypePrimitiveAndReferenceRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("A script engine should exist for the precompiled data-type roundtrip regression"), ScriptEngine))
	{
		return false;
	}

	asCTypeInfo* ActorType = static_cast<asCTypeInfo*>(ScriptEngine->GetTypeInfoByName("AActor"));
	if (!TestNotNull(TEXT("AActor should exist in the script type system for data-type roundtrip tests"), ActorType))
	{
		return false;
	}

	FAngelscriptPrecompiledData Context(ScriptEngine);

	asCDataType PrimitiveType = asCDataType::CreatePrimitive(ttInt64, true);
	FAngelscriptPrecompiledDataType StoredPrimitiveType;
	asCDataType RecreatedPrimitiveType;
	if (!SerializeAndRecreateDataType(*this, Context, PrimitiveType, StoredPrimitiveType, RecreatedPrimitiveType))
	{
		return false;
	}

	if (!TestEqual(TEXT("Primitive roundtrip should keep the primitive token"), StoredPrimitiveType.TokenType, static_cast<int32>(ttInt64))
		|| !TestTrue(TEXT("Primitive roundtrip should not record an object type reference"), StoredPrimitiveType.TypeInfo.IsNull())
		|| !TestFalse(TEXT("Primitive roundtrip should not mark the serialized type as a handle"), StoredPrimitiveType.bIsObjectHandle)
		|| !TestFalse(TEXT("Primitive roundtrip should not mark the serialized type as a reference"), StoredPrimitiveType.bIsReference)
		|| !TestFalse(TEXT("Primitive roundtrip should not mark the serialized type as auto"), StoredPrimitiveType.bIsAuto)
		|| !TestTrue(TEXT("Primitive roundtrip should preserve const semantics in serialized storage"), StoredPrimitiveType.bIsObjectConst))
	{
		return false;
	}

	if (!TestTrue(TEXT("Primitive roundtrip should recreate an exact primitive data type"), RecreatedPrimitiveType == PrimitiveType)
		|| !TestTrue(TEXT("Primitive roundtrip should recreate primitive semantics"), RecreatedPrimitiveType.IsPrimitive())
		|| !TestTrue(TEXT("Primitive roundtrip should recreate const semantics"), RecreatedPrimitiveType.IsReadOnly())
		|| !TestFalse(TEXT("Primitive roundtrip should recreate a non-reference type"), RecreatedPrimitiveType.IsReference()))
	{
		return false;
	}

	asCDataType ObjectReferenceType = asCDataType::CreateType(ActorType, true);
	ObjectReferenceType.MakeReference(true);

	FAngelscriptPrecompiledDataType StoredReferenceType;
	asCDataType RecreatedReferenceType;
	if (!SerializeAndRecreateDataType(*this, Context, ObjectReferenceType, StoredReferenceType, RecreatedReferenceType))
	{
		return false;
	}

	if (!TestEqual(TEXT("Reference roundtrip should keep the identifier token"), StoredReferenceType.TokenType, static_cast<int32>(ttIdentifier))
		|| !TestFalse(TEXT("Reference roundtrip should record a type reference"), StoredReferenceType.TypeInfo.IsNull())
		|| !TestTrue(TEXT("Reference roundtrip should preserve reference semantics in serialized storage"), StoredReferenceType.bIsReference)
		|| !TestFalse(TEXT("Reference roundtrip should not mark the serialized type as a handle"), StoredReferenceType.bIsObjectHandle)
		|| !TestFalse(TEXT("Reference roundtrip should not mark the serialized type as auto"), StoredReferenceType.bIsAuto)
		|| !TestTrue(TEXT("Reference roundtrip should preserve const object semantics in serialized storage"), StoredReferenceType.bIsObjectConst))
	{
		return false;
	}

	if (!TestTrue(TEXT("Reference roundtrip should recreate an exact object-reference data type"), RecreatedReferenceType == ObjectReferenceType)
		|| !TestTrue(TEXT("Reference roundtrip should preserve the engine type info"), RecreatedReferenceType.GetTypeInfo() == ActorType)
		|| !TestTrue(TEXT("Reference roundtrip should preserve reference semantics"), RecreatedReferenceType.IsReference())
		|| !TestTrue(TEXT("Reference roundtrip should preserve object semantics"), RecreatedReferenceType.IsObject()))
	{
		return false;
	}

	return true;
}

bool FAngelscriptPrecompiledDataTypeObjectHandleAndAutoRoundtripTest::RunTest(const FString& Parameters)
{
	TUniquePtr<FAngelscriptEngine> OwnedEngine = CreateTestEngine(*this);
	if (OwnedEngine == nullptr)
	{
		return false;
	}

	asCScriptEngine* ScriptEngine = static_cast<asCScriptEngine*>(OwnedEngine->GetScriptEngine());
	if (!TestNotNull(TEXT("A script engine should exist for the precompiled data-type handle/auto regression"), ScriptEngine))
	{
		return false;
	}

	asCTypeInfo* ActorType = static_cast<asCTypeInfo*>(ScriptEngine->GetTypeInfoByName("AActor"));
	if (!TestNotNull(TEXT("AActor should exist in the script type system for data-type handle/auto tests"), ActorType))
	{
		return false;
	}

	FAngelscriptPrecompiledData Context(ScriptEngine);

	asCDataType ObjectHandleType = asCDataType::CreateObjectHandle(ActorType, true);
	ObjectHandleType.MakeHandleToConst(true);
	ObjectHandleType.SetIfHandleThenConst(true);

	FAngelscriptPrecompiledDataType StoredObjectHandleType;
	asCDataType RecreatedObjectHandleType;
	if (!SerializeAndRecreateDataType(*this, Context, ObjectHandleType, StoredObjectHandleType, RecreatedObjectHandleType))
	{
		return false;
	}

	if (!TestEqual(TEXT("Object-handle roundtrip should keep the identifier token"), StoredObjectHandleType.TokenType, static_cast<int32>(ttIdentifier))
		|| !TestFalse(TEXT("Object-handle roundtrip should record a type reference"), StoredObjectHandleType.TypeInfo.IsNull())
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve handle semantics in serialized storage"), StoredObjectHandleType.bIsObjectHandle)
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve const-handle semantics in serialized storage"), StoredObjectHandleType.bIsConstHandle)
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve handle-to-const semantics in serialized storage"), StoredObjectHandleType.bIsObjectConst)
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve if-handle-then-const semantics in serialized storage"), StoredObjectHandleType.bIfHandleThenConst)
		|| !TestFalse(TEXT("Object-handle roundtrip should not mark the serialized type as auto"), StoredObjectHandleType.bIsAuto))
	{
		return false;
	}

	if (!TestTrue(TEXT("Object-handle roundtrip should recreate the same handle data type"), RecreatedObjectHandleType == ObjectHandleType)
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve the engine type info"), RecreatedObjectHandleType.GetTypeInfo() == ActorType)
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve handle semantics"), RecreatedObjectHandleType.IsObjectHandle())
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve handle-to-const semantics"), RecreatedObjectHandleType.IsHandleToConst())
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve const-handle semantics"), RecreatedObjectHandleType.IsReadOnly())
		|| !TestTrue(TEXT("Object-handle roundtrip should preserve if-handle-then-const semantics"), RecreatedObjectHandleType.HasIfHandleThenConst()))
	{
		return false;
	}

	asCDataType AutoHandleReferenceType = asCDataType::CreateAuto(true);
	AutoHandleReferenceType.MakeHandle(true);
	AutoHandleReferenceType.MakeReadOnly(true);
	AutoHandleReferenceType.MakeReference(true);

	FAngelscriptPrecompiledDataType StoredAutoType;
	asCDataType RecreatedAutoType;
	if (!SerializeAndRecreateDataType(*this, Context, AutoHandleReferenceType, StoredAutoType, RecreatedAutoType))
	{
		return false;
	}

	if (!TestEqual(TEXT("Auto roundtrip should keep the identifier token"), StoredAutoType.TokenType, static_cast<int32>(ttIdentifier))
		|| !TestTrue(TEXT("Auto roundtrip should not record a type reference"), StoredAutoType.TypeInfo.IsNull())
		|| !TestTrue(TEXT("Auto roundtrip should preserve auto semantics in serialized storage"), StoredAutoType.bIsAuto)
		|| !TestTrue(TEXT("Auto roundtrip should preserve handle semantics in serialized storage"), StoredAutoType.bIsObjectHandle)
		|| !TestTrue(TEXT("Auto roundtrip should preserve const-handle semantics in serialized storage"), StoredAutoType.bIsConstHandle)
		|| !TestTrue(TEXT("Auto roundtrip should preserve handle-to-const semantics in serialized storage"), StoredAutoType.bIsObjectConst)
		|| !TestTrue(TEXT("Auto roundtrip should preserve reference semantics in serialized storage"), StoredAutoType.bIsReference))
	{
		return false;
	}

	if (!TestTrue(TEXT("Auto roundtrip should preserve auto semantics"), RecreatedAutoType.IsAuto())
		|| !TestTrue(TEXT("Auto roundtrip should preserve handle semantics"), RecreatedAutoType.IsObjectHandle())
		|| !TestTrue(TEXT("Auto roundtrip should preserve const-handle semantics"), RecreatedAutoType.IsReadOnly())
		|| !TestTrue(TEXT("Auto roundtrip should preserve handle-to-const semantics"), RecreatedAutoType.IsHandleToConst())
		|| !TestTrue(TEXT("Auto roundtrip should preserve reference semantics"), RecreatedAutoType.IsReference()))
	{
		return false;
	}

	return true;
}

#endif
