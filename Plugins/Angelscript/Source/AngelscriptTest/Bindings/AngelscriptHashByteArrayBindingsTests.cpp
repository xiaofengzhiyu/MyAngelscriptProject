#include "../Shared/AngelscriptTestEngineHelper.h"
#include "../Shared/AngelscriptTestMacros.h"

#include "Hash/CityHash.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace AngelscriptTestSupport;

namespace AngelscriptTest_Bindings_AngelscriptHashByteArrayBindingsTests_Private
{
	TArray<int8> MakeByteArray(std::initializer_list<int32> Values)
	{
		TArray<int8> Bytes;
		Bytes.Reserve(static_cast<int32>(Values.size()));
		for (const int32 Value : Values)
		{
			Bytes.Add(static_cast<int8>(Value));
		}

		return Bytes;
	}

	uint32 Hash32(const TArray<int8>& Bytes)
	{
		return CityHash32(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()));
	}

	uint64 Hash64(const TArray<int8>& Bytes)
	{
		return CityHash64(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()));
	}

	uint64 Hash64WithSeedValue(const TArray<int8>& Bytes, const uint64 Seed)
	{
		return CityHash64WithSeed(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()), Seed);
	}

	uint64 Hash64WithSeedsValue(const TArray<int8>& Bytes, const uint64 Seed0, const uint64 Seed1)
	{
		return CityHash64WithSeeds(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<uint32>(Bytes.Num()), Seed0, Seed1);
	}

	FString FormatScriptUInt32Literal(const uint32 Value)
	{
		return FString::Printf(TEXT("uint32(%u)"), Value);
	}

	FString FormatScriptUInt64Literal(const uint64 Value)
	{
		return FString::Printf(TEXT("uint64(%llu)"), static_cast<unsigned long long>(Value));
	}
}

