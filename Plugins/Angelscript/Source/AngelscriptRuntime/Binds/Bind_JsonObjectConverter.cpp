#include "AngelscriptBinds.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
bool DeserializeJsonObjectString(const FString& JsonString, TSharedPtr<FJsonObject>& OutJsonObject)
{
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	return FJsonSerializer::Deserialize(JsonReader, OutJsonObject) && OutJsonObject.IsValid();
}

bool SerializeJsonObjectString(
	const TSharedRef<FJsonObject>& JsonObject,
	FString& OutJsonString,
	int Indent,
	bool PrettyPrint)
{
	if (PrettyPrint)
	{
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutJsonString, Indent);
		return FJsonSerializer::Serialize(JsonObject, JsonWriter);
	}

	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJsonString, Indent);
	return FJsonSerializer::Serialize(JsonObject, JsonWriter);
}

bool UStructToJsonObjectString(const void* Data, int TypeId, FString& Result, int CheckFlags, int SkipFlags, int Indent, bool PrettyPrint)
{
	const FAngelscriptTypeUsage Usage = FAngelscriptTypeUsage::FromTypeId(TypeId);
	const UStruct* StructDef = Usage.GetUnrealStruct();
	if (StructDef == nullptr)
	{
		UE_LOG(LogJson, Warning, TEXT("UStructToJsonObjectString - not a valid USTRUCT"));
		return false;
	}

	return FJsonObjectConverter::UStructToJsonObjectString(
		StructDef, Data, Result, CheckFlags, SkipFlags, Indent, nullptr, PrettyPrint);
}

bool AppendUStructToJsonObjectString(const void* Data, int TypeId, FString& InOutString, int CheckFlags, int SkipFlags, int Indent, bool PrettyPrint)
{
	FString NewJsonString;
	const bool bConversionResult = UStructToJsonObjectString(Data, TypeId, NewJsonString, CheckFlags, SkipFlags, Indent, PrettyPrint);
	if (!bConversionResult)
	{
		return false;
	}

	if (InOutString.IsEmpty())
	{
		InOutString = MoveTemp(NewJsonString);
		return true;
	}

	TSharedPtr<FJsonObject> ExistingJsonObject;
	if (!DeserializeJsonObjectString(InOutString, ExistingJsonObject))
	{
		UE_LOG(LogJson, Warning, TEXT("AppendUStructToJsonObjectString - Unable to parse existing json=[%s]"), *InOutString);
		return false;
	}

	TSharedPtr<FJsonObject> NewJsonObject;
	if (!DeserializeJsonObjectString(NewJsonString, NewJsonObject))
	{
		UE_LOG(LogJson, Warning, TEXT("AppendUStructToJsonObjectString - Unable to parse appended json=[%s]"), *NewJsonString);
		return false;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& FieldPair : NewJsonObject->Values)
	{
		ExistingJsonObject->SetField(FieldPair.Key, FieldPair.Value);
	}

	InOutString.Reset();
	return SerializeJsonObjectString(ExistingJsonObject.ToSharedRef(), InOutString, Indent, PrettyPrint);
}

bool JsonObjectStringToUStruct(const FString& JsonString, void* Data, int TypeId, int CheckFlags, int SkipFlags)
{
	const FAngelscriptTypeUsage Usage = FAngelscriptTypeUsage::FromTypeId(TypeId);
	const UStruct* StructDef = Usage.GetUnrealStruct();
	if (StructDef == nullptr)
	{
		UE_LOG(LogJson, Warning, TEXT("JsonObjectStringToUStruct - not a valid USTRUCT"));
		return false;
	}

	// This part is copied verbatim from FJsonObjectConverter::JsonObjectStringToUStruct
	// There is no API that does deserialize that also takes a UStruct*;
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(JsonReader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogJson, Warning, TEXT("JsonObjectStringToUStruct - Unable to parse json=[%s]"), *JsonString);
		return false;
	}

	return FJsonObjectConverter::JsonObjectToUStruct(JsonObject.ToSharedRef(), StructDef, Data, CheckFlags, SkipFlags);
}

}	// namespace

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_JsonObjectConvert(FAngelscriptBinds::EOrder::Late, [] {
	FAngelscriptBinds::FNamespace ns("FJsonObjectConverter");

	const FString StructToStringSignature =
		"bool UStructToJsonObjectString(const ?&in MaybeStruct, "
		"FString &out Result, "
		"int CheckFlags = 0, "
		"int SkipFlags = 0, "
		"int Indent = 0, "
		"bool PrettyPrint = true)";
	FAngelscriptBinds::BindGlobalFunction(StructToStringSignature, UStructToJsonObjectString);

	const FString AppendStructToStringSignature =
		"bool AppendUStructToJsonObjectString(const ?&in MaybeStruct, "
		"FString& InOutString, "
		"int CheckFlags = 0, "
		"int SkipFlags = 0, "
		"int Indent = 0, "
		"bool PrettyPrint = true)";
	FAngelscriptBinds::BindGlobalFunction(AppendStructToStringSignature, AppendUStructToJsonObjectString);

	const FString StringToStructSignature =
		"bool JsonObjectStringToUStruct(const FString &in JsonString, "
		"?&out MaybeStruct, "
		"int CheckFlags = 0, "
		"int SkipFlags = 0)";
	FAngelscriptBinds::BindGlobalFunction(StringToStructSignature, JsonObjectStringToUStruct);
});
