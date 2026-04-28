#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "Blueprint/UserWidget.h"
#include "Layout/Geometry.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Rendering/SlateLayoutTransform.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptWidgetBindingsTests_Private
{
	static constexpr ANSICHAR WidgetLayoutValueModuleName[] = "ASWidgetLayoutValueCompat";
	static constexpr ANSICHAR WidgetGeometryPaletteModuleName[] = "ASWidgetGeometryAndPaletteCompat";
	static constexpr TCHAR WidgetFixtureModuleName[] = TEXT("ASWidgetPaletteCompatFixture");
	static constexpr TCHAR WidgetFixtureClassName[] = TEXT("UWidgetPaletteCompatFixture");
	static constexpr TCHAR WidgetFixtureObjectName[] = TEXT("WidgetPaletteCompatObject");

	void ReplaceToken(FString& Source, const TCHAR* Token, const FString& Replacement)
	{
		Source.ReplaceInline(Token, *Replacement, ESearchCase::CaseSensitive);
	}

	bool SetArgAddressChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Address,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind address argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgAddress(ArgumentIndex, Address),
			static_cast<int32>(asSUCCESS));
	}

	bool SetArgObjectChecked(
		FAutomationTestBase& Test,
		asIScriptContext& Context,
		const asUINT ArgumentIndex,
		void* Object,
		const TCHAR* ContextLabel)
	{
		return Test.TestEqual(
			*FString::Printf(TEXT("%s should bind object argument %u"), ContextLabel, static_cast<uint32>(ArgumentIndex)),
			Context.SetArgObject(ArgumentIndex, Object),
			static_cast<int32>(asSUCCESS));
	}

	bool ExecuteIntFunction(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		asIScriptModule& Module,
		const TCHAR* FunctionDecl,
		TFunctionRef<bool(asIScriptContext&)> BindArguments,
		const TCHAR* ContextLabel,
		int32& OutResult)
	{
		asIScriptFunction* Function = GetFunctionByDecl(Test, Module, FunctionDecl);
		if (Function == nullptr)
		{
			return false;
		}

		FAngelscriptEngineScope EngineScope(Engine);
		asIScriptContext* Context = Engine.CreateContext();
		if (!Test.TestNotNull(*FString::Printf(TEXT("%s should create an execution context"), ContextLabel), Context))
		{
			return false;
		}

		ON_SCOPE_EXIT
		{
			Context->Release();
		};

		const int PrepareResult = Context->Prepare(Function);
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should prepare successfully"), ContextLabel),
			PrepareResult,
			static_cast<int32>(asSUCCESS)))
		{
			return false;
		}

		if (!BindArguments(*Context))
		{
			return false;
		}

		const int ExecuteResult = Context->Execute();
		if (!Test.TestEqual(
			*FString::Printf(TEXT("%s should execute successfully"), ContextLabel),
			ExecuteResult,
			static_cast<int32>(asEXECUTION_FINISHED)))
		{
			return false;
		}

		OutResult = static_cast<int32>(Context->GetReturnDWord());
		return true;
	}

	UClass* CreateWidgetFixtureClass(FAutomationTestBase& Test, FAngelscriptEngine& Engine)
	{
		const bool bCompiled = CompileAnnotatedModuleFromMemory(
			&Engine,
			FName(WidgetFixtureModuleName),
			FString(WidgetFixtureModuleName) + TEXT(".as"),
			FString::Printf(
				TEXT(R"AS(
UCLASS()
class %s : UUserWidget
{
}
)AS"),
				WidgetFixtureClassName));
		if (!Test.TestTrue(TEXT("WidgetGeometryAndPaletteCompat should compile a concrete scripted widget fixture class"), bCompiled))
		{
			return nullptr;
		}

		UClass* WidgetClass = FindGeneratedClass(&Engine, FName(WidgetFixtureClassName));
		Test.TestNotNull(TEXT("WidgetGeometryAndPaletteCompat should publish the generated widget fixture class"), WidgetClass);
		return WidgetClass;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptWidgetBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWidgetLayoutValueCompatBindingsTest,
	"Angelscript.TestModule.Bindings.WidgetLayoutValueCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptWidgetGeometryAndPaletteCompatBindingsTest,
	"Angelscript.TestModule.Bindings.WidgetGeometryAndPaletteCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptWidgetLayoutValueCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASWidgetLayoutValueCompat"));
	};

	const FGeometry RootGeometry = FGeometry::MakeRoot(
		FVector2f(120.0f, 60.0f),
		FSlateLayoutTransform(FVector2f(10.0f, 20.0f)));

	const FString Script = TEXT(R"AS(
int VerifyWidgetLayoutValueCompat(const FGeometry& RootGeometry)
{
	FMargin UniformMargin(4.0f);
	if (UniformMargin.Left != 4.0f || UniformMargin.Top != 4.0f || UniformMargin.Right != 4.0f || UniformMargin.Bottom != 4.0f)
		return 10;

	FMargin AxisMargin(3.0f, 5.0f);
	if (AxisMargin.Left != 3.0f || AxisMargin.Top != 5.0f || AxisMargin.Right != 3.0f || AxisMargin.Bottom != 5.0f)
		return 20;

	FMargin VectorMargin(FVector2D(3.0f, 5.0f));
	if (!(AxisMargin == VectorMargin))
		return 30;

	FMargin QuadMargin(1.0f, 2.0f, 3.0f, 4.0f);
	FMargin Vector4Margin(FVector4(1.0f, 2.0f, 3.0f, 4.0f));
	if (!(QuadMargin == Vector4Margin))
		return 40;

	FMargin AddedMargin = QuadMargin + UniformMargin;
	if (AddedMargin.Left != 5.0f || AddedMargin.Top != 6.0f || AddedMargin.Right != 7.0f || AddedMargin.Bottom != 8.0f)
		return 50;

	FMargin SubtractedMargin = AddedMargin - UniformMargin;
	if (!(SubtractedMargin == QuadMargin))
		return 60;

	FMargin ScaledMargin = QuadMargin * 2.0f;
	if (ScaledMargin.Left != 2.0f || ScaledMargin.Top != 4.0f || ScaledMargin.Right != 6.0f || ScaledMargin.Bottom != 8.0f)
		return 70;

	FMargin ComponentScaledMargin = QuadMargin * FMargin(2.0f, 3.0f, 4.0f, 5.0f);
	if (ComponentScaledMargin.Left != 2.0f || ComponentScaledMargin.Top != 6.0f || ComponentScaledMargin.Right != 12.0f || ComponentScaledMargin.Bottom != 20.0f)
		return 80;

	FVector2D MarginTopLeft = QuadMargin.GetTopLeft();
	if (MarginTopLeft.X != 1.0f || MarginTopLeft.Y != 2.0f)
		return 90;

	FVector2D DesiredSize = QuadMargin.GetDesiredSize();
	if (DesiredSize.X != 4.0f || DesiredSize.Y != 6.0f)
		return 100;

	if (QuadMargin.GetTotalSpaceAlongHorizontal() != 4.0f || QuadMargin.GetTotalSpaceAlongVertical() != 6.0f)
		return 110;

	FAnchors UniformAnchors(0.25f);
	if (UniformAnchors.Minimum.X != 0.25f || UniformAnchors.Minimum.Y != 0.25f || UniformAnchors.Maximum.X != 0.25f || UniformAnchors.Maximum.Y != 0.25f)
		return 120;

	FAnchors FixedAnchors(0.5f, 0.75f);
	if (FixedAnchors.IsStretchedHorizontal() || FixedAnchors.IsStretchedVertical())
		return 130;

	FAnchors StretchAnchors(0.125f, 0.25f, 0.875f, 0.75f);
	if (StretchAnchors.Minimum.X != 0.125f || StretchAnchors.Minimum.Y != 0.25f || StretchAnchors.Maximum.X != 0.875f || StretchAnchors.Maximum.Y != 0.75f)
		return 140;
	if (!StretchAnchors.IsStretchedHorizontal() || !StretchAnchors.IsStretchedVertical())
		return 150;
	if (!(StretchAnchors == FAnchors(0.125f, 0.25f, 0.875f, 0.75f)))
		return 160;

	FVector2D RootLocalSize = RootGeometry.GetLocalSize();
	if (RootLocalSize.X != 120.0f || RootLocalSize.Y != 60.0f)
		return 170;

	FVector2D RootAbsoluteSize = RootGeometry.GetAbsoluteSize();
	if (RootAbsoluteSize.X != 120.0f || RootAbsoluteSize.Y != 60.0f)
		return 180;

	FVector2D RootAbsolutePoint = RootGeometry.LocalToAbsolute(FVector2D(7.0f, 9.0f));
	if (RootAbsolutePoint.X != 17.0f || RootAbsolutePoint.Y != 29.0f)
		return 190;

	FVector2D RootLocalPoint = RootGeometry.AbsoluteToLocal(FVector2D(17.0f, 29.0f));
	if (RootLocalPoint.X != 7.0f || RootLocalPoint.Y != 9.0f)
		return 200;

	FGeometry ChildGeometry = RootGeometry.MakeChild(FVector2D(5.0f, 7.0f), FVector2D(30.0f, 20.0f));
	FVector2D ChildLocalSize = ChildGeometry.GetLocalSize();
	if (ChildLocalSize.X != 30.0f || ChildLocalSize.Y != 20.0f)
		return 210;

	FVector2D ChildAbsoluteSize = ChildGeometry.GetAbsoluteSize();
	if (ChildAbsoluteSize.X != 30.0f || ChildAbsoluteSize.Y != 20.0f)
		return 220;

	FVector2D ChildAbsolutePoint = ChildGeometry.LocalToAbsolute(FVector2D(1.0f, 2.0f));
	if (ChildAbsolutePoint.X != 16.0f || ChildAbsolutePoint.Y != 29.0f)
		return 230;

	FVector2D ChildLocalPoint = ChildGeometry.AbsoluteToLocal(FVector2D(16.0f, 29.0f));
	if (ChildLocalPoint.X != 1.0f || ChildLocalPoint.Y != 2.0f)
		return 240;

	return 1;
}
)AS");

	asIScriptModule* Module = BuildModule(*this, Engine, WidgetLayoutValueModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyWidgetLayoutValueCompat(const FGeometry& RootGeometry)"),
			[this, &RootGeometry](asIScriptContext& Context)
			{
				return SetArgAddressChecked(*this, Context, 0, const_cast<FGeometry*>(&RootGeometry), TEXT("VerifyWidgetLayoutValueCompat"));
			},
			TEXT("VerifyWidgetLayoutValueCompat"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Widget layout value bindings should preserve FMargin, FAnchors, and FGeometry constructor and transform semantics"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptWidgetGeometryAndPaletteCompatBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_FULL();
	ASTEST_BEGIN_FULL
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASWidgetGeometryAndPaletteCompat"));
		Engine.DiscardModule(TEXT("ASWidgetPaletteCompatFixture"));
	};

	UClass* WidgetClass = CreateWidgetFixtureClass(*this, Engine);
	if (WidgetClass == nullptr)
	{
		return false;
	}

	UUserWidget* Widget = NewObject<UUserWidget>(
		GetTransientPackage(),
		WidgetClass,
		WidgetFixtureObjectName,
		RF_Transient);
	if (!TestNotNull(TEXT("WidgetGeometryAndPaletteCompat should create the transient scripted widget fixture"), Widget))
	{
		return false;
	}

	ON_SCOPE_EXIT
	{
		if (Widget != nullptr)
		{
			Widget->MarkAsGarbage();
		}
	};

	const FString InitialPaletteCategory = TEXT("InitialWidgetPaletteCompat");
	const FString UpdatedPaletteCategory = TEXT("UpdatedWidgetPaletteCompat");
#if WITH_EDITORONLY_DATA
	Widget->PaletteCategory = FText::AsCultureInvariant(InitialPaletteCategory);
#endif

	FString Script = TEXT(R"AS(
void ProbeAddToViewport(UUserWidget Widget)
{
	if (Widget != null)
	{
		Widget.AddToViewport(3);
	}
}

int VerifyWidgetGeometryAndPaletteCompat(UUserWidget Widget)
{
	if (Widget == null)
		return 10;

	if (!(Widget.GetPaletteCategory().ToString() == "__INITIAL_PALETTE__"))
		return 20;

	Widget.SetPaletteCategory(FText::AsCultureInvariant("__UPDATED_PALETTE__"));
	if (!(Widget.GetPaletteCategory().ToString() == "__UPDATED_PALETTE__"))
		return 30;

	return 1;
}
)AS");
	ReplaceToken(Script, TEXT("__INITIAL_PALETTE__"), InitialPaletteCategory);
	ReplaceToken(Script, TEXT("__UPDATED_PALETTE__"), UpdatedPaletteCategory);

	asIScriptModule* Module = BuildModule(*this, Engine, WidgetGeometryPaletteModuleName, Script);
	if (Module == nullptr)
	{
		return false;
	}

	int32 Result = INDEX_NONE;
	if (!ExecuteIntFunction(
			*this,
			Engine,
			*Module,
			TEXT("int VerifyWidgetGeometryAndPaletteCompat(UUserWidget Widget)"),
			[this, Widget](asIScriptContext& Context)
			{
				return SetArgObjectChecked(*this, Context, 0, Widget, TEXT("VerifyWidgetGeometryAndPaletteCompat"));
			},
			TEXT("VerifyWidgetGeometryAndPaletteCompat"),
			Result))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("Widget palette bindings should round-trip palette category text and keep AddToViewport script-compilable"),
		Result,
		1);
	bPassed &= TestTrue(
		TEXT("WidgetGeometryAndPaletteCompat should leave the native widget with the palette category written by the script"),
#if WITH_EDITORONLY_DATA
		Widget->PaletteCategory.ToString() == UpdatedPaletteCategory);
#else
		false);
#endif

	ASTEST_END_FULL
	return bPassed;
}

#endif
