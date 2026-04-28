#include "Core/AngelscriptSerializationTestTypes.h"

#include "UnversionedPropertySerialization.h"

#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeExit.h"
#include "Serialization/ArchiveProxy.h"
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Core_AngelscriptSerializationTests_Private
{
	enum class ESerializationPath : uint8
	{
		Versioned,
		Unversioned,
	};

	class FAngelscriptSerializationTestArchive final : public FArchiveProxy
	{
	public:
		using FArchiveProxy::FArchiveProxy;

		virtual FArchive& operator<<(FName& Value) override
		{
			uint32 UnstableDisplayIndex = Value.GetDisplayIndex().ToUnstableInt();
			int32 Number = Value.GetNumber();
			InnerArchive << UnstableDisplayIndex << Number;

			if (IsLoading())
			{
				Value = FName::CreateFromDisplayId(FNameEntryId::FromUnstableInt(UnstableDisplayIndex), Number);
			}

			return *this;
		}
	};

	static const TCHAR* ToString(const ESerializationPath Path)
	{
		return Path == ESerializationPath::Unversioned ? TEXT("unversioned") : TEXT("versioned");
	}

	static bool IsUnversionedPropertySerializationEnabled()
	{
		bool bEnabled = false;
		return GConfig->GetBool(TEXT("Core.System"), TEXT("CanUseUnversionedPropertySerialization"), bEnabled, GEngineIni) && bEnabled;
	}

	static void DestroySerializationTestSchemas()
	{
		DestroyAngelscriptUnversionedSchema(FAngelscriptSerializationInnerValue::StaticStruct());
		DestroyAngelscriptUnversionedSchema(FAngelscriptSerializationNestedFixture::StaticStruct());
		DestroyAngelscriptUnversionedSchema(FAngelscriptSerializationVersionToleranceV1::StaticStruct());
		DestroyAngelscriptUnversionedSchema(FAngelscriptSerializationVersionToleranceV2::StaticStruct());
	}

	static FAngelscriptSerializationInnerValue MakeInnerValue(const int32 Count, const float Ratio, const TCHAR* Label, const TCHAR* Token)
	{
		FAngelscriptSerializationInnerValue Value;
		Value.Count = Count;
		Value.Ratio = Ratio;
		Value.Label = Label;
		Value.Token = Token;
		return Value;
	}

	static FAngelscriptSerializationNestedFixture MakeNestedFixture()
	{
		FAngelscriptSerializationNestedFixture Fixture;
		Fixture.Primary = MakeInnerValue(7, 3.25f, TEXT("Primary"), TEXT("PrimaryToken"));
		Fixture.Secondary = MakeInnerValue(-4, 0.5f, TEXT("Secondary"), TEXT("SecondaryToken"));
		Fixture.Children = {
			MakeInnerValue(1, 1.0f, TEXT("ChildOne"), TEXT("ChildOneToken")),
			MakeInnerValue(2, 2.5f, TEXT("ChildTwo"), TEXT("ChildTwoToken")),
			MakeInnerValue(3, -7.75f, TEXT("ChildThree"), TEXT("ChildThreeToken")),
		};
		return Fixture;
	}

	static FAngelscriptSerializationVersionToleranceV1 MakeVersionToleranceV1Fixture()
	{
		FAngelscriptSerializationVersionToleranceV1 Fixture;
		Fixture.Count = 17;
		Fixture.Title = TEXT("LegacyFixture");
		Fixture.Payload = MakeInnerValue(9, 4.5f, TEXT("LegacyPayload"), TEXT("LegacyPayloadToken"));
		Fixture.Status = TEXT("LegacyReady");
		return Fixture;
	}

	static FAngelscriptSerializationVersionToleranceV2 MakeVersionToleranceV2Fixture()
	{
		FAngelscriptSerializationVersionToleranceV2 Fixture;
		Fixture.Count = 29;
		Fixture.SchemaVersion = 77;
		Fixture.Title = TEXT("CurrentFixture");
		Fixture.Payload = MakeInnerValue(13, 9.75f, TEXT("CurrentPayload"), TEXT("CurrentPayloadToken"));
		Fixture.Status = TEXT("CurrentReady");
		Fixture.Score = 6.25f;
		return Fixture;
	}

	template <typename StructType>
	static void SaveStruct(
		const ESerializationPath Path,
		const StructType& Source,
		const StructType& Defaults,
		TArray<uint8>& OutBytes)
	{
		StructType MutableSource = Source;
		StructType MutableDefaults = Defaults;
		UScriptStruct* Struct = StructType::StaticStruct();

		OutBytes.Reset();

		FMemoryWriter Writer(OutBytes);
		Writer.SetUseUnversionedPropertySerialization(Path == ESerializationPath::Unversioned);

		FAngelscriptSerializationTestArchive Linker(Writer);
		FBinaryArchiveFormatter Formatter(Linker);
		FStructuredArchive StructuredArchive(Formatter);
		FStructuredArchive::FSlot Slot = StructuredArchive.Open();

		if (Path == ESerializationPath::Unversioned)
		{
			SerializeUnversionedProperties(
				Struct,
				Slot,
				reinterpret_cast<uint8*>(&MutableSource),
				Struct,
				reinterpret_cast<uint8*>(&MutableDefaults));
		}
		else
		{
			Struct->SerializeTaggedProperties(
				Slot,
				reinterpret_cast<uint8*>(&MutableSource),
				Struct,
				reinterpret_cast<uint8*>(&MutableDefaults));
		}
	}

	template <typename StructType>
	static bool LoadStruct(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const ESerializationPath Path,
		const TArray<uint8>& Bytes,
		const StructType& Defaults,
		StructType& OutLoaded)
	{
		StructType MutableDefaults = Defaults;
		UScriptStruct* Struct = StructType::StaticStruct();

		FMemoryReader Reader(Bytes);
		Reader.SetUseUnversionedPropertySerialization(Path == ESerializationPath::Unversioned);

		FAngelscriptSerializationTestArchive Linker(Reader);
		FBinaryArchiveFormatter Formatter(Linker);
		FStructuredArchive StructuredArchive(Formatter);
		FStructuredArchive::FSlot Slot = StructuredArchive.Open();

		TGuardValue<bool> SavingPackageGuard(GIsSavingPackage, false);
		OutLoaded = StructType();

		if (Path == ESerializationPath::Unversioned)
		{
			SerializeUnversionedProperties(
				Struct,
				Slot,
				reinterpret_cast<uint8*>(&OutLoaded),
				Struct,
				reinterpret_cast<uint8*>(&MutableDefaults));
		}
		else
		{
			Struct->UStruct::SerializeTaggedProperties(
				Slot,
				reinterpret_cast<uint8*>(&OutLoaded),
				Struct,
				reinterpret_cast<uint8*>(&MutableDefaults));
		}

		return Test.TestEqual(
			*FString::Printf(TEXT("%s should consume the entire %s payload"), Context, ToString(Path)),
			Reader.Tell(),
			static_cast<int64>(Bytes.Num()));
	}

	static bool ExpectInnerValueEquals(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const FAngelscriptSerializationInnerValue& Actual,
		const FAngelscriptSerializationInnerValue& Expected)
	{
		bool bOk = true;
		bOk &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve Count"), Context),
			Actual.Count,
			Expected.Count);
		bOk &= Test.TestTrue(
			*FString::Printf(TEXT("%s should preserve Ratio"), Context),
			FMath::IsNearlyEqual(Actual.Ratio, Expected.Ratio, KINDA_SMALL_NUMBER));
		bOk &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve Label"), Context),
			Actual.Label,
			Expected.Label);
		bOk &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve Token"), Context),
			Actual.Token,
			Expected.Token);
		return bOk;
	}

	static bool ExpectNestedFixtureEquals(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const FAngelscriptSerializationNestedFixture& Actual,
		const FAngelscriptSerializationNestedFixture& Expected)
	{
		bool bOk = true;
		bOk &= ExpectInnerValueEquals(Test, *FString::Printf(TEXT("%s Primary"), Context), Actual.Primary, Expected.Primary);
		bOk &= ExpectInnerValueEquals(Test, *FString::Printf(TEXT("%s Secondary"), Context), Actual.Secondary, Expected.Secondary);
		bOk &= Test.TestEqual(
			*FString::Printf(TEXT("%s should preserve Children.Num"), Context),
			Actual.Children.Num(),
			Expected.Children.Num());

		const int32 SharedChildCount = FMath::Min(Actual.Children.Num(), Expected.Children.Num());
		for (int32 Index = 0; Index < SharedChildCount; ++Index)
		{
			bOk &= ExpectInnerValueEquals(
				Test,
				*FString::Printf(TEXT("%s Children[%d]"), Context, Index),
				Actual.Children[Index],
				Expected.Children[Index]);
		}

		return bOk;
	}

	template <typename StructType, typename CompareFuncType>
	static bool ExpectRoundTrip(
		FAutomationTestBase& Test,
		const TCHAR* Context,
		const ESerializationPath Path,
		const StructType& Source,
		const StructType& Defaults,
		CompareFuncType&& Compare)
	{
		TArray<uint8> Payload;
		SaveStruct(Path, Source, Defaults, Payload);

		bool bOk = Test.TestTrue(
			*FString::Printf(TEXT("%s should produce a non-empty %s payload"), Context, ToString(Path)),
			Payload.Num() > 0);

		StructType Loaded;
		bOk &= LoadStruct(Test, Context, Path, Payload, Defaults, Loaded);
		if (!bOk)
		{
			return false;
		}

		return Compare(Loaded);
	}
}

