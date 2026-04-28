#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Engine/Texture2D.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptObjectLifecycleBindingsTests_Private
{
	static constexpr ANSICHAR ObjectLifecycleModuleName[] = "ASObjectLifecycleCompat";

	struct FObjectLifecycleFixtureNames
	{
		FString OuterTextureName;
		FString InnerTextureName;
		FString StandaloneTextureName;
	};

	FObjectLifecycleFixtureNames CreateFixtureNames()
	{
		const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);

		FObjectLifecycleFixtureNames FixtureNames;
		FixtureNames.OuterTextureName = FString::Printf(TEXT("ASObjectLifecycleOuter_%s"), *Suffix);
		FixtureNames.InnerTextureName = FString::Printf(TEXT("ASObjectLifecycleInner_%s"), *Suffix);
		FixtureNames.StandaloneTextureName = FString::Printf(TEXT("ASObjectLifecycleStandalone_%s"), *Suffix);
		return FixtureNames;
	}

	FString BuildScriptSource(const FObjectLifecycleFixtureNames& FixtureNames)
	{
		FString Script = TEXT(R"AS(
int Entry()
{
	UObject TransientPackageObject = GetTransientPackage();

	UTexture2D OuterTexture = Cast<UTexture2D>(NewObject(GetTransientPackage(), UTexture2D::StaticClass(), FName("__OUTER_TEXTURE_NAME__"), true));
	if (!IsValid(OuterTexture))
		return 10;
	if (!OuterTexture.IsTransient())
		return 20;
	if (!(OuterTexture.GetOuter() == TransientPackageObject))
		return 30;
	if (!(OuterTexture.GetPackage() == GetTransientPackage()))
		return 40;

	UObject OuterTextureObject = OuterTexture;
	UTexture2D InnerTexture = Cast<UTexture2D>(NewObject(OuterTexture, UTexture2D::StaticClass(), FName("__INNER_TEXTURE_NAME__"), true));
	if (!IsValid(InnerTexture))
		return 50;
	if (!InnerTexture.IsTransient())
		return 60;
	if (!(InnerTexture.GetOuter() == OuterTextureObject))
		return 70;

	UObject TypedTextureOuter = InnerTexture.GetTypedOuter(UTexture2D::StaticClass());
	if (!IsValid(TypedTextureOuter))
		return 75;
	if (!(TypedTextureOuter == OuterTexture))
		return 80;

	UObject NullOuter = nullptr;
	UTexture2D StandaloneTexture = Cast<UTexture2D>(NewObject(NullOuter, UTexture2D::StaticClass(), FName("__STANDALONE_TEXTURE_NAME__"), false));
	if (!IsValid(StandaloneTexture))
		return 100;
	if (!StandaloneTexture.IsTransient())
		return 110;
	if (!(StandaloneTexture.GetOuter() == TransientPackageObject))
		return 120;
	if (!(StandaloneTexture.GetPackage() == GetTransientPackage()))
		return 130;
	if (StandaloneTexture.GetIsRooted())
		return 140;

	StandaloneTexture.AddToRoot();
	if (!StandaloneTexture.GetIsRooted())
		return 150;

	StandaloneTexture.RemoveFromRoot();
	if (StandaloneTexture.GetIsRooted())
		return 160;

	StandaloneTexture.SetTransactional(true);

	return 1;
}
)AS");

		Script.ReplaceInline(TEXT("__OUTER_TEXTURE_NAME__"), *FixtureNames.OuterTextureName, ESearchCase::CaseSensitive);
		Script.ReplaceInline(TEXT("__INNER_TEXTURE_NAME__"), *FixtureNames.InnerTextureName, ESearchCase::CaseSensitive);
		Script.ReplaceInline(TEXT("__STANDALONE_TEXTURE_NAME__"), *FixtureNames.StandaloneTextureName, ESearchCase::CaseSensitive);
		return Script;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptObjectLifecycleBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptObjectLifecycleBindingsTest,
	"Angelscript.TestModule.Bindings.ObjectLifecycleCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptObjectLifecycleBindingsTest::RunTest(const FString& Parameters)
{
	const FObjectLifecycleFixtureNames FixtureNames = CreateFixtureNames();
	const FString Script = BuildScriptSource(FixtureNames);
	UObject* const ExpectedTransientPackage = GetTransientPackage();
	bool bPassed = true;

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(ObjectLifecycleModuleName));
	};

	asIScriptModule* Module = BuildModule(*this, Engine, ObjectLifecycleModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	asIScriptFunction* Function = GetFunctionByDecl(*this, *Module, TEXT("int Entry()"));
	if (Function == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(*this, Engine, *Function, Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("UObject lifecycle bindings should execute the script-visible root, transient, transactional, and typed-outer checks"),
		Result,
		1);

	UTexture2D* OuterTexture = FindObject<UTexture2D>(GetTransientPackage(), *FixtureNames.OuterTextureName);
	UTexture2D* InnerTexture = OuterTexture != nullptr ? FindObject<UTexture2D>(OuterTexture, *FixtureNames.InnerTextureName) : nullptr;
	UTexture2D* StandaloneTexture = FindObject<UTexture2D>(GetTransientPackage(), *FixtureNames.StandaloneTextureName);

	bPassed &= TestNotNull(TEXT("Object lifecycle bindings should create the outer transient texture fixture"), OuterTexture);
	bPassed &= TestNotNull(TEXT("Object lifecycle bindings should create the inner transient texture fixture"), InnerTexture);
	bPassed &= TestNotNull(TEXT("Object lifecycle bindings should create the standalone transient texture fixture"), StandaloneTexture);

	if (OuterTexture != nullptr)
	{
		bPassed &= TestTrue(TEXT("Object lifecycle outer texture fixture should keep RF_Transient"), OuterTexture->HasAnyFlags(RF_Transient));
		bPassed &= TestFalse(TEXT("Object lifecycle outer texture fixture should not start transactional"), OuterTexture->HasAnyFlags(RF_Transactional));
		bPassed &= TestEqual(TEXT("Object lifecycle outer texture fixture should keep the transient package as its outer"), OuterTexture->GetOuter(), ExpectedTransientPackage);
		bPassed &= TestEqual(TEXT("Object lifecycle outer texture fixture should resolve the transient package as its package"), OuterTexture->GetPackage(), GetTransientPackage());
	}

	if (InnerTexture != nullptr && OuterTexture != nullptr)
	{
		bPassed &= TestTrue(TEXT("Object lifecycle inner fixture should keep RF_Transient"), InnerTexture->HasAnyFlags(RF_Transient));
		bPassed &= TestFalse(TEXT("Object lifecycle inner fixture should not start transactional"), InnerTexture->HasAnyFlags(RF_Transactional));
		bPassed &= TestEqual(TEXT("Object lifecycle inner fixture should keep the outer texture as its outer"), InnerTexture->GetOuter(), static_cast<UObject*>(OuterTexture));
		bPassed &= TestEqual(TEXT("Object lifecycle inner fixture should resolve the typed texture outer"), InnerTexture->GetTypedOuter(UTexture2D::StaticClass()), static_cast<UObject*>(OuterTexture));
		bPassed &= TestEqual(TEXT("Object lifecycle inner fixture should resolve the transient package as its package"), InnerTexture->GetPackage(), GetTransientPackage());
	}

	if (StandaloneTexture != nullptr)
	{
		bPassed &= TestTrue(TEXT("Object lifecycle standalone fixture should keep RF_Transient"), StandaloneTexture->HasAnyFlags(RF_Transient));
		bPassed &= TestFalse(TEXT("Object lifecycle standalone fixture should not remain rooted after RemoveFromRoot"), StandaloneTexture->IsRooted());
		bPassed &= TestTrue(TEXT("Object lifecycle standalone fixture should keep RF_Transactional after SetTransactional(true)"), StandaloneTexture->HasAnyFlags(RF_Transactional));
		bPassed &= TestEqual(TEXT("Object lifecycle standalone fixture should keep the transient package as its outer"), StandaloneTexture->GetOuter(), ExpectedTransientPackage);
		bPassed &= TestEqual(TEXT("Object lifecycle standalone fixture should resolve the transient package as its package"), StandaloneTexture->GetPackage(), GetTransientPackage());

		if (StandaloneTexture->IsRooted())
		{
			StandaloneTexture->RemoveFromRoot();
		}
		StandaloneTexture->ClearFlags(RF_Transactional);
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
