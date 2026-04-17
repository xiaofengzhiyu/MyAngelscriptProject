#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptCollisionProfileChannelConversionsBindingsTest,
	"Angelscript.TestModule.Bindings.CollisionProfileChannelConversions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptCollisionProfileChannelConversionsBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	UCollisionProfile* CollisionProfile = UCollisionProfile::Get();
	if (!TestNotNull(TEXT("Collision profile binding test should access the native collision profile singleton"), CollisionProfile))
	{
		return false;
	}

	const EObjectTypeQuery WorldStaticObjectType = CollisionProfile->ConvertToObjectType(ECC_WorldStatic);
	const EObjectTypeQuery WorldDynamicObjectType = CollisionProfile->ConvertToObjectType(ECC_WorldDynamic);
	const ETraceTypeQuery VisibilityTraceType = CollisionProfile->ConvertToTraceType(ECC_Visibility);
	const ETraceTypeQuery CameraTraceType = CollisionProfile->ConvertToTraceType(ECC_Camera);

	const ECollisionChannel WorldStaticRoundTrip = CollisionProfile->ConvertToCollisionChannel(false, static_cast<int32>(WorldStaticObjectType));
	const ECollisionChannel WorldDynamicRoundTrip = CollisionProfile->ConvertToCollisionChannel(false, static_cast<int32>(WorldDynamicObjectType));
	const ECollisionChannel VisibilityRoundTrip = CollisionProfile->ConvertToCollisionChannel(true, static_cast<int32>(VisibilityTraceType));
	const ECollisionChannel CameraRoundTrip = CollisionProfile->ConvertToCollisionChannel(true, static_cast<int32>(CameraTraceType));

	bPassed &= TestEqual(
		TEXT("Native world-static object query should round-trip back to ECC_WorldStatic"),
		static_cast<int32>(WorldStaticRoundTrip),
		static_cast<int32>(ECC_WorldStatic));
	bPassed &= TestEqual(
		TEXT("Native world-dynamic object query should round-trip back to ECC_WorldDynamic"),
		static_cast<int32>(WorldDynamicRoundTrip),
		static_cast<int32>(ECC_WorldDynamic));
	bPassed &= TestEqual(
		TEXT("Native visibility trace query should round-trip back to ECC_Visibility"),
		static_cast<int32>(VisibilityRoundTrip),
		static_cast<int32>(ECC_Visibility));
	bPassed &= TestEqual(
		TEXT("Native camera trace query should round-trip back to ECC_Camera"),
		static_cast<int32>(CameraRoundTrip),
		static_cast<int32>(ECC_Camera));
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
int Entry()
{
	if (int(UCollisionProfile::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic)) != $WORLD_STATIC_OBJECT_TYPE$)
		return 10;
	if (int(UCollisionProfile::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic)) != $WORLD_DYNAMIC_OBJECT_TYPE$)
		return 20;
	if (int(UCollisionProfile::ConvertToTraceType(ECollisionChannel::ECC_Visibility)) != $VISIBILITY_TRACE_TYPE$)
		return 30;
	if (int(UCollisionProfile::ConvertToTraceType(ECollisionChannel::ECC_Camera)) != $CAMERA_TRACE_TYPE$)
		return 40;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, $WORLD_STATIC_OBJECT_TYPE$)) != $WORLD_STATIC_CHANNEL$)
		return 50;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, $WORLD_DYNAMIC_OBJECT_TYPE$)) != $WORLD_DYNAMIC_CHANNEL$)
		return 60;
	if (int(UCollisionProfile::ConvertToCollisionChannel(true, $VISIBILITY_TRACE_TYPE$)) != $VISIBILITY_CHANNEL$)
		return 70;
	if (int(UCollisionProfile::ConvertToCollisionChannel(true, $CAMERA_TRACE_TYPE$)) != $CAMERA_CHANNEL$)
		return 80;
	if (int(UCollisionProfile::ConvertToCollisionChannel(false, int(UCollisionProfile::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic)))) != $WORLD_STATIC_CHANNEL$)
		return 90;
	if (int(UCollisionProfile::ConvertToCollisionChannel(true, int(UCollisionProfile::ConvertToTraceType(ECollisionChannel::ECC_Visibility)))) != $VISIBILITY_CHANNEL$)
		return 100;
	return 1;
}
)");

	Script.ReplaceInline(TEXT("$WORLD_STATIC_OBJECT_TYPE$"), *LexToString(static_cast<int32>(WorldStaticObjectType)));
	Script.ReplaceInline(TEXT("$WORLD_DYNAMIC_OBJECT_TYPE$"), *LexToString(static_cast<int32>(WorldDynamicObjectType)));
	Script.ReplaceInline(TEXT("$VISIBILITY_TRACE_TYPE$"), *LexToString(static_cast<int32>(VisibilityTraceType)));
	Script.ReplaceInline(TEXT("$CAMERA_TRACE_TYPE$"), *LexToString(static_cast<int32>(CameraTraceType)));
	Script.ReplaceInline(TEXT("$WORLD_STATIC_CHANNEL$"), *LexToString(static_cast<int32>(ECC_WorldStatic)));
	Script.ReplaceInline(TEXT("$WORLD_DYNAMIC_CHANNEL$"), *LexToString(static_cast<int32>(ECC_WorldDynamic)));
	Script.ReplaceInline(TEXT("$VISIBILITY_CHANNEL$"), *LexToString(static_cast<int32>(ECC_Visibility)));
	Script.ReplaceInline(TEXT("$CAMERA_CHANNEL$"), *LexToString(static_cast<int32>(ECC_Camera)));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASCollisionProfileChannelConversions", Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* EntryFunction = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (EntryFunction == nullptr)
	{
		return false;
	}

	int32 Result = 0;
	if (!ExecuteIntFunction(*this, Engine, *EntryFunction, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Collision profile channel conversion bindings should match the native object/trace round-trip baseline"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
