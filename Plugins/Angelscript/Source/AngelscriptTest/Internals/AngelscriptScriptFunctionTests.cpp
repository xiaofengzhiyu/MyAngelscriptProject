#include "Angelscript/AngelscriptTestSupport.h"
#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#include "StartAngelscriptHeaders.h"
#include "source/as_scriptfunction.h"
#include "EndAngelscriptHeaders.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Internals_AngelscriptScriptFunctionTests_Private
{
	struct FExpectedParamMetadata
	{
		asUINT Index = 0;
		int32 TypeId = 0;
		asDWORD Flags = 0;
		const TCHAR* Name = nullptr;
		const TCHAR* DefaultArg = nullptr;
	};

	FString ToFString(const char* Value)
	{
		return Value != nullptr ? UTF8_TO_TCHAR(Value) : FString();
	}

	bool DeclarationListContains(const TArray<FString>& Declarations, const TCHAR* Needle)
	{
		return Declarations.ContainsByPredicate(
			[Needle](const FString& Declaration)
			{
				return Declaration.Contains(Needle);
			});
	}

	bool VerifyParamMetadata(
		FAutomationTestBase& Test,
		asIScriptFunction& Function,
		const FExpectedParamMetadata& Expected)
	{
		int ParamTypeId = 0;
		asDWORD ParamFlags = 0;
		const char* RawParamName = nullptr;
		const char* RawDefaultArg = reinterpret_cast<const char*>(1);
		const int Result = Function.GetParam(Expected.Index, &ParamTypeId, &ParamFlags, &RawParamName, &RawDefaultArg);

		bool bPassed = true;
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("ScriptFunction metadata should read parameter %u successfully"), static_cast<uint32>(Expected.Index)),
			Result,
			static_cast<int32>(asSUCCESS));
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("ScriptFunction metadata should preserve parameter %u type"), static_cast<uint32>(Expected.Index)),
			ParamTypeId,
			Expected.TypeId);
		if (Expected.Flags == MAX_uint32)
		{
			const asDWORD DirectionFlags = ParamFlags & asTM_INOUTREF;
			bPassed &= Test.TestTrue(
				*FString::Printf(TEXT("ScriptFunction metadata should keep parameter %u as an input-only parameter"), static_cast<uint32>(Expected.Index)),
				DirectionFlags != asTM_OUTREF && DirectionFlags != asTM_INOUTREF);
		}
		else
		{
			bPassed &= Test.TestEqual(
				*FString::Printf(TEXT("ScriptFunction metadata should preserve parameter %u flags"), static_cast<uint32>(Expected.Index)),
				static_cast<uint32>(ParamFlags),
				static_cast<uint32>(Expected.Flags));
		}
		bPassed &= Test.TestEqual(
			*FString::Printf(TEXT("ScriptFunction metadata should preserve parameter %u name"), static_cast<uint32>(Expected.Index)),
			ToFString(RawParamName),
			FString(Expected.Name));

		if (Expected.DefaultArg == nullptr)
		{
			bPassed &= Test.TestTrue(
				*FString::Printf(TEXT("ScriptFunction metadata should keep parameter %u without a default argument"), static_cast<uint32>(Expected.Index)),
				RawDefaultArg == nullptr);
		}
		else
		{
			bPassed &= Test.TestNotNull(
				*FString::Printf(TEXT("ScriptFunction metadata should expose a default argument for parameter %u"), static_cast<uint32>(Expected.Index)),
				RawDefaultArg);
			if (RawDefaultArg != nullptr)
			{
				bPassed &= Test.TestEqual(
					*FString::Printf(TEXT("ScriptFunction metadata should preserve parameter %u default argument text"), static_cast<uint32>(Expected.Index)),
					ToFString(RawDefaultArg),
					FString(Expected.DefaultArg));
			}
		}

		return bPassed;
	}

	void CollectLocalVariableMetadata(
		asIScriptFunction& Function,
		TArray<FString>& OutVarNames,
		TArray<FString>& OutVarDeclarations)
	{
		OutVarNames.Reset();
		OutVarDeclarations.Reset();

		for (asUINT VarIndex = 0; VarIndex < Function.GetVarCount(); ++VarIndex)
		{
			const char* RawVarName = nullptr;
			Function.GetVar(VarIndex, &RawVarName, nullptr);
			OutVarNames.Add(ToFString(RawVarName));
			OutVarDeclarations.Add(ToFString(Function.GetVarDecl(VarIndex, false)));
		}
	}
}