using namespace AngelscriptTest_Core_AngelscriptSerializationTests_Private;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptUnversionedPropertySerializationNestedStructRoundTripTest,
	"Angelscript.TestModule.Engine.UnversionedPropertySerialization.NestedStructRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptUnversionedPropertySerializationVersionTolerantFieldAddRemoveTest,
	"Angelscript.TestModule.Engine.UnversionedPropertySerialization.VersionTolerantFieldAddRemove",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptUnversionedPropertySerializationNestedStructRoundTripTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(
			TEXT("Automation environment should enable unversioned property serialization"),
			IsUnversionedPropertySerializationEnabled()))
	{
		return false;
	}

	DestroySerializationTestSchemas();
	ON_SCOPE_EXIT
	{
		DestroySerializationTestSchemas();
	};

	const FAngelscriptSerializationNestedFixture Defaults;
	const FAngelscriptSerializationNestedFixture Source = MakeNestedFixture();

	bool bOk = true;
	bOk &= ExpectRoundTrip(
		*this,
		TEXT("Nested struct versioned roundtrip"),
		ESerializationPath::Versioned,
		Source,
		Defaults,
		[this, &Source](const FAngelscriptSerializationNestedFixture& Loaded)
		{
			return ExpectNestedFixtureEquals(*this, TEXT("Nested struct versioned roundtrip"), Loaded, Source);
		});
	bOk &= ExpectRoundTrip(
		*this,
		TEXT("Nested struct unversioned roundtrip"),
		ESerializationPath::Unversioned,
		Source,
		Defaults,
		[this, &Source](const FAngelscriptSerializationNestedFixture& Loaded)
		{
			return ExpectNestedFixtureEquals(*this, TEXT("Nested struct unversioned roundtrip"), Loaded, Source);
		});
	return bOk;
}

