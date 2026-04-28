#include "Misc/AutomationTest.h"
#include "StaticJIT/AngelscriptStaticJIT.h"
#include "StaticJIT/StaticJITConfig.h"
#include "StaticJIT/StaticJITHeader.h"

#if WITH_DEV_AUTOMATION_TESTS && AS_CAN_GENERATE_JIT

namespace AngelscriptStaticJITDatabaseTests_Private
{
	static constexpr uint32 RegisteredFunctionId = 0x13572468u;
	static constexpr uint64 FunctionLookupToken = 0x1111222233334444ull;
	static constexpr uint64 SystemFunctionLookupToken = 0x5555666677778888ull;
	static constexpr uint64 GlobalVarLookupToken = 0x9999AAAABBBBCCCCull;
	static constexpr uint64 TypeLookupToken = 0xDDDDEEEEFFFF0001ull;
	static constexpr uint64 PropertyLookupToken = 0x123456789ABCDEF0ull;
	static constexpr uint32 PropertyOffsetSentinel = 0xFFFFu;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
	static constexpr uint64 VerifyPropertyLookupToken = 0x02468ACE13579BDFull;
	static constexpr SIZE_T VerifyPropertyOffsetValue = 96;
	static constexpr uint64 VerifyTypeLookupToken = 0x0F1E2D3C4B5A6978ull;
	static constexpr SIZE_T VerifyTypeSizeValue = 128;
	static constexpr SIZE_T VerifyTypeAlignmentValue = 16;
#endif

	static void StaticJITVMEntryStub(FScriptExecution& Execution, asDWORD* l_fp, asQWORD* l_outValue)
	{
		(void)Execution;
		(void)l_fp;
		(void)l_outValue;
	}

	static void StaticJITParmsEntryStub(FScriptExecution& Execution, void* Object, void* Parms)
	{
		(void)Execution;
		(void)Object;
		(void)Parms;
	}

	static void StaticJITRawStub()
	{
	}

	struct FScopedJITDatabaseSnapshot
	{
		FScopedJITDatabaseSnapshot()
		{
			Save(FJITDatabase::Get());
		}

		~FScopedJITDatabaseSnapshot()
		{
			Restore(FJITDatabase::Get());
		}

	private:
		void Save(const FJITDatabase& Database)
		{
			SavedFunctions = Database.Functions;
			SavedFunctionLookups = Database.FunctionLookups;
			SavedSystemFunctionPointerLookups = Database.SystemFunctionPointerLookups;
			SavedGlobalVarLookups = Database.GlobalVarLookups;
			SavedTypeInfoLookups = Database.TypeInfoLookups;
			SavedPropertyOffsetLookups = Database.PropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			SavedVerifyPropertyOffsets = Database.VerifyPropertyOffsets;
			SavedVerifyTypeSizes = Database.VerifyTypeSizes;
			SavedVerifyTypeAlignments = Database.VerifyTypeAlignments;
#endif
		}

		void Restore(FJITDatabase& Database) const
		{
			Database.Functions = SavedFunctions;
			Database.FunctionLookups = SavedFunctionLookups;
			Database.SystemFunctionPointerLookups = SavedSystemFunctionPointerLookups;
			Database.GlobalVarLookups = SavedGlobalVarLookups;
			Database.TypeInfoLookups = SavedTypeInfoLookups;
			Database.PropertyOffsetLookups = SavedPropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			Database.VerifyPropertyOffsets = SavedVerifyPropertyOffsets;
			Database.VerifyTypeSizes = SavedVerifyTypeSizes;
			Database.VerifyTypeAlignments = SavedVerifyTypeAlignments;
#endif
		}

		TMap<uint32, FJITDatabase::FJITFunctions> SavedFunctions;
		TArray<void**> SavedFunctionLookups;
		TArray<void*> SavedSystemFunctionPointerLookups;
		TArray<void**> SavedGlobalVarLookups;
		TArray<void**> SavedTypeInfoLookups;
		TArray<TPair<uint64, uint32*>> SavedPropertyOffsetLookups;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
		TArray<TPair<uint64, SIZE_T>> SavedVerifyPropertyOffsets;
		TArray<TPair<uint64, SIZE_T>> SavedVerifyTypeSizes;
		TArray<TPair<uint64, SIZE_T>> SavedVerifyTypeAlignments;
#endif
	};

	struct FSeededJITDatabaseState
	{
		void* FunctionLookupValue = reinterpret_cast<void*>(FunctionLookupToken);
		void* SystemFunctionPointerValue = reinterpret_cast<void*>(SystemFunctionLookupToken);
		void* GlobalVarLookupValue = reinterpret_cast<void*>(GlobalVarLookupToken);
		void* TypeInfoLookupValue = reinterpret_cast<void*>(TypeLookupToken);
		uint32 PropertyOffsetValue = 77u;