using namespace AngelscriptTest_Bindings_AngelscriptHashByteArrayBindingsTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptHashByteArrayBindingsTest,
	"Angelscript.TestModule.Bindings.Hash.ByteArrayCompat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptHashByteArrayBindingsTest::RunTest(const FString& Parameters)
{
	bool bPassed = true;
	FAngelscriptEngine& Engine = ASTEST_CREATE_ENGINE_SHARE_CLEAN();
	ASTEST_BEGIN_SHARE_CLEAN
	ON_SCOPE_EXIT
	{
		Engine.DiscardModule(TEXT("ASHashByteArrayCompat"));
	};

	const TArray<int8> PrimaryBytes = MakeByteArray({ 65, 0, -1, 127, -128, 42 });
	const TArray<int8> SecondaryBytes = MakeByteArray({ 65, 0, -2, 127, -128, 42 });
	const uint64 SeedA = 1234567890123456789ull;
	const uint64 SeedB = 9876543210987654321ull;

	const uint32 ExpectedPrimaryHash32 = Hash32(PrimaryBytes);
	const uint32 ExpectedSecondaryHash32 = Hash32(SecondaryBytes);
	const uint64 ExpectedPrimaryHash64 = Hash64(PrimaryBytes);
	const uint64 ExpectedSecondaryHash64 = Hash64(SecondaryBytes);
	const uint64 ExpectedPrimaryHash64Seed = Hash64WithSeedValue(PrimaryBytes, SeedA);
	const uint64 ExpectedSecondaryHash64Seed = Hash64WithSeedValue(SecondaryBytes, SeedA);
	const uint64 ExpectedPrimaryHash64Seeds = Hash64WithSeedsValue(PrimaryBytes, SeedA, SeedB);
	const uint64 ExpectedSecondaryHash64Seeds = Hash64WithSeedsValue(SecondaryBytes, SeedA, SeedB);

	bPassed &= TestNotEqual(TEXT("Native CityHash32 baseline should distinguish the two byte buffers"), ExpectedPrimaryHash32, ExpectedSecondaryHash32);
	bPassed &= TestNotEqual(TEXT("Native CityHash64 baseline should distinguish the two byte buffers"), ExpectedPrimaryHash64, ExpectedSecondaryHash64);
	bPassed &= TestNotEqual(TEXT("Native CityHash64WithSeed baseline should distinguish the two byte buffers"), ExpectedPrimaryHash64Seed, ExpectedSecondaryHash64Seed);
	bPassed &= TestNotEqual(TEXT("Native CityHash64WithSeeds baseline should distinguish the two byte buffers"), ExpectedPrimaryHash64Seeds, ExpectedSecondaryHash64Seeds);
	if (!bPassed)
	{
		return false;
	}

	FString Script = TEXT(R"(
TArray<int8> BuildPrimaryBytes()
{
	TArray<int8> Bytes;
	Bytes.Add(int8(65));
	Bytes.Add(int8(0));
	Bytes.Add(int8(-1));
	Bytes.Add(int8(127));
	Bytes.Add(int8(-128));
	Bytes.Add(int8(42));
	return Bytes;
}

TArray<int8> BuildSecondaryBytes()
{
	TArray<int8> Bytes;
	Bytes.Add(int8(65));
	Bytes.Add(int8(0));
	Bytes.Add(int8(-2));
	Bytes.Add(int8(127));
	Bytes.Add(int8(-128));
	Bytes.Add(int8(42));
	return Bytes;
}

int Entry()
{
	const TArray<int8> Primary = BuildPrimaryBytes();
	const TArray<int8> Secondary = BuildSecondaryBytes();

	if (Hash::CityHash32(Primary) != $EXPECTED_PRIMARY_HASH32$)
		return 10;
	if (Hash::CityHash32(Secondary) != $EXPECTED_SECONDARY_HASH32$)
		return 20;
	if (Hash::CityHash64(Primary) != $EXPECTED_PRIMARY_HASH64$)
		return 30;
	if (Hash::CityHash64(Secondary) != $EXPECTED_SECONDARY_HASH64$)
		return 40;
	if (Hash::CityHash64WithSeed(Primary, $SEED_A$) != $EXPECTED_PRIMARY_HASH64_SEED$)
		return 50;
	if (Hash::CityHash64WithSeed(Secondary, $SEED_A$) != $EXPECTED_SECONDARY_HASH64_SEED$)
		return 60;
	if (Hash::CityHash64WithSeeds(Primary, $SEED_A$, $SEED_B$) != $EXPECTED_PRIMARY_HASH64_SEEDS$)
		return 70;
	if (Hash::CityHash64WithSeeds(Secondary, $SEED_A$, $SEED_B$) != $EXPECTED_SECONDARY_HASH64_SEEDS$)
		return 80;

	if (Hash::CityHash32(Primary) == Hash::CityHash32(Secondary))
		return 90;
	if (Hash::CityHash64(Primary) == Hash::CityHash64(Secondary))
		return 100;
	if (Hash::CityHash64WithSeed(Primary, $SEED_A$) == Hash::CityHash64WithSeed(Secondary, $SEED_A$))
		return 110;
	if (Hash::CityHash64WithSeeds(Primary, $SEED_A$, $SEED_B$) == Hash::CityHash64WithSeeds(Secondary, $SEED_A$, $SEED_B$))
		return 120;

	return 1;
}
)");

	Script.ReplaceInline(TEXT("$EXPECTED_PRIMARY_HASH32$"), *FormatScriptUInt32Literal(ExpectedPrimaryHash32));
	Script.ReplaceInline(TEXT("$EXPECTED_SECONDARY_HASH32$"), *FormatScriptUInt32Literal(ExpectedSecondaryHash32));
	Script.ReplaceInline(TEXT("$EXPECTED_PRIMARY_HASH64$"), *FormatScriptUInt64Literal(ExpectedPrimaryHash64));
	Script.ReplaceInline(TEXT("$EXPECTED_SECONDARY_HASH64$"), *FormatScriptUInt64Literal(ExpectedSecondaryHash64));
	Script.ReplaceInline(TEXT("$EXPECTED_PRIMARY_HASH64_SEED$"), *FormatScriptUInt64Literal(ExpectedPrimaryHash64Seed));
	Script.ReplaceInline(TEXT("$EXPECTED_SECONDARY_HASH64_SEED$"), *FormatScriptUInt64Literal(ExpectedSecondaryHash64Seed));
	Script.ReplaceInline(TEXT("$EXPECTED_PRIMARY_HASH64_SEEDS$"), *FormatScriptUInt64Literal(ExpectedPrimaryHash64Seeds));
	Script.ReplaceInline(TEXT("$EXPECTED_SECONDARY_HASH64_SEEDS$"), *FormatScriptUInt64Literal(ExpectedSecondaryHash64Seeds));
	Script.ReplaceInline(TEXT("$SEED_A$"), *FormatScriptUInt64Literal(SeedA));
	Script.ReplaceInline(TEXT("$SEED_B$"), *FormatScriptUInt64Literal(SeedB));

	asIScriptModule* Module = BuildModule(*this, Engine, "ASHashByteArrayCompat", Script);
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

	bPassed &= TestEqual(
		TEXT("Hash byte-array overloads should match the native CityHash baselines"),
		Result,
		1);

	ASTEST_END_SHARE_CLEAN
	return bPassed;
}

#endif
