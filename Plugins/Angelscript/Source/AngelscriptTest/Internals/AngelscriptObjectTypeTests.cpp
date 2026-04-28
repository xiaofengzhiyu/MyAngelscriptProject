#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_objecttype.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptObjectTypeTests_Private
{
	struct FPropertyMetadata
	{
		FString Name;
		FString Declaration;
		int32 TypeId = asINVALID_TYPE;
		bool bIsPrivate = false;
		bool bIsProtected = false;
		int32 Offset = INDEX_NONE;
		bool bIsReference = false;
		bool bIsInherited = false;
	};

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

	void CollectPropertyMetadata(asITypeInfo& TypeInfo, TArray<FPropertyMetadata>& OutProperties)
	{
		OutProperties.Reset();

		for (asUINT PropertyIndex = 0; PropertyIndex < TypeInfo.GetPropertyCount(); ++PropertyIndex)
		{
			const char* RawName = nullptr;
			int TypeId = asINVALID_TYPE;
			bool bIsPrivate = false;
			bool bIsProtected = false;
			int Offset = INDEX_NONE;
			bool bIsReference = false;
			TypeInfo.GetProperty(PropertyIndex, &RawName, &TypeId, &bIsPrivate, &bIsProtected, &Offset, &bIsReference);

			FPropertyMetadata Metadata;
			Metadata.Name = ToFString(RawName);
			Metadata.Declaration = ToFString(TypeInfo.GetPropertyDeclaration(PropertyIndex, false));
			Metadata.TypeId = TypeId;
			Metadata.bIsPrivate = bIsPrivate;
			Metadata.bIsProtected = bIsProtected;
			Metadata.Offset = Offset;
			Metadata.bIsReference = bIsReference;
			Metadata.bIsInherited = TypeInfo.IsPropertyInherited(PropertyIndex);
			OutProperties.Add(Metadata);
		}
	}

	const FPropertyMetadata* FindPropertyMetadata(const TArray<FPropertyMetadata>& Properties, const TCHAR* PropertyName)
	{
		return Properties.FindByPredicate(
			[PropertyName](const FPropertyMetadata& Metadata)
			{
				return Metadata.Name == PropertyName;
			});
	}

	void CollectMethodDeclarations(asITypeInfo& TypeInfo, TArray<FString>& OutDeclarations)
	{
		OutDeclarations.Reset();

		for (asUINT MethodIndex = 0; MethodIndex < TypeInfo.GetMethodCount(); ++MethodIndex)
		{
			asIScriptFunction* Method = TypeInfo.GetMethodByIndex(MethodIndex);
			if (Method != nullptr)
			{
				OutDeclarations.Add(ToFString(Method->GetDeclaration()));
			}
		}
	}

	void CollectFactoryDeclarations(asITypeInfo& TypeInfo, TArray<FString>& OutDeclarations)
	{
		OutDeclarations.Reset();

		for (asUINT FactoryIndex = 0; FactoryIndex < TypeInfo.GetFactoryCount(); ++FactoryIndex)
		{
			asIScriptFunction* Factory = TypeInfo.GetFactoryByIndex(FactoryIndex);
			if (Factory != nullptr)
			{
				OutDeclarations.Add(ToFString(Factory->GetDeclaration()));
			}
		}
	}

	void CollectBehaviourMetadata(
		asITypeInfo& TypeInfo,
		TArray<asEBehaviours>& OutBehaviours,
		TArray<FString>& OutDeclarations)
	{
		OutBehaviours.Reset();
		OutDeclarations.Reset();

		for (asUINT BehaviourIndex = 0; BehaviourIndex < TypeInfo.GetBehaviourCount(); ++BehaviourIndex)
		{
			asEBehaviours Behaviour = asBEHAVE_CONSTRUCT;
			asIScriptFunction* BehaviourFunction = TypeInfo.GetBehaviourByIndex(BehaviourIndex, &Behaviour);
			if (BehaviourFunction != nullptr)
			{
				OutBehaviours.Add(Behaviour);
				OutDeclarations.Add(ToFString(BehaviourFunction->GetDeclaration()));
			}
		}
	}

	bool HasBehaviour(const TArray<asEBehaviours>& Behaviours, const asEBehaviours Expected)
	{
		return Behaviours.Contains(Expected);
	}
}

