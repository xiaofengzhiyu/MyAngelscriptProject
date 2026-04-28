#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"
#include "../Shared/AngelscriptTestUtilities.h"

#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptFileHelperFlagBindingsTests_Private
{
	static constexpr ANSICHAR FileHelperAppendModuleName[] = "ASFileHelperAppendUtf8WithoutBom";
	static constexpr ANSICHAR FileHelperNoReplaceModuleName[] = "ASFileHelperNoReplaceExisting";

	FString MakeSavedTestFilename(const TCHAR* Filename)
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), Filename));
	}

	bool HasUtf8Bom(const TArray<uint8>& Bytes)
	{
		return Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptFileHelperFlagBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFileHelperAppendUtf8WithoutBomBindingsTest,
	"Angelscript.TestModule.Bindings.FileHelper.AppendUtf8WithoutBom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptFileHelperNoReplaceExistingBindingsTest,
	"Angelscript.TestModule.Bindings.FileHelper.NoReplaceExisting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptFileHelperAppendUtf8WithoutBomBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	const FString Filename = MakeSavedTestFilename(TEXT("AngelscriptFileHelperAppendUtf8WithoutBom.txt"));
	IFileManager::Get().Delete(*Filename, false, true, true);
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(FileHelperAppendModuleName));
		IFileManager::Get().Delete(*Filename, false, true, true);
	};

	FString Script = TEXT(R"AS(
int Entry()
{
	const FString Filename = "__FILE__";
	if (!FFileHelper::SaveStringToFile("Alpha", Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		return 10;

	if (!FFileHelper::SaveStringToFile("|Beta", Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, uint32(EFileWrite::Append)))
		return 20;

	FString Loaded;
	if (!FFileHelper::LoadFileToString(Loaded, Filename))
		return 30;
	if (!(Loaded == "Alpha|Beta"))
		return 40;

	return 1;
}
)AS");
	Script.ReplaceInline(TEXT("__FILE__"), *Filename.ReplaceCharWithEscapedChar());

	asIScriptModule* Module = BuildModule(*this, Engine, FileHelperAppendModuleName, Script);
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

	bPassed &= TestEqual(TEXT("FileHelper append write-flags script should acknowledge success"), Result, 1);

	FString NativeLoaded;
	const bool bLoadedString = FFileHelper::LoadFileToString(NativeLoaded, *Filename);
	bPassed &= TestTrue(TEXT("FileHelper append write-flags test should read the saved file natively"), bLoadedString);
	if (bLoadedString)
	{
		bPassed &= TestEqual(TEXT("FileHelper append write-flags should preserve appended UTF-8 text"), NativeLoaded, FString(TEXT("Alpha|Beta")));
	}

	TArray<uint8> FileBytes;
	const bool bLoadedBytes = FFileHelper::LoadFileToArray(FileBytes, *Filename);
	bPassed &= TestTrue(TEXT("FileHelper append write-flags test should read the raw saved bytes"), bLoadedBytes);
	if (bLoadedBytes)
	{
		bPassed &= TestFalse(TEXT("ForceUTF8WithoutBOM should not emit a UTF-8 BOM"), HasUtf8Bom(FileBytes));
		bPassed &= TestEqual(TEXT("ForceUTF8WithoutBOM append path should keep the plain UTF-8 byte count"), FileBytes.Num(), 10);
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptFileHelperNoReplaceExistingBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	const FString NativeFilename = MakeSavedTestFilename(TEXT("AngelscriptFileHelperNoReplaceExisting_Native.txt"));
	const FString ScriptFilename = MakeSavedTestFilename(TEXT("AngelscriptFileHelperNoReplaceExisting_Script.txt"));
	IFileManager::Get().Delete(*NativeFilename, false, true, true);
	IFileManager::Get().Delete(*ScriptFilename, false, true, true);
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(ANSI_TO_TCHAR(FileHelperNoReplaceModuleName));
		IFileManager::Get().Delete(*NativeFilename, false, true, true);
		IFileManager::Get().Delete(*ScriptFilename, false, true, true);
	};

	if (!TestTrue(
			TEXT("FileHelper NoReplaceExisting native baseline should create the initial file"),
			FFileHelper::SaveStringToFile(TEXT("Original"), *NativeFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		|| !TestTrue(
			TEXT("FileHelper NoReplaceExisting script baseline should create the initial file"),
			FFileHelper::SaveStringToFile(TEXT("Original"), *ScriptFilename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)))
	{
		return false;
	}

	const bool bExpectedSaveResult = FFileHelper::SaveStringToFile(
		TEXT("Replacement"),
		*NativeFilename,
		FFileHelper::EEncodingOptions::AutoDetect,
		&IFileManager::Get(),
		FILEWRITE_NoReplaceExisting);

	FString ExpectedContents;
	if (!TestTrue(
			TEXT("FileHelper NoReplaceExisting native baseline should be readable after the write attempt"),
			FFileHelper::LoadFileToString(ExpectedContents, *NativeFilename)))
	{
		return false;
	}

	FString Script = TEXT(R"AS(
int Entry()
{
	const FString Filename = "__FILE__";
	const bool bSaveResult = FFileHelper::SaveStringToFile("Replacement", Filename, FFileHelper::EEncodingOptions::AutoDetect, uint32(EFileWrite::NoReplaceExisting));
	if (bSaveResult != __EXPECTED_RESULT__)
		return 10;

	FString Loaded;
	if (!FFileHelper::LoadFileToString(Loaded, Filename))
		return 20;
	if (!(Loaded == "__EXPECTED_CONTENTS__"))
		return 30;

	return 1;
}
)AS");
	Script.ReplaceInline(TEXT("__FILE__"), *ScriptFilename.ReplaceCharWithEscapedChar());
	Script.ReplaceInline(TEXT("__EXPECTED_RESULT__"), bExpectedSaveResult ? TEXT("true") : TEXT("false"));
	Script.ReplaceInline(TEXT("__EXPECTED_CONTENTS__"), *ExpectedContents.ReplaceCharWithEscapedChar());

	asIScriptModule* Module = BuildModule(*this, Engine, FileHelperNoReplaceModuleName, Script);
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

	bPassed &= TestEqual(TEXT("FileHelper NoReplaceExisting script should match the native write-result baseline"), Result, 1);

	FString ScriptLoaded;
	const bool bLoadedString = FFileHelper::LoadFileToString(ScriptLoaded, *ScriptFilename);
	bPassed &= TestTrue(TEXT("FileHelper NoReplaceExisting script file should be readable after the write attempt"), bLoadedString);
	if (bLoadedString)
	{
		bPassed &= TestEqual(TEXT("FileHelper NoReplaceExisting script file should match the native post-write contents"), ScriptLoaded, ExpectedContents);
	}

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