using namespace AngelscriptTest_Internals_AngelscriptScriptFunctionTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScriptFunctionSignatureMetadataTest,
	"Angelscript.TestModule.Internals.ScriptFunction.SignatureMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptScriptFunctionDebugMetadataTest,
	"Angelscript.TestModule.Internals.ScriptFunction.DebugMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptScriptFunctionSignatureMetadataTest::RunTest(const FString& Parameters)
{
	static const FName ModuleName(TEXT("Tests.Internals.ScriptFunction.SignatureMetadata"));
	static const FString ScriptFilename(TEXT("Tests/Internals/ScriptFunctionSignatureMetadata.as"));

	const FString ScriptSource = TEXT(R"AS(
int EvaluateNumbers(int Required, int OptionalMultiplier = 2, int OptionalBias = 5)
{
	int Seed = Required + 1;
	int Intermediate = Seed * OptionalMultiplier;
	int FinalValue = Intermediate + OptionalBias;
	return FinalValue;
}

int Entry()
{
	return EvaluateNumbers(16);
}
)AS");

	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
	};

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		ScriptFilename,
		ScriptSource,
		false,
		Summary);

	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should compile successfully"),
		bCompiled);
	bPassed &= TestFalse(
		TEXT("ScriptFunction.SignatureMetadata should stay on the non-preprocessor path"),
		Summary.bUsedPreprocessor);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep diagnostics empty"),
		Summary.Diagnostics.IsEmpty());
	if (!bCompiled)
	{
		return false;
	}

	const auto ModuleDesc = Engine.GetModuleByModuleName(ModuleName.ToString());
	if (!TestTrue(
			TEXT("ScriptFunction.SignatureMetadata should register the compiled module"),
			ModuleDesc.IsValid()))
	{
		return false;
	}
	if (!TestNotNull(
			TEXT("ScriptFunction.SignatureMetadata should expose the compiled script module"),
			ModuleDesc->ScriptModule))
	{
		return false;
	}

	int32 EntryResult = 0;
	const bool bExecuted = ExecuteIntFunction(&Engine, ModuleName, TEXT("int Entry()"), EntryResult);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should execute Entry successfully"),
		bExecuted);
	if (bExecuted)
	{
		bPassed &= TestEqual(
			TEXT("ScriptFunction.SignatureMetadata should keep default-argument execution semantics intact"),
			EntryResult,
			39);
	}

	asCScriptFunction* Function = static_cast<asCScriptFunction*>(
		GetFunctionByDecl(*this, *ModuleDesc->ScriptModule, TEXT("int EvaluateNumbers(int, int, int)")));
	if (!TestNotNull(
			TEXT("ScriptFunction.SignatureMetadata should resolve EvaluateNumbers(int, int, int)"),
			Function))
	{
		return false;
	}

	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should keep the function name"),
		ToFString(Function->GetName()),
		FString(TEXT("EvaluateNumbers")));
	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should keep the parameter count"),
		static_cast<int32>(Function->GetParamCount()),
		3);

	const FString DeclarationWithNames = ToFString(Function->GetDeclaration(false, false, true, false));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep the declaration name"),
		DeclarationWithNames.Contains(TEXT("EvaluateNumbers")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep parameter names in the declaration"),
		DeclarationWithNames.Contains(TEXT("Required")) &&
			DeclarationWithNames.Contains(TEXT("OptionalMultiplier")) &&
			DeclarationWithNames.Contains(TEXT("OptionalBias")));

	const asCString DeclarationWithDefaultsAnsi = Function->GetDeclarationStr(false, false, true, false, true);
	const FString DeclarationWithDefaults = UTF8_TO_TCHAR(DeclarationWithDefaultsAnsi.AddressOf());
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep the first default argument in the internal declaration string"),
		DeclarationWithDefaults.Contains(TEXT("= 2")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep the second default argument in the internal declaration string"),
		DeclarationWithDefaults.Contains(TEXT("= 5")));

	asDWORD ReturnFlags = 0;
	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should report int as the return type"),
		Function->GetReturnTypeId(&ReturnFlags),
		static_cast<int32>(asTYPEID_INT32));
	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should keep a plain-value return type without flags"),
		static_cast<uint32>(ReturnFlags),
		static_cast<uint32>(0));

	bPassed &= VerifyParamMetadata(
		*this,
		*Function,
		{0, asTYPEID_INT32, MAX_uint32, TEXT("Required"), nullptr});
	bPassed &= VerifyParamMetadata(
		*this,
		*Function,
		{1, asTYPEID_INT32, MAX_uint32, TEXT("OptionalMultiplier"), TEXT("2")});
	bPassed &= VerifyParamMetadata(
		*this,
		*Function,
		{2, asTYPEID_INT32, MAX_uint32, TEXT("OptionalBias"), TEXT("5")});

	TArray<FString> VarNames;
	TArray<FString> VarDeclarations;
	CollectLocalVariableMetadata(*Function, VarNames, VarDeclarations);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should keep parameter and local variable metadata"),
		VarDeclarations.Num(),
		6);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the Required parameter declaration through variable metadata"),
		DeclarationListContains(VarDeclarations, TEXT("Required")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the OptionalMultiplier parameter declaration through variable metadata"),
		DeclarationListContains(VarDeclarations, TEXT("OptionalMultiplier")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the OptionalBias parameter declaration through variable metadata"),
		DeclarationListContains(VarDeclarations, TEXT("OptionalBias")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the Seed local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("Seed")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the Intermediate local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("Intermediate")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should expose the FinalValue local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("FinalValue")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should keep parameter names alongside local variable names in debug metadata"),
		VarNames.Contains(TEXT("Required")) && VarNames.Contains(TEXT("OptionalMultiplier")) && VarNames.Contains(TEXT("OptionalBias")));

	const FString ScriptSectionName = FPaths::GetCleanFilename(ToFString(Function->GetScriptSectionName()));
	bPassed &= TestEqual(
		TEXT("ScriptFunction.SignatureMetadata should keep the script section filename"),
		ScriptSectionName,
		FString(TEXT("ScriptFunctionSignatureMetadata.as")));

	asUINT BytecodeLength = 0;
	asDWORD* Bytecode = Function->GetByteCode(&BytecodeLength);
	bPassed &= TestNotNull(
		TEXT("ScriptFunction.SignatureMetadata should expose a bytecode buffer"),
		Bytecode);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.SignatureMetadata should emit at least one bytecode instruction"),
		BytecodeLength > 0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

bool FAngelscriptScriptFunctionDebugMetadataTest::RunTest(const FString& Parameters)
{
	static const FName ModuleName(TEXT("Tests.Internals.ScriptFunction.DebugMetadata"));
	static const FString ScriptFilename(TEXT("Tests/Internals/ScriptFunctionDebugMetadata.as"));

	const FString ScriptSource = TEXT(R"AS(
int ScanLines(bool bTakeBranch)
{
	int Start = 1;

	// Intentional gap to force FindNextLineWithCode to skip blank/comment lines.
	if (bTakeBranch)
	{
		int BranchValue = Start + 4;
		return BranchValue;
	}

	int TailValue = Start * 3;
	return TailValue;
}
)AS");

	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN

	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(*ModuleName.ToString());
	};

	FAngelscriptCompileTraceSummary Summary;
	const bool bCompiled = CompileModuleWithSummary(
		&Engine,
		ECompileType::SoftReloadOnly,
		ModuleName,
		ScriptFilename,
		ScriptSource,
		false,
		Summary);

	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should compile successfully"),
		bCompiled);
	bPassed &= TestFalse(
		TEXT("ScriptFunction.DebugMetadata should stay on the non-preprocessor path"),
		Summary.bUsedPreprocessor);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should keep diagnostics empty"),
		Summary.Diagnostics.IsEmpty());
	if (!bCompiled)
	{
		return false;
	}

	const auto ModuleDesc = Engine.GetModuleByModuleName(ModuleName.ToString());
	if (!TestTrue(
			TEXT("ScriptFunction.DebugMetadata should register the compiled module"),
			ModuleDesc.IsValid()))
	{
		return false;
	}
	if (!TestNotNull(
			TEXT("ScriptFunction.DebugMetadata should expose the compiled script module"),
			ModuleDesc->ScriptModule))
	{
		return false;
	}

	asCScriptFunction* Function = static_cast<asCScriptFunction*>(
		GetFunctionByDecl(*this, *ModuleDesc->ScriptModule, TEXT("int ScanLines(bool)")));
	if (!TestNotNull(
			TEXT("ScriptFunction.DebugMetadata should resolve ScanLines(bool)"),
			Function))
	{
		return false;
	}

	const FString ScriptSectionName = FPaths::GetCleanFilename(ToFString(Function->GetScriptSectionName()));
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should keep the script section filename"),
		ScriptSectionName,
		FString(TEXT("ScriptFunctionDebugMetadata.as")));

	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should reject searches before the declaration line"),
		Function->FindNextLineWithCode(1),
		-1);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should advance from the declaration line to the first executable statement"),
		Function->FindNextLineWithCode(2),
		4);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should keep the branch condition line as executable code"),
		Function->FindNextLineWithCode(7),
		7);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should resume at the tail assignment after the branch block"),
		Function->FindNextLineWithCode(11),
		13);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should report no executable line after the function body ends"),
		Function->FindNextLineWithCode(15),
		-1);

	TArray<FString> VarNames;
	TArray<FString> VarDeclarations;
	CollectLocalVariableMetadata(*Function, VarNames, VarDeclarations);
	bPassed &= TestEqual(
		TEXT("ScriptFunction.DebugMetadata should keep parameter and local variable metadata"),
		VarDeclarations.Num(),
		4);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should expose the boolean parameter declaration through variable metadata"),
		DeclarationListContains(VarDeclarations, TEXT("bTakeBranch")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should expose the Start local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("Start")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should expose the BranchValue local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("BranchValue")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should expose the TailValue local declaration"),
		DeclarationListContains(VarDeclarations, TEXT("TailValue")));
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should keep the boolean parameter name in variable metadata"),
		VarNames.Contains(TEXT("bTakeBranch")));

	asUINT BytecodeLength = 0;
	asDWORD* Bytecode = Function->GetByteCode(&BytecodeLength);
	bPassed &= TestNotNull(
		TEXT("ScriptFunction.DebugMetadata should expose a bytecode buffer"),
		Bytecode);
	bPassed &= TestTrue(
		TEXT("ScriptFunction.DebugMetadata should emit bytecode for line-map inspection"),
		BytecodeLength > 0);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