using namespace AngelscriptTest_Internals_AngelscriptObjectTypeTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectTypeInheritanceAndPropertiesTest,
	"Angelscript.TestModule.Internals.ObjectType.InheritanceAndProperties",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectTypeMethodsFactoriesAndBehavioursTest,
	"Angelscript.TestModule.Internals.ObjectType.MethodsFactoriesAndBehaviours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptObjectTypeInheritanceAndPropertiesTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASObjectTypeInheritanceProperties",
		TEXT(R"AS(
class BaseType
{
	int BaseValue = 4;

	int BaseOnly() const
	{
		return BaseValue;
	}

	int SharedValue() const
	{
		return BaseValue + 1;
	}
}

class DerivedType : BaseType
{
	int DerivedValue = 9;

	int DerivedOnly() const
	{
		return BaseValue + DerivedValue;
	}

	int SharedValue() const
	{
		return BaseValue + DerivedValue + 1;
	}
}

int Entry()
{
	return 0;
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asCObjectType* BaseType = static_cast<asCObjectType*>(Module->GetTypeInfoByDecl("BaseType"));
	asCObjectType* DerivedType = static_cast<asCObjectType*>(Module->GetTypeInfoByDecl("DerivedType"));
	if (!TestNotNull(TEXT("ObjectType.InheritanceAndProperties should resolve BaseType"), BaseType)
		|| !TestNotNull(TEXT("ObjectType.InheritanceAndProperties should resolve DerivedType"), DerivedType))
	{
		return false;
	}

	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep BaseType marked as a reference type"),
		(BaseType->GetFlags() & asOBJ_REF) != 0);
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep DerivedType marked as a reference type"),
		(DerivedType->GetFlags() & asOBJ_REF) != 0);
	bPassed &= TestNull(
		TEXT("ObjectType.InheritanceAndProperties should keep the base type without a parent type"),
		BaseType->GetBaseType());
	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should expose BaseType as the derived parent"),
		DerivedType->GetBaseType(),
		static_cast<asITypeInfo*>(BaseType));
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should report that DerivedType derives from BaseType"),
		DerivedType->DerivesFrom(BaseType));
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should not report that BaseType derives from DerivedType"),
		BaseType->DerivesFrom(DerivedType));
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep the derived type size at least as large as the base type"),
		DerivedType->GetSize() >= BaseType->GetSize());
	bPassed &= TestNotEqual(
		TEXT("ObjectType.InheritanceAndProperties should preserve distinct type ids for the base and derived classes"),
		DerivedType->GetTypeId(),
		BaseType->GetTypeId());

	TArray<FPropertyMetadata> BaseProperties;
	TArray<FPropertyMetadata> DerivedProperties;
	CollectPropertyMetadata(*BaseType, BaseProperties);
	CollectPropertyMetadata(*DerivedType, DerivedProperties);

	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should keep exactly one declared property on BaseType"),
		BaseProperties.Num(),
		1);
	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should keep the inherited and local properties on DerivedType"),
		DerivedProperties.Num(),
		2);

	const FPropertyMetadata* BaseBaseValue = FindPropertyMetadata(BaseProperties, TEXT("BaseValue"));
	const FPropertyMetadata* DerivedBaseValue = FindPropertyMetadata(DerivedProperties, TEXT("BaseValue"));
	const FPropertyMetadata* DerivedDerivedValue = FindPropertyMetadata(DerivedProperties, TEXT("DerivedValue"));
	if (!TestNotNull(TEXT("ObjectType.InheritanceAndProperties should expose BaseValue on BaseType"), BaseBaseValue)
		|| !TestNotNull(TEXT("ObjectType.InheritanceAndProperties should expose inherited BaseValue on DerivedType"), DerivedBaseValue)
		|| !TestNotNull(TEXT("ObjectType.InheritanceAndProperties should expose DerivedValue on DerivedType"), DerivedDerivedValue))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should keep BaseValue typed as int on BaseType"),
		BaseBaseValue->TypeId,
		static_cast<int32>(asTYPEID_INT32));
	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should keep inherited BaseValue typed as int on DerivedType"),
		DerivedBaseValue->TypeId,
		static_cast<int32>(asTYPEID_INT32));
	bPassed &= TestEqual(
		TEXT("ObjectType.InheritanceAndProperties should keep DerivedValue typed as int on DerivedType"),
		DerivedDerivedValue->TypeId,
		static_cast<int32>(asTYPEID_INT32));
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should not mark BaseType::BaseValue as inherited"),
		BaseBaseValue->bIsInherited);
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should mark BaseValue as inherited on DerivedType"),
		DerivedBaseValue->bIsInherited);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep DerivedValue as a local property on DerivedType"),
		DerivedDerivedValue->bIsInherited);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep BaseValue as a non-reference property on BaseType"),
		BaseBaseValue->bIsReference);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep BaseValue as a non-reference property on DerivedType"),
		DerivedBaseValue->bIsReference);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep DerivedValue as a non-reference property on DerivedType"),
		DerivedDerivedValue->bIsReference);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep BaseValue public on BaseType"),
		BaseBaseValue->bIsPrivate || BaseBaseValue->bIsProtected);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep inherited BaseValue public on DerivedType"),
		DerivedBaseValue->bIsPrivate || DerivedBaseValue->bIsProtected);
	bPassed &= TestFalse(
		TEXT("ObjectType.InheritanceAndProperties should keep DerivedValue public on DerivedType"),
		DerivedDerivedValue->bIsPrivate || DerivedDerivedValue->bIsProtected);
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep the BaseValue declaration text on BaseType"),
		BaseBaseValue->Declaration.Contains(TEXT("BaseValue")));
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep the inherited BaseValue declaration text on DerivedType"),
		DerivedBaseValue->Declaration.Contains(TEXT("BaseValue")));
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep the DerivedValue declaration text on DerivedType"),
		DerivedDerivedValue->Declaration.Contains(TEXT("DerivedValue")));
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should keep non-negative offsets for all exposed properties"),
		BaseBaseValue->Offset >= 0 && DerivedBaseValue->Offset >= 0 && DerivedDerivedValue->Offset >= 0);
	bPassed &= TestTrue(
		TEXT("ObjectType.InheritanceAndProperties should place the derived property after the inherited base property"),
		DerivedDerivedValue->Offset > DerivedBaseValue->Offset);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptObjectTypeMethodsFactoriesAndBehavioursTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASObjectTypeMethodsFactoriesBehaviours",
		TEXT(R"AS(
class BaseType
{
	int BaseValue = 3;

	int BaseOnly() const
	{
		return BaseValue;
	}

	int SharedValue() const
	{
		return BaseValue + 1;
	}
}

class DerivedType : BaseType
{
	int DerivedValue = 6;

	int DerivedOnly() const
	{
		return BaseValue + DerivedValue;
	}

	int SharedValue() const
	{
		return BaseValue + DerivedValue + 1;
	}
}
)AS"));
	if (Module == nullptr)
	{
		return false;
	}

	asCObjectType* BaseType = static_cast<asCObjectType*>(Module->GetTypeInfoByDecl("BaseType"));
	asCObjectType* DerivedType = static_cast<asCObjectType*>(Module->GetTypeInfoByDecl("DerivedType"));
	if (!TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve BaseType"), BaseType)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve DerivedType"), DerivedType))
	{
		return false;
	}

	asIScriptFunction* BaseOnlyByName = BaseType->GetMethodByName("BaseOnly");
	asIScriptFunction* BaseOnlyByDecl = BaseType->GetMethodByDecl("int BaseOnly() const");
	asIScriptFunction* DerivedOnlyByName = DerivedType->GetMethodByName("DerivedOnly");
	asIScriptFunction* DerivedOnlyByDecl = DerivedType->GetMethodByDecl("int DerivedOnly() const");
	asIScriptFunction* SharedValueByName = DerivedType->GetMethodByName("SharedValue");
	asIScriptFunction* SharedValueByDecl = DerivedType->GetMethodByDecl("int SharedValue() const");
	if (!TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve BaseType::BaseOnly by name"), BaseOnlyByName)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve BaseType::BaseOnly by declaration"), BaseOnlyByDecl)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve DerivedType::DerivedOnly by name"), DerivedOnlyByName)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve DerivedType::DerivedOnly by declaration"), DerivedOnlyByDecl)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve DerivedType::SharedValue by name"), SharedValueByName)
		|| !TestNotNull(TEXT("ObjectType.MethodsFactoriesAndBehaviours should resolve DerivedType::SharedValue by declaration"), SharedValueByDecl))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep BaseOnly lookup stable across name and declaration lookups"),
		BaseOnlyByDecl,
		BaseOnlyByName);
	bPassed &= TestEqual(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep DerivedOnly lookup stable across name and declaration lookups"),
		DerivedOnlyByDecl,
		DerivedOnlyByName);
	bPassed &= TestEqual(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep SharedValue lookup stable across name and declaration lookups"),
		SharedValueByDecl,
		SharedValueByName);

	TArray<FString> BaseMethodDeclarations;
	TArray<FString> DerivedMethodDeclarations;
	CollectMethodDeclarations(*BaseType, BaseMethodDeclarations);
	CollectMethodDeclarations(*DerivedType, DerivedMethodDeclarations);

	bPassed &= TestEqual(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep two declared methods on BaseType"),
		BaseMethodDeclarations.Num(),
		2);
	bPassed &= TestEqual(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep inherited methods visible on DerivedType"),
		DerivedMethodDeclarations.Num(),
		3);
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate BaseOnly on BaseType"),
		ContainsFragment(BaseMethodDeclarations, TEXT("BaseOnly")));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate SharedValue on BaseType"),
		ContainsFragment(BaseMethodDeclarations, TEXT("SharedValue")));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate inherited BaseOnly on DerivedType"),
		ContainsFragment(DerivedMethodDeclarations, TEXT("BaseOnly")));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate DerivedOnly on DerivedType"),
		ContainsFragment(DerivedMethodDeclarations, TEXT("DerivedOnly")));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate SharedValue on DerivedType"),
		ContainsFragment(DerivedMethodDeclarations, TEXT("SharedValue")));

	TArray<FString> BaseFactoryDeclarations;
	TArray<FString> DerivedFactoryDeclarations;
	CollectFactoryDeclarations(*BaseType, BaseFactoryDeclarations);
	CollectFactoryDeclarations(*DerivedType, DerivedFactoryDeclarations);

	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep at least one factory on BaseType"),
		BaseType->GetFactoryCount() >= 1);
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep at least one factory on DerivedType"),
		DerivedType->GetFactoryCount() >= 1);
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate a BaseType factory declaration"),
		ContainsFragment(BaseFactoryDeclarations, TEXT("BaseType")));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate a DerivedType factory declaration"),
		ContainsFragment(DerivedFactoryDeclarations, TEXT("DerivedType")));
	if (!DerivedFactoryDeclarations.IsEmpty())
	{
		const FTCHARToUTF8 FactoryDeclUtf8(*DerivedFactoryDeclarations[0]);
		bPassed &= TestNotNull(
			TEXT("ObjectType.MethodsFactoriesAndBehaviours should round-trip factory lookup through GetFactoryByDecl"),
			DerivedType->GetFactoryByDecl(FactoryDeclUtf8.Get()));
	}

	TArray<asEBehaviours> BaseBehaviours;
	TArray<asEBehaviours> DerivedBehaviours;
	TArray<FString> BaseBehaviourDeclarations;
	TArray<FString> DerivedBehaviourDeclarations;
	CollectBehaviourMetadata(*BaseType, BaseBehaviours, BaseBehaviourDeclarations);
	CollectBehaviourMetadata(*DerivedType, DerivedBehaviours, DerivedBehaviourDeclarations);

	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should expose constructor behaviours on BaseType"),
		HasBehaviour(BaseBehaviours, asBEHAVE_CONSTRUCT));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should expose constructor behaviours on DerivedType"),
		HasBehaviour(DerivedBehaviours, asBEHAVE_CONSTRUCT));
	bPassed &= TestFalse(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep BaseType free of implicit reference-count behaviours on this fork"),
		HasBehaviour(BaseBehaviours, asBEHAVE_ADDREF) || HasBehaviour(BaseBehaviours, asBEHAVE_RELEASE));
	bPassed &= TestFalse(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should keep DerivedType free of implicit reference-count behaviours on this fork"),
		HasBehaviour(DerivedBehaviours, asBEHAVE_ADDREF) || HasBehaviour(DerivedBehaviours, asBEHAVE_RELEASE));
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate non-empty behaviour declarations for BaseType"),
		!BaseBehaviourDeclarations.IsEmpty());
	bPassed &= TestTrue(
		TEXT("ObjectType.MethodsFactoriesAndBehaviours should enumerate non-empty behaviour declarations for DerivedType"),
		!DerivedBehaviourDeclarations.IsEmpty());

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
