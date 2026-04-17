// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/UnrealType.h"

struct FBlake3Hash;

// Serialize sparse unversioned properties for a particular struct
ANGELSCRIPTRUNTIME_API void SerializeUnversionedProperties(const UStruct* Struct, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults);

ANGELSCRIPTRUNTIME_API void DestroyAngelscriptUnversionedSchema(const UStruct* Struct);

#if WITH_EDITORONLY_DATA
/**
 * Read the SchemaHash for the given struct, with our without editor-only properties.
 * This hash covers the Struct's data for both versioned and unversioned serialization.
 */
ANGELSCRIPTRUNTIME_API const FBlake3Hash& GetSchemaHash(const UStruct* Struct, bool bSkipEditorOnly);
#endif
