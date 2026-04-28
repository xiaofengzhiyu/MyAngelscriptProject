#include "../Shared/AngelscriptTestUtilities.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestEngineHelper.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptGuidBindingsTest,
	"Angelscript.TestModule.Bindings.GuidCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPathsBindingsTest,
	"Angelscript.TestModule.Bindings.PathsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPathsExactBindingsTest,
	"Angelscript.TestModule.Bindings.PathsExactCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptPathsMutationBindingsTest,
	"Angelscript.TestModule.Bindings.PathsMutationCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNumberFormattingOptionsBindingsTest,
	"Angelscript.TestModule.Bindings.NumberFormattingOptionsCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptNumberFormattingOptionsFormattingBindingsTest,
	"Angelscript.TestModule.Bindings.NumberFormattingOptionsFormattingCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptGuidBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASGuidCompat",
		TEXT(R"(
int Entry()
{
	FGuid ExplicitGuid(1, 2, 3, 4);
	if (!ExplicitGuid.IsValid())
		return 10;

	FString GuidString = ExplicitGuid.ToString(EGuidFormats::DigitsWithHyphens);
	if (GuidString.IsEmpty())
		return 20;

	FGuid ParsedGuid;
	if (!FGuid::Parse(GuidString, ParsedGuid))
		return 30;
	if (!(ParsedGuid == ExplicitGuid))
		return 40;
	if (ParsedGuid.opCmp(ExplicitGuid) != 0)
		return 50;

	FGuid ParsedExactGuid;
	if (!FGuid::ParseExact(GuidString, EGuidFormats::DigitsWithHyphens, ParsedExactGuid))
		return 60;
	if (!(ParsedExactGuid == ExplicitGuid))
		return 70;

	FGuid Copy = ExplicitGuid;
	if (!(Copy == ExplicitGuid))
		return 80;

	Copy.Invalidate();
	if (Copy.IsValid())
		return 90;

	FGuid NewGuid = FGuid::NewGuid();
	if (!NewGuid.IsValid())
		return 100;
	if (NewGuid.GetTypeHash() == 0)
		return 110;

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

	TestEqual(TEXT("Guid compat operations should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

bool FAngelscriptPathsBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASPathsCompat",
		TEXT(R"(
int Entry()
{
	FString ProjectDir = FPaths::ProjectDir();
	if (ProjectDir.IsEmpty())
		return 10;

	FString Combined = FPaths::CombinePaths(ProjectDir, "Script/Test.as");
	if (Combined.IsEmpty())
		return 20;

	FString Relative = "Script/Test.as";
	if (!FPaths::IsRelative(Relative))
		return 30;

	FString FullPath = FPaths::ConvertRelativePathToFull(Relative);
	if (FullPath.IsEmpty())
		return 40;

	FString FullFromBase = FPaths::ConvertRelativePathToFull(ProjectDir, Relative);
	if (FullFromBase.IsEmpty())
		return 50;

	FString Extension = FPaths::GetExtension(Combined, true);
	if (!(Extension == ".as"))
		return 60;

	FString Clean = FPaths::GetCleanFilename(Combined);
	if (!(Clean == "Test.as"))
		return 70;

	FString Base = FPaths::GetBaseFilename(Combined, true);
	if (!(Base == "Test"))
		return 80;

	FString PathOnly = FPaths::GetPath(Combined);
	if (PathOnly.IsEmpty())
		return 90;

	if (!FPaths::DirectoryExists(ProjectDir))
		return 100;
	if (FPaths::FileExists(ProjectDir))
		return 110;

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

	TestEqual(TEXT("Paths compat operations should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

bool FAngelscriptPathsExactBindingsTest::RunTest(const FString& Parameters)
{
	auto EscapeScriptString = [](const FString& Value) -> FString
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Escaped;
	};

	const FString ProjectDir = FPaths::ProjectDir();
	const FString Relative = TEXT("Script/Test.as");
	const FString ExpectedCombined = FPaths::Combine(ProjectDir, Relative);
	const FString ExpectedFullFromRelative = FPaths::ConvertRelativePathToFull(Relative);
	const FString ExpectedFullFromBase = FPaths::ConvertRelativePathToFull(ProjectDir, Relative);
	const FString ExpectedPathOnly = FPaths::GetPath(ExpectedCombined);
	const FString ExpectedExtension = FPaths::GetExtension(ExpectedCombined, true);
	const FString ExpectedBase = FPaths::GetBaseFilename(ExpectedCombined, true);
	const FString CaseVariantCombined = ExpectedCombined.ToUpper();
	const FString PrefixCollisionPath = FPaths::Combine(ProjectDir, TEXT("ScriptBackup/Test.as"));
	const bool bExpectedSamePathIgnoresCase = FPaths::IsSamePath(ExpectedCombined, CaseVariantCombined);
	const bool bExpectedCombinedIsUnderScriptDir = FPaths::IsUnderDirectory(ExpectedCombined, ExpectedPathOnly);
	const bool bExpectedPrefixCollisionIsUnderScriptDir = FPaths::IsUnderDirectory(PrefixCollisionPath, ExpectedPathOnly);
	const bool bExpectedProjectDirExists = FPaths::DirectoryExists(ProjectDir);
	const bool bExpectedProjectDirIsFile = FPaths::FileExists(ProjectDir);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FString Script = TEXT(R"(
int Entry()
{
	const FString Relative = "__RELATIVE__";
	const FString ExpectedCombined = "__EXPECTED_COMBINED__";
	const FString ExpectedFullFromRelative = "__EXPECTED_FULL_FROM_RELATIVE__";
	const FString ExpectedFullFromBase = "__EXPECTED_FULL_FROM_BASE__";
	const FString ExpectedPathOnly = "__EXPECTED_PATH_ONLY__";
	const FString ExpectedExtension = "__EXPECTED_EXTENSION__";
	const FString ExpectedBase = "__EXPECTED_BASE__";
	const FString CaseVariantCombined = "__CASE_VARIANT_COMBINED__";
	const FString PrefixCollisionPath = "__PREFIX_COLLISION_PATH__";
	const bool bExpectedSamePathIgnoresCase = __EXPECTED_SAME_PATH_IGNORES_CASE__;
	const bool bExpectedCombinedIsUnderScriptDir = __EXPECTED_COMBINED_UNDER_SCRIPT_DIR__;
	const bool bExpectedPrefixCollisionIsUnderScriptDir = __EXPECTED_PREFIX_COLLISION_UNDER_SCRIPT_DIR__;
	const bool bExpectedProjectDirExists = __EXPECTED_PROJECT_DIR_EXISTS__;
	const bool bExpectedProjectDirIsFile = __EXPECTED_PROJECT_DIR_IS_FILE__;

	FString ProjectDir = FPaths::ProjectDir();
	if (!(ProjectDir == "__EXPECTED_PROJECT_DIR__"))
		return 10;

	FString Combined = FPaths::CombinePaths(ProjectDir, Relative);
	if (!(Combined == ExpectedCombined))
		return 20;

	if (!(FPaths::ConvertRelativePathToFull(Relative) == ExpectedFullFromRelative))
		return 30;
	if (!(FPaths::ConvertRelativePathToFull(ProjectDir, Relative) == ExpectedFullFromBase))
		return 40;
	if (!(FPaths::GetPath(Combined) == ExpectedPathOnly))
		return 50;
	if (!(FPaths::GetExtension(Combined, true) == ExpectedExtension))
		return 60;
	if (!(FPaths::GetBaseFilename(Combined, true) == ExpectedBase))
		return 70;
	if (FPaths::DirectoryExists(ProjectDir) != bExpectedProjectDirExists)
		return 80;
	if (FPaths::FileExists(ProjectDir) != bExpectedProjectDirIsFile)
		return 90;
	if (FPaths::IsSamePath(Combined, CaseVariantCombined) != bExpectedSamePathIgnoresCase)
		return 100;
	if (FPaths::IsUnderDirectory(Combined, ExpectedPathOnly) != bExpectedCombinedIsUnderScriptDir)
		return 110;
	if (FPaths::IsUnderDirectory(PrefixCollisionPath, ExpectedPathOnly) != bExpectedPrefixCollisionIsUnderScriptDir)
		return 120;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("__RELATIVE__"), *EscapeScriptString(Relative), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PROJECT_DIR__"), *EscapeScriptString(ProjectDir), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_COMBINED__"), *EscapeScriptString(ExpectedCombined), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_FULL_FROM_RELATIVE__"), *EscapeScriptString(ExpectedFullFromRelative), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_FULL_FROM_BASE__"), *EscapeScriptString(ExpectedFullFromBase), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PATH_ONLY__"), *EscapeScriptString(ExpectedPathOnly), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_EXTENSION__"), *EscapeScriptString(ExpectedExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_BASE__"), *EscapeScriptString(ExpectedBase), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__CASE_VARIANT_COMBINED__"), *EscapeScriptString(CaseVariantCombined), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__PREFIX_COLLISION_PATH__"), *EscapeScriptString(PrefixCollisionPath), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_SAME_PATH_IGNORES_CASE__"), bExpectedSamePathIgnoresCase ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_COMBINED_UNDER_SCRIPT_DIR__"), bExpectedCombinedIsUnderScriptDir ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PREFIX_COLLISION_UNDER_SCRIPT_DIR__"), bExpectedPrefixCollisionIsUnderScriptDir ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PROJECT_DIR_EXISTS__"), bExpectedProjectDirExists ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PROJECT_DIR_IS_FILE__"), bExpectedProjectDirIsFile ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASPathsExactCompat",
		Script);
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

	TestEqual(TEXT("Paths exact compat operations should match native FPaths results"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptPathsMutationBindingsTest::RunTest(const FString& Parameters)
{
	auto EscapeScriptString = [](const FString& Value) -> FString
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Escaped;
	};

	const FString PathLeafInput = TEXT("Script\\Automation//Nested");
	const FString PathWithExtension = TEXT("Script\\Automation//Nested/Test.data");
	const FString PathWithoutExtension = TEXT("Script\\Automation//Nested/Test");
	const FString NormalizeFilenameInput = TEXT("Script\\\\Automation//Nested\\\\Test.as");
	const FString NormalizeDirectoryInput = TEXT("Script\\\\Automation//Nested\\\\");
	const FString CollapseInputOriginal = TEXT("Script/Automation/../Bindings/Test.as");
	const FString DuplicateSlashesInputOriginal = TEXT("Script////Automation\\\\\\\\Bindings//Test.as");
	const FString StandardFilenameInputOriginal = TEXT(".\\Script\\Automation\\..\\Bindings\\Test.as");
	const FString PlatformFilenameInputOriginal = TEXT("Script/Automation/Bindings/Test.as");

	const FString ExpectedPathLeaf = FPaths::GetPathLeaf(PathLeafInput);
	const FString ExpectedChangedExtension = FPaths::ChangeExtension(PathWithExtension, TEXT("log"));
	const FString ExpectedSetExtension = FPaths::SetExtension(PathWithoutExtension, TEXT("json"));

	FString ExpectedSplitPath;
	FString ExpectedSplitFilename;
	FString ExpectedSplitExtension;
	FPaths::Split(PathWithExtension, ExpectedSplitPath, ExpectedSplitFilename, ExpectedSplitExtension);

	FString ExpectedNormalizedFilename = NormalizeFilenameInput;
	FPaths::NormalizeFilename(ExpectedNormalizedFilename);

	FString ExpectedNormalizedDirectory = NormalizeDirectoryInput;
	FPaths::NormalizeDirectoryName(ExpectedNormalizedDirectory);

	FString ExpectedCollapsedPath = CollapseInputOriginal;
	const bool bExpectedCollapseSucceeded = FPaths::CollapseRelativeDirectories(ExpectedCollapsedPath);

	FString ExpectedDeduplicatedPath = DuplicateSlashesInputOriginal;
	FPaths::RemoveDuplicateSlashes(ExpectedDeduplicatedPath);

	FString ExpectedStandardFilename = StandardFilenameInputOriginal;
	FPaths::MakeStandardFilename(ExpectedStandardFilename);

	FString ExpectedPlatformFilename = PlatformFilenameInputOriginal;
	FPaths::MakePlatformFilename(ExpectedPlatformFilename);

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FString Script = TEXT(R"(
int Entry()
{
	const FString PathLeafInput = "__PATH_LEAF_INPUT__";
	const FString PathWithExtension = "__PATH_WITH_EXTENSION__";
	const FString PathWithoutExtension = "__PATH_WITHOUT_EXTENSION__";
	const FString NormalizeFilenameInput = "__NORMALIZE_FILENAME_INPUT__";
	const FString NormalizeDirectoryInput = "__NORMALIZE_DIRECTORY_INPUT__";
	const FString CollapseInput = "__COLLAPSE_INPUT__";
	const FString DuplicateSlashesInput = "__DUPLICATE_SLASHES_INPUT__";
	const FString StandardFilenameInput = "__STANDARD_FILENAME_INPUT__";
	const FString PlatformFilenameInput = "__PLATFORM_FILENAME_INPUT__";

	if (!(FPaths::GetPathLeaf(PathLeafInput) == "__EXPECTED_PATH_LEAF__"))
		return 10;

	if (!(FPaths::ChangeExtension(PathWithExtension, "log") == "__EXPECTED_CHANGED_EXTENSION__"))
		return 20;

	if (!(FPaths::SetExtension(PathWithoutExtension, "json") == "__EXPECTED_SET_EXTENSION__"))
		return 30;

	FString SplitPath;
	FString SplitFilename;
	FString SplitExtension;
	FPaths::Split(PathWithExtension, SplitPath, SplitFilename, SplitExtension);
	if (!(SplitPath == "__EXPECTED_SPLIT_PATH__"))
		return 40;
	if (!(SplitFilename == "__EXPECTED_SPLIT_FILENAME__"))
		return 50;
	if (!(SplitExtension == "__EXPECTED_SPLIT_EXTENSION__"))
		return 60;

	FString NormalizedFilename = NormalizeFilenameInput;
	FPaths::NormalizeFilename(NormalizedFilename);
	if (!(NormalizedFilename == "__EXPECTED_NORMALIZED_FILENAME__"))
		return 70;

	FString NormalizedDirectory = NormalizeDirectoryInput;
	FPaths::NormalizeDirectoryName(NormalizedDirectory);
	if (!(NormalizedDirectory == "__EXPECTED_NORMALIZED_DIRECTORY__"))
		return 80;

	FString CollapsedPath = CollapseInput;
	if (FPaths::CollapseRelativeDirectories(CollapsedPath) != __EXPECTED_COLLAPSE_SUCCEEDED__)
		return 90;
	if (!(CollapsedPath == "__EXPECTED_COLLAPSED_PATH__"))
		return 100;

	FString DeduplicatedPath = DuplicateSlashesInput;
	FPaths::RemoveDuplicateSlashes(DeduplicatedPath);
	if (!(DeduplicatedPath == "__EXPECTED_DEDUPLICATED_PATH__"))
		return 110;

	FString StandardFilename = StandardFilenameInput;
	FPaths::MakeStandardFilename(StandardFilename);
	if (!(StandardFilename == "__EXPECTED_STANDARD_FILENAME__"))
		return 120;

	FString PlatformFilename = PlatformFilenameInput;
	FPaths::MakePlatformFilename(PlatformFilename);
	if (!(PlatformFilename == "__EXPECTED_PLATFORM_FILENAME__"))
		return 130;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("__PATH_LEAF_INPUT__"), *EscapeScriptString(PathLeafInput), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__PATH_WITH_EXTENSION__"), *EscapeScriptString(PathWithExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__PATH_WITHOUT_EXTENSION__"), *EscapeScriptString(PathWithoutExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__NORMALIZE_FILENAME_INPUT__"), *EscapeScriptString(NormalizeFilenameInput), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__NORMALIZE_DIRECTORY_INPUT__"), *EscapeScriptString(NormalizeDirectoryInput), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__COLLAPSE_INPUT__"), *EscapeScriptString(CollapseInputOriginal), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__DUPLICATE_SLASHES_INPUT__"), *EscapeScriptString(DuplicateSlashesInputOriginal), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__STANDARD_FILENAME_INPUT__"), *EscapeScriptString(StandardFilenameInputOriginal), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__PLATFORM_FILENAME_INPUT__"), *EscapeScriptString(PlatformFilenameInputOriginal), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PATH_LEAF__"), *EscapeScriptString(ExpectedPathLeaf), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_CHANGED_EXTENSION__"), *EscapeScriptString(ExpectedChangedExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_SET_EXTENSION__"), *EscapeScriptString(ExpectedSetExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_SPLIT_PATH__"), *EscapeScriptString(ExpectedSplitPath), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_SPLIT_FILENAME__"), *EscapeScriptString(ExpectedSplitFilename), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_SPLIT_EXTENSION__"), *EscapeScriptString(ExpectedSplitExtension), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_NORMALIZED_FILENAME__"), *EscapeScriptString(ExpectedNormalizedFilename), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_NORMALIZED_DIRECTORY__"), *EscapeScriptString(ExpectedNormalizedDirectory), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_COLLAPSE_SUCCEEDED__"), bExpectedCollapseSucceeded ? TEXT("true") : TEXT("false"), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_COLLAPSED_PATH__"), *EscapeScriptString(ExpectedCollapsedPath), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_DEDUPLICATED_PATH__"), *EscapeScriptString(ExpectedDeduplicatedPath), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_STANDARD_FILENAME__"), *EscapeScriptString(ExpectedStandardFilename), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_PLATFORM_FILENAME__"), *EscapeScriptString(ExpectedPlatformFilename), ESearchCase::CaseSensitive);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASPathsMutationCompat",
		Script);
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

	TestEqual(TEXT("Paths mutation compat operations should match native FPaths results"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

bool FAngelscriptNumberFormattingOptionsBindingsTest::RunTest(const FString& Parameters)
{
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE();
	ASTEST_BEGIN_SHARE
	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASNumberFormattingOptionsCompat",
		TEXT(R"(
int Entry()
{
	FNumberFormattingOptions Options;
	Options.SetAlwaysSign(true)
		.SetUseGrouping(false)
		.SetMinimumIntegralDigits(2)
		.SetMaximumIntegralDigits(4)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(3);

	FNumberFormattingOptions Copy = Options;
	if (!Options.IsIdentical(Copy))
		return 10;
	if (!(Options.GetTypeHash() == Copy.GetTypeHash()))
		return 20;

	FNumberFormattingOptions DefaultGrouped = FNumberFormattingOptions::DefaultWithGrouping();
	FNumberFormattingOptions DefaultUngrouped = FNumberFormattingOptions::DefaultNoGrouping();
	if (DefaultGrouped.IsIdentical(DefaultUngrouped))
		return 30;

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

	TestEqual(TEXT("NumberFormattingOptions compat operations should behave as expected"), Result, 1);
	ASTEST_END_SHARE

	return true;
}

bool FAngelscriptNumberFormattingOptionsFormattingBindingsTest::RunTest(const FString& Parameters)
{
	auto EscapeScriptString = [](const FString& Value) -> FString
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Escaped.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Escaped.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Escaped;
	};

	constexpr double Sample = 1234.5;

	FNumberFormattingOptions UngroupedOptions;
	UngroupedOptions
		.SetAlwaysSign(true)
		.SetUseGrouping(false)
		.SetMinimumIntegralDigits(2)
		.SetMaximumIntegralDigits(4)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(3);

	FNumberFormattingOptions GroupedOptions;
	GroupedOptions
		.SetAlwaysSign(false)
		.SetUseGrouping(true)
		.SetMinimumIntegralDigits(2)
		.SetMaximumIntegralDigits(4)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(3);

	const FString ExpectedUngrouped = FText::AsNumber(Sample, &UngroupedOptions).ToString();
	const FString ExpectedGrouped = FText::AsNumber(Sample, &GroupedOptions).ToString();

	if (!TestFalse(
			TEXT("NumberFormattingOptions formatting baseline should produce distinct grouped and ungrouped outputs"),
			ExpectedUngrouped == ExpectedGrouped))
	{
		return false;
	}

	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	FString Script = TEXT(R"(
int Entry()
{
	const float64 Sample = 1234.5;

	FNumberFormattingOptions UngroupedOptions;
	UngroupedOptions
		.SetAlwaysSign(true)
		.SetUseGrouping(false)
		.SetMinimumIntegralDigits(2)
		.SetMaximumIntegralDigits(4)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(3);

	FNumberFormattingOptions GroupedOptions;
	GroupedOptions
		.SetAlwaysSign(false)
		.SetUseGrouping(true)
		.SetMinimumIntegralDigits(2)
		.SetMaximumIntegralDigits(4)
		.SetMinimumFractionalDigits(1)
		.SetMaximumFractionalDigits(3);

	FString UngroupedResult = FText::AsNumber(Sample, UngroupedOptions).ToString();
	if (!(UngroupedResult == "__EXPECTED_UNGROUPED__"))
		return 10;

	FString GroupedResult = FText::AsNumber(Sample, GroupedOptions).ToString();
	if (!(GroupedResult == "__EXPECTED_GROUPED__"))
		return 20;

	if (UngroupedOptions.IsIdentical(GroupedOptions))
		return 30;

	if (UngroupedResult == GroupedResult)
		return 40;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("__EXPECTED_UNGROUPED__"), *EscapeScriptString(ExpectedUngrouped), ESearchCase::CaseSensitive);
	Script.ReplaceInline(TEXT("__EXPECTED_GROUPED__"), *EscapeScriptString(ExpectedGrouped), ESearchCase::CaseSensitive);

	asIScriptModule* Module = BuildModule(
		*this,
		Engine,
		"ASNumberFormattingOptionsFormattingCompat",
		Script);
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

	TestEqual(TEXT("NumberFormattingOptions formatting compat should match native FText::AsNumber output"), Result, 1);
	ASTEST_END_SHARE_CLEAN

	return true;
}

#endif
