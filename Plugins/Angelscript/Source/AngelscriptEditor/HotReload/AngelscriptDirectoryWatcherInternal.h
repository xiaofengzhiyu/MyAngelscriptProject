#pragma once

#include "CoreMinimal.h"
#include "IDirectoryWatcher.h"

#include "AngelscriptEngine.h"

namespace AngelscriptEditor::Private
{
	using FEnumerateLoadedScripts = TFunction<TArray<FAngelscriptEngine::FFilenamePair>(const FString& AbsoluteFolderPath)>;

	ANGELSCRIPTEDITOR_API TArray<FAngelscriptEngine::FFilenamePair> GatherLoadedScriptsForFolder(FAngelscriptEngine& Engine, const FString& AbsoluteFolderPath);
	ANGELSCRIPTEDITOR_API void QueueScriptFileChanges(const TArray<FFileChangeData>& Changes, const TArray<FString>& RootPaths, FAngelscriptEngine& Engine, IFileManager& FileManager, const FEnumerateLoadedScripts& EnumerateLoadedScripts);
}