		void Seed(FJITDatabase& Database)
		{
			FJITDatabase::FJITFunctions SeededFunctions;
			SeededFunctions.VMEntry = &StaticJITVMEntryStub;
			SeededFunctions.ParmsEntry = &StaticJITParmsEntryStub;
			SeededFunctions.RawFunction = &StaticJITRawStub;

			Database.Functions.Add(RegisteredFunctionId, SeededFunctions);
			Database.FunctionLookups.Add(&FunctionLookupValue);
			Database.SystemFunctionPointerLookups.Add(SystemFunctionPointerValue);
			Database.GlobalVarLookups.Add(&GlobalVarLookupValue);
			Database.TypeInfoLookups.Add(&TypeInfoLookupValue);
			Database.PropertyOffsetLookups.Add(TPair<uint64, uint32*>(PropertyLookupToken, &PropertyOffsetValue));

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			Database.VerifyPropertyOffsets.Add(TPair<uint64, SIZE_T>(VerifyPropertyLookupToken, VerifyPropertyOffsetValue));
			Database.VerifyTypeSizes.Add(TPair<uint64, SIZE_T>(VerifyTypeLookupToken, VerifyTypeSizeValue));
			Database.VerifyTypeAlignments.Add(TPair<uint64, SIZE_T>(VerifyTypeLookupToken, VerifyTypeAlignmentValue));
#endif
		}

		bool AssertMatches(FAutomationTestBase& Test, const FJITDatabase& Database, const TCHAR* Context) const
		{
			const FJITDatabase::FJITFunctions* StoredFunctions = Database.Functions.Find(RegisteredFunctionId);
			if (!Test.TestEqual(*FString::Printf(TEXT("%s should keep the registered function count"), Context), Database.Functions.Num(), 1))
			{
				return false;
			}

			if (!Test.TestNotNull(*FString::Printf(TEXT("%s should preserve the registered function entry"), Context), StoredFunctions))
			{
				return false;
			}

			if (!Test.TestTrue(*FString::Printf(TEXT("%s should preserve the registered VM entry stub"), Context), StoredFunctions->VMEntry == &StaticJITVMEntryStub)
				|| !Test.TestTrue(*FString::Printf(TEXT("%s should preserve the registered parms entry stub"), Context), StoredFunctions->ParmsEntry == &StaticJITParmsEntryStub)
				|| !Test.TestTrue(*FString::Printf(TEXT("%s should preserve the registered raw entry stub"), Context), StoredFunctions->RawFunction == &StaticJITRawStub))
			{
				return false;
			}

			if (!Test.TestEqual(*FString::Printf(TEXT("%s should keep the function-lookup count"), Context), Database.FunctionLookups.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the system-function lookup count"), Context), Database.SystemFunctionPointerLookups.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the global-variable lookup count"), Context), Database.GlobalVarLookups.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the type-info lookup count"), Context), Database.TypeInfoLookups.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the property-offset lookup count"), Context), Database.PropertyOffsetLookups.Num(), 1))
			{
				return false;
			}

			if (!Test.TestTrue(*FString::Printf(TEXT("%s should keep the function lookup token"), Context), *Database.FunctionLookups[0] == FunctionLookupValue)
				|| !Test.TestTrue(*FString::Printf(TEXT("%s should keep the system-function lookup token"), Context), Database.SystemFunctionPointerLookups[0] == SystemFunctionPointerValue)
				|| !Test.TestTrue(*FString::Printf(TEXT("%s should keep the global-variable lookup token"), Context), *Database.GlobalVarLookups[0] == GlobalVarLookupValue)
				|| !Test.TestTrue(*FString::Printf(TEXT("%s should keep the type-info lookup token"), Context), *Database.TypeInfoLookups[0] == TypeInfoLookupValue)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the property-offset lookup token"), Context), Database.PropertyOffsetLookups[0].Key, PropertyLookupToken)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the property-offset value"), Context), *Database.PropertyOffsetLookups[0].Value, PropertyOffsetValue))
			{
				return false;
			}

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
			if (!Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-property lookup count"), Context), Database.VerifyPropertyOffsets.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-type-size lookup count"), Context), Database.VerifyTypeSizes.Num(), 1)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-type-alignment lookup count"), Context), Database.VerifyTypeAlignments.Num(), 1))
			{
				return false;
			}

			if (!Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-property token"), Context), Database.VerifyPropertyOffsets[0].Key, VerifyPropertyLookupToken)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-property offset"), Context), Database.VerifyPropertyOffsets[0].Value, VerifyPropertyOffsetValue)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-type token"), Context), Database.VerifyTypeSizes[0].Key, VerifyTypeLookupToken)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-type size"), Context), Database.VerifyTypeSizes[0].Value, VerifyTypeSizeValue)
				|| !Test.TestEqual(*FString::Printf(TEXT("%s should keep the verify-type alignment"), Context), Database.VerifyTypeAlignments[0].Value, VerifyTypeAlignmentValue))
			{
				return false;
			}
