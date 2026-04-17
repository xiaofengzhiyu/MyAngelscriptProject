#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Engine/EngineTypes.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptEnumLookupCompatBindingsTest,
	"Angelscript.TestModule.Bindings.EnumLookupCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptEnumLookupCompatBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	UEnum* Enum = StaticEnum<EAttachmentRule>();
	if (!TestNotNull(TEXT("Enum lookup compat baseline should resolve the native UEnum"), Enum))
	{
		return false;
	}

	const int64 WorldValue = static_cast<int64>(EAttachmentRule::KeepWorld);
	const FName WorldName = Enum->GetNameByValue(WorldValue);
	const FString WorldNameString = Enum->GetNameStringByValue(WorldValue);
	const int32 WorldIndex = Enum->GetIndexByName(WorldName);
	const FString WorldDisplay = Enum->GetDisplayNameTextByValue(WorldValue).ToString();
	const FString EnumPrefix = Enum->GenerateEnumPrefix();
	const FName MissingName(TEXT("EAttachmentRule::DefinitelyMissing"));
	const FString MissingNameString(TEXT("DefinitelyMissing"));

	FString Script = TEXT(R"(
int Entry()
{
	UObject EnumObject = FindObject("$ENUM_PATH$");
	UEnum RuntimeEnum = Cast<UEnum>(EnumObject);
	if (!IsValid(RuntimeEnum))
		return 10;
	if (!(RuntimeEnum.GetNameByIndex($WORLD_INDEX$) == FName("$WORLD_NAME$")))
		return 20;
	if (!(RuntimeEnum.GetNameByValue($WORLD_VALUE$) == FName("$WORLD_NAME$")))
		return 30;
	if (RuntimeEnum.GetIndexByName(FName("$WORLD_NAME$")) != $WORLD_INDEX$)
		return 40;
	if (RuntimeEnum.GetValueByName(FName("$WORLD_NAME$")) != $WORLD_VALUE$)
		return 50;
	if (!(RuntimeEnum.GetNameStringByIndex($WORLD_INDEX$) == "$WORLD_NAME_STRING$"))
		return 60;
	if (!(RuntimeEnum.GetNameStringByValue($WORLD_VALUE$) == "$WORLD_NAME_STRING$"))
		return 70;
	if (RuntimeEnum.GetIndexByNameString("$WORLD_NAME_STRING$") != $WORLD_INDEX$)
		return 80;
	if (RuntimeEnum.GetValueByNameString("$WORLD_NAME_STRING$") != $WORLD_VALUE$)
		return 90;
	if (!(RuntimeEnum.GetDisplayNameTextByValue($WORLD_VALUE$).ToString() == "$WORLD_DISPLAY$"))
		return 100;
	if (!(RuntimeEnum.GenerateEnumPrefix() == "$ENUM_PREFIX$"))
		return 110;
	if (!RuntimeEnum.IsValidEnumValue($WORLD_VALUE$))
		return 120;
	if (RuntimeEnum.IsValidEnumValue(99))
		return 130;
	if (!RuntimeEnum.IsValidEnumName(FName("$WORLD_NAME$")))
		return 140;
	if (RuntimeEnum.IsValidEnumName(FName("$MISSING_NAME$")))
		return 150;
	if (RuntimeEnum.GetValueByName(FName("$MISSING_NAME$")) != $MISSING_VALUE$)
		return 160;
	if (RuntimeEnum.GetIndexByName(FName("$MISSING_NAME$")) != $MISSING_INDEX$)
		return 170;
	if (RuntimeEnum.GetIndexByNameString("$MISSING_NAME_STRING$") != $MISSING_INDEX_STRING$)
		return 180;
	return 1;
}
)");

	Script.ReplaceInline(TEXT("$ENUM_PATH$"), *Enum->GetPathName().ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$WORLD_NAME$"), *WorldName.ToString().ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$WORLD_NAME_STRING$"), *WorldNameString.ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$WORLD_DISPLAY$"), *WorldDisplay.ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$ENUM_PREFIX$"), *EnumPrefix.ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("$MISSING_NAME$"), *MissingName.ToString());
	Script.ReplaceInline(TEXT("$MISSING_NAME_STRING$"), *MissingNameString);
	Script.ReplaceInline(TEXT("$WORLD_INDEX$"), *LexToString(WorldIndex));
	Script.ReplaceInline(TEXT("$WORLD_VALUE$"), *LexToString(WorldValue));
	Script.ReplaceInline(TEXT("$MISSING_VALUE$"), *LexToString(Enum->GetValueByName(MissingName)));
	Script.ReplaceInline(TEXT("$MISSING_INDEX$"), *LexToString(Enum->GetIndexByName(MissingName)));
	Script.ReplaceInline(TEXT("$MISSING_INDEX_STRING$"), *LexToString(Enum->GetIndexByNameString(MissingNameString)));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASEnumLookupCompat", Script);
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

	TestEqual(TEXT("Bound UEnum lookup APIs should match native parity"), Result, 1);
	ASTEST_END_SHARE_CLEAN
	return true;
}

#endif