bool FAngelscriptUnversionedPropertySerializationVersionTolerantFieldAddRemoveTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(
			TEXT("Automation environment should enable unversioned property serialization"),
			IsUnversionedPropertySerializationEnabled()))
	{
		return false;
	}

	DestroySerializationTestSchemas();
	ON_SCOPE_EXIT
	{
		DestroySerializationTestSchemas();
	};

	const FAngelscriptSerializationVersionToleranceV1 DefaultsV1;
	const FAngelscriptSerializationVersionToleranceV2 DefaultsV2;
	const FAngelscriptSerializationVersionToleranceV1 LegacySource = MakeVersionToleranceV1Fixture();
	const FAngelscriptSerializationVersionToleranceV2 CurrentSource = MakeVersionToleranceV2Fixture();

	TArray<uint8> LegacyPayload;
	SaveStruct(ESerializationPath::Versioned, LegacySource, DefaultsV1, LegacyPayload);
	if (!TestTrue(TEXT("Legacy-to-current load should serialize a non-empty versioned payload"), LegacyPayload.Num() > 0))
	{
		return false;
	}

	FAngelscriptSerializationVersionToleranceV2 LoadedCurrent;
	if (!LoadStruct(
			*this,
			TEXT("Legacy-to-current load"),
			ESerializationPath::Versioned,
			LegacyPayload,
			DefaultsV2,
			LoadedCurrent))
	{
		return false;
	}

	bool bOk = true;
	bOk &= TestEqual(TEXT("Legacy-to-current load should preserve Count"), LoadedCurrent.Count, LegacySource.Count);
	bOk &= TestEqual(TEXT("Legacy-to-current load should preserve Title"), LoadedCurrent.Title, LegacySource.Title);
	bOk &= ExpectInnerValueEquals(*this, TEXT("Legacy-to-current load Payload"), LoadedCurrent.Payload, LegacySource.Payload);
	bOk &= TestEqual(TEXT("Legacy-to-current load should preserve Status"), LoadedCurrent.Status, LegacySource.Status);
	bOk &= TestEqual(TEXT("Legacy-to-current load should keep SchemaVersion at its destination default"), LoadedCurrent.SchemaVersion, DefaultsV2.SchemaVersion);
	bOk &= TestTrue(
		TEXT("Legacy-to-current load should keep Score at its destination default"),
		FMath::IsNearlyEqual(LoadedCurrent.Score, DefaultsV2.Score, KINDA_SMALL_NUMBER));

	TArray<uint8> CurrentPayload;
	SaveStruct(ESerializationPath::Versioned, CurrentSource, DefaultsV2, CurrentPayload);
	if (!TestTrue(TEXT("Current-to-legacy load should serialize a non-empty versioned payload"), CurrentPayload.Num() > 0))
	{
		return false;
	}

	FAngelscriptSerializationVersionToleranceV1 LoadedLegacy;
	if (!LoadStruct(
			*this,
			TEXT("Current-to-legacy load"),
			ESerializationPath::Versioned,
			CurrentPayload,
			DefaultsV1,
			LoadedLegacy))
	{
		return false;
	}

	bOk &= TestEqual(TEXT("Current-to-legacy load should preserve Count"), LoadedLegacy.Count, CurrentSource.Count);
	bOk &= TestEqual(TEXT("Current-to-legacy load should preserve Title"), LoadedLegacy.Title, CurrentSource.Title);
	bOk &= ExpectInnerValueEquals(*this, TEXT("Current-to-legacy load Payload"), LoadedLegacy.Payload, CurrentSource.Payload);
	bOk &= TestEqual(TEXT("Current-to-legacy load should preserve Status"), LoadedLegacy.Status, CurrentSource.Status);
	return bOk;
}

#endif
