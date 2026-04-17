#include "Misc/AutomationTest.h"
#include "Serialization/MemoryWriter.h"

#include "StaticJIT/StringInArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAngelscriptStaticJITStringInArchiveRoundtripUtf8Test,
	"Angelscript.TestModule.StaticJIT.StringInArchive.RoundtripUtf8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAngelscriptStaticJITStringInArchiveRoundtripUtf8Test::RunTest(const FString& Parameters)
{
	const FString Utf8Source = TEXT("UTF8-你好-ß");
	const FTCHARToUTF8 Utf8SourceBytes(*Utf8Source);
	const int32 ExpectedUtf8ByteLength = Utf8SourceBytes.Length();
	const int32 ExpectedSerializedByteCount = (sizeof(int32) * 2) + ExpectedUtf8ByteLength + 1;

	FStringInArchive EmptyWritten;
	FStringInArchive Utf8Written;
	Utf8Written.AssignAsUTF8(Utf8Source);

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer << EmptyWritten;
	Writer << Utf8Written;

	if (!TestEqual(
			TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should serialize two length fields plus the UTF-8 payload"),
			Bytes.Num(),
			ExpectedSerializedByteCount))
	{
		return false;
	}

	FMemoryReaderWithPtr Reader(Bytes);
	TestEqual(
		TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should start reading from the beginning of the buffer"),
		Reader.GetCurrentPtr(),
		static_cast<void*>(Bytes.GetData()));

	FStringInArchive EmptyRead;
	Reader << EmptyRead;
	TestEqual(TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should restore an empty string length"), EmptyRead.Len(), 0);
	TestTrue(TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should restore an empty string payload"), EmptyRead.UnrealString_UTF8().IsEmpty());
	TestEqual(
		TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should advance to the second length field after reading an empty string"),
		Reader.GetCurrentPtr(),
		static_cast<void*>(Bytes.GetData() + sizeof(int32)));

	FStringInArchive Utf8Read;
	Reader << Utf8Read;
	TestEqual(TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should preserve the UTF-8 byte length"), Utf8Read.Len(), ExpectedUtf8ByteLength);
	TestEqual(TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should reconstruct the original UTF-8 string"), Utf8Read.UnrealString_UTF8(), Utf8Source);
	TestEqual(
		TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should point the loaded string at the payload bytes in the source buffer"),
		static_cast<void*>(Utf8Read.Ptr),
		static_cast<void*>(Bytes.GetData() + (sizeof(int32) * 2)));
	TestEqual(
		TEXT("StaticJIT.StringInArchive.RoundtripUtf8 should leave the reader positioned at the end of the serialized buffer"),
		Reader.Tell(),
		static_cast<int64>(Bytes.Num()));

	return true;
}

#endif