#endif

			return true;
		}
	};

	static bool AssertDatabaseIsEmpty(FAutomationTestBase& Test, const FJITDatabase& Database, const TCHAR* Context)
	{
		bool bEmpty = Test.TestTrue(*FString::Printf(TEXT("%s should leave the function registry empty"), Context), Database.Functions.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the function-lookup registry empty"), Context), Database.FunctionLookups.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the system-function lookup registry empty"), Context), Database.SystemFunctionPointerLookups.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the global-variable lookup registry empty"), Context), Database.GlobalVarLookups.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the type-info lookup registry empty"), Context), Database.TypeInfoLookups.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the property-offset lookup registry empty"), Context), Database.PropertyOffsetLookups.IsEmpty());

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
		bEmpty = bEmpty
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the verify-property registry empty"), Context), Database.VerifyPropertyOffsets.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the verify-type-size registry empty"), Context), Database.VerifyTypeSizes.IsEmpty())
			&& Test.TestTrue(*FString::Printf(TEXT("%s should leave the verify-type-alignment registry empty"), Context), Database.VerifyTypeAlignments.IsEmpty());
#endif

		return bEmpty;
	}
}

using namespace AngelscriptStaticJITDatabaseTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITDatabaseRegistrarsPopulateAndClearTest,
	"Angelscript.CppTests.StaticJIT.Database.RegistrarsPopulateAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITDatabaseSnapshotRestoresPriorStateTest,
	"Angelscript.CppTests.StaticJIT.Database.SnapshotRestoresPriorState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStaticJITDatabaseRegistrarsPopulateAndClearTest::RunTest(const FString& Parameters)
{
	FScopedJITDatabaseSnapshot DatabaseSnapshot;

	FJITDatabase& Database = FJITDatabase::Get();
	if (!TestTrue(TEXT("FJITDatabase::Get should return a stable singleton"), &Database == &FJITDatabase::Get()))
	{
		return false;
	}

	FJITDatabase::Clear();
	if (!AssertDatabaseIsEmpty(*this, Database, TEXT("Clearing the JIT database before registrar population")))
	{
		return false;
	}

	FStaticJITFunction RegisteredFunction(RegisteredFunctionId, &StaticJITVMEntryStub, &StaticJITParmsEntryStub, &StaticJITRawStub);
	FJitRef_Function FunctionLookup(FunctionLookupToken);
	FJitRef_SystemFunctionPointer SystemFunctionLookup(SystemFunctionLookupToken);
	FJitRef_GlobalVar GlobalVarLookup(GlobalVarLookupToken);
	FJitRef_Type TypeLookup(TypeLookupToken);
	FJitRef_PropertyOffset PropertyOffsetLookup(PropertyLookupToken);
	(void)RegisteredFunction;
	(void)GlobalVarLookup;
	(void)TypeLookup;

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
	FJitVerifyPropertyOffset VerifyPropertyOffset(VerifyPropertyLookupToken, VerifyPropertyOffsetValue);
	FJitVerifyTypeSize VerifyTypeSize(VerifyTypeLookupToken, VerifyTypeSizeValue, VerifyTypeAlignmentValue);
	(void)VerifyPropertyOffset;
	(void)VerifyTypeSize;
#endif

	const FJITDatabase::FJITFunctions* StoredFunctions = Database.Functions.Find(RegisteredFunctionId);
	if (!TestNotNull(TEXT("Registrar population should store the registered function entry"), StoredFunctions))
	{
		return false;
	}

	if (!TestTrue(TEXT("Registrar population should keep the registered VM entry stub"), StoredFunctions->VMEntry == &StaticJITVMEntryStub)
		|| !TestTrue(TEXT("Registrar population should keep the registered parms entry stub"), StoredFunctions->ParmsEntry == &StaticJITParmsEntryStub)
		|| !TestTrue(TEXT("Registrar population should keep the registered raw entry stub"), StoredFunctions->RawFunction == &StaticJITRawStub))
	{
		return false;
	}

	if (!TestEqual(TEXT("Registrar population should add one function lookup"), Database.FunctionLookups.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one system-function lookup"), Database.SystemFunctionPointerLookups.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one global-variable lookup"), Database.GlobalVarLookups.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one type-info lookup"), Database.TypeInfoLookups.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one property-offset lookup"), Database.PropertyOffsetLookups.Num(), 1))
	{
		return false;
	}

	if (!TestTrue(TEXT("Registrar population should preserve the function lookup token"), *Database.FunctionLookups[0] == reinterpret_cast<void*>(FunctionLookupToken))
		|| !TestTrue(TEXT("Registrar population should preserve the function lookup address"), Database.FunctionLookups[0] == &FunctionLookup.Pointer)
		|| !TestTrue(TEXT("Registrar population should preserve the system-function lookup address"), Database.SystemFunctionPointerLookups[0] == &SystemFunctionLookup)
		|| !TestTrue(TEXT("Registrar population should preserve the global-variable lookup token"), *Database.GlobalVarLookups[0] == reinterpret_cast<void*>(GlobalVarLookupToken))
		|| !TestTrue(TEXT("Registrar population should preserve the type-info lookup token"), *Database.TypeInfoLookups[0] == reinterpret_cast<void*>(TypeLookupToken))
		|| !TestEqual(TEXT("Registrar population should preserve the property-offset lookup token"), Database.PropertyOffsetLookups[0].Key, PropertyLookupToken)
		|| !TestEqual(TEXT("Registrar population should preserve the property-offset sentinel"), PropertyOffsetLookup.Get(), PropertyOffsetSentinel)
		|| !TestEqual(TEXT("Registrar population should preserve the property-offset pointer value"), *Database.PropertyOffsetLookups[0].Value, PropertyOffsetSentinel))
	{
		return false;
	}

