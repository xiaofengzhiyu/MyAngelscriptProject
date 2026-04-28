#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/EngineTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEnumMetadataCompatBindingsTest,
	"Angelscript.TestModule.Bindings.EnumMetadataCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEnumMetadataCompatBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	UEnum* Enum = StaticEnum<EAttachmentRule>();
	if (!TestNotNull(TEXT("Enum metadata compat baseline should resolve the native UEnum"), Enum))
	{
		return false;
	}

	const int64 WorldValue = static_cast<int64>(EAttachmentRule::KeepWorld);
	const int32 WorldIndex = Enum->GetIndexByValue(WorldValue);
	if (!TestTrue(TEXT("Enum metadata compat baseline should resolve the KeepWorld enum index"), WorldIndex != INDEX_NONE))
	{
		return false;
	}

	const int32 NumEnums = Enum->NumEnums();
	const int64 MaxEnumValue = Enum->GetMaxEnumValue();
	const bool bContainsExistingMax = Enum->ContainsExistingMax();
	const FString WorldDisplayByIndex = Enum->GetDisplayNameTextByIndex(WorldIndex).ToString();
	const FString WorldNameString = Enum->GetNameStringByIndex(WorldIndex);

	FString Script = TEXT(R"(
int Entry()
{
	UObject EnumObject = FindObject("$ENUM_PATH$");
	UEnum RuntimeEnum = Cast<UEnum>(EnumObject);
	if (!IsValid(RuntimeEnum))
		return 10;
	if (RuntimeEnum.NumEnums() != $NUM_ENUMS$)
		return 20;
	if (RuntimeEnum.GetMaxEnumValue() != $MAX_ENUM_VALUE$)
		return 30;
	if (RuntimeEnum.ContainsExistingMax() != $CONTAINS_EXISTING_MAX$)
		return 40;
	if (!(RuntimeEnum.GetDisplayNameTextByIndex($WORLD_INDEX$).ToString() == "$WORLD_DISPLAY_BY_INDEX$"))
		return 50;
	if (!(RuntimeEnum.GetNameStringByIndex($WORLD_INDEX$) == "$WORLD_NAME_STRING$"))
		return 60;
	return 1;
}
)");

	Script.ReplaceInline(TEXT("$ENUM_PATH$"), *Enum->GetPathName().ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$NUM_ENUMS$"), *LexToString(NumEnums));
	Script.ReplaceInline(TEXT("$MAX_ENUM_VALUE$"), *LexToString(MaxEnumValue));
	Script.ReplaceInline(TEXT("$CONTAINS_EXISTING_MAX$"), bContainsExistingMax ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(TEXT("$WORLD_INDEX$"), *LexToString(WorldIndex));
	Script.ReplaceInline(TEXT("$WORLD_DISPLAY_BY_INDEX$"), *WorldDisplayByIndex.ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$WORLD_NAME_STRING$"), *WorldNameString.ReplaceCharWithEscapedChar());

	asIScriptModule* Module = BuildModule(*this, Engine, "ASEnumMetadataCompat", Script);
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

	TestEqual(TEXT("Bound UEnum metadata APIs should match native parity"), Result, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