#if AS_JIT_VERIFY_PROPERTY_OFFSETS
	if (!TestEqual(TEXT("Registrar population should add one verify-property entry"), Database.VerifyPropertyOffsets.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one verify-type-size entry"), Database.VerifyTypeSizes.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should add one verify-type-alignment entry"), Database.VerifyTypeAlignments.Num(), 1)
		|| !TestEqual(TEXT("Registrar population should preserve the verify-property token"), Database.VerifyPropertyOffsets[0].Key, VerifyPropertyLookupToken)
		|| !TestEqual(TEXT("Registrar population should preserve the verify-property offset"), Database.VerifyPropertyOffsets[0].Value, VerifyPropertyOffsetValue)
		|| !TestEqual(TEXT("Registrar population should preserve the verify-type token"), Database.VerifyTypeSizes[0].Key, VerifyTypeLookupToken)
		|| !TestEqual(TEXT("Registrar population should preserve the verify-type size"), Database.VerifyTypeSizes[0].Value, VerifyTypeSizeValue)
		|| !TestEqual(TEXT("Registrar population should preserve the verify-type alignment"), Database.VerifyTypeAlignments[0].Value, VerifyTypeAlignmentValue))
	{
		return false;
	}
#endif

	FJITDatabase::Clear();
	return AssertDatabaseIsEmpty(*this, Database, TEXT("Clearing the JIT database after registrar population"));
}

bool FAngelscriptStaticJITDatabaseSnapshotRestoresPriorStateTest::RunTest(const FString& Parameters)
{
	FScopedJITDatabaseSnapshot OriginalDatabaseSnapshot;

	FJITDatabase& Database = FJITDatabase::Get();
	FJITDatabase::Clear();

	FSeededJITDatabaseState BaselineState;
	BaselineState.Seed(Database);
	if (!BaselineState.AssertMatches(*this, Database, TEXT("Baseline seeded state")))
	{
		return false;
	}

	{
		FScopedJITDatabaseSnapshot BaselineSnapshot;

		FJITDatabase::Clear();
		FStaticJITFunction MutatedFunction(RegisteredFunctionId + 1u, &StaticJITVMEntryStub, &StaticJITParmsEntryStub, &StaticJITRawStub);
		FJitRef_Function MutatedFunctionLookup(FunctionLookupToken + 1u);
		FJitRef_SystemFunctionPointer MutatedSystemFunctionLookup(SystemFunctionLookupToken + 1u);
		FJitRef_GlobalVar MutatedGlobalVarLookup(GlobalVarLookupToken + 1u);
		FJitRef_Type MutatedTypeLookup(TypeLookupToken + 1u);
		FJitRef_PropertyOffset MutatedPropertyOffsetLookup(PropertyLookupToken + 1u);
		(void)MutatedFunction;
		(void)MutatedFunctionLookup;
		(void)MutatedSystemFunctionLookup;
		(void)MutatedGlobalVarLookup;
		(void)MutatedTypeLookup;
		(void)MutatedPropertyOffsetLookup;

		if (!TestEqual(TEXT("Snapshot restore test should observe the mutated function count inside the scope"), Database.Functions.Num(), 1)
			|| !TestTrue(TEXT("Snapshot restore test should observe the mutated function id inside the scope"), Database.Functions.Contains(RegisteredFunctionId + 1u)))
		{
			return false;
		}
	}

	return BaselineState.AssertMatches(*this, Database, TEXT("Scoped snapshot restore"));
}

#endif
