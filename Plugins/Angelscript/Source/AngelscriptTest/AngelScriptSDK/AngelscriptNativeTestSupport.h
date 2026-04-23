#pragma once

#include "CoreMinimal.h"
#include "AngelscriptInclude.h"

#include <cstring>

namespace AngelscriptNativeTestSupport
{
	struct FNativeMessageEntry
	{
		FString Section;
		int32 Row = 0;
		int32 Column = 0;
		asEMsgType Type = asMSGTYPE_INFORMATION;
		FString Message;
	};

	struct FNativeMessageCollector
	{
		TArray<FNativeMessageEntry> Entries;

		void Reset()
		{
			Entries.Reset();
		}
	};

	inline const TCHAR* ToMessageTypeString(const asEMsgType Type)
	{
		switch (Type)
		{
		case asMSGTYPE_ERROR:
			return TEXT("Error");
		case asMSGTYPE_WARNING:
			return TEXT("Warning");
		case asMSGTYPE_INFORMATION:
		default:
			return TEXT("Info");
		}
	}

	inline void CaptureNativeMessage(const asSMessageInfo* MessageInfo, void* UserData)
	{
		if (MessageInfo == nullptr)
		{
			return;
		}

		FNativeMessageCollector* const Collector = static_cast<FNativeMessageCollector*>(UserData);
		if (Collector == nullptr)
		{
			return;
		}

		FNativeMessageEntry Entry;
		Entry.Section = UTF8_TO_TCHAR(MessageInfo->section != nullptr ? MessageInfo->section : "");
		Entry.Row = MessageInfo->row;
		Entry.Column = MessageInfo->col;
		Entry.Type = MessageInfo->type;
		Entry.Message = UTF8_TO_TCHAR(MessageInfo->message != nullptr ? MessageInfo->message : "");
		Collector->Entries.Add(MoveTemp(Entry));
	}

	inline FNativeMessageCollector& GetDefaultMessageCollector()
	{
		static FNativeMessageCollector Collector;
		return Collector;
	}

	inline FString CollectMessages(const FNativeMessageCollector& Collector)
	{
		FString Result;
		for (const FNativeMessageEntry& Entry : Collector.Entries)
		{
			if (!Result.IsEmpty())
			{
				Result += LINE_TERMINATOR;
			}

			Result += FString::Printf(
				TEXT("[%s] %s:%d:%d %s"),
				ToMessageTypeString(Entry.Type),
				Entry.Section.IsEmpty() ? TEXT("<memory>") : *Entry.Section,
				Entry.Row,
				Entry.Column,
				*Entry.Message);
		}

		return Result;
	}

	inline FString CollectFunctionDeclarations(asIScriptModule* Module)
	{
		if (Module == nullptr)
		{
			return TEXT("<null module>");
		}

		FString Result;
		const asUINT FunctionCount = Module->GetFunctionCount();
		for (asUINT FunctionIndex = 0; FunctionIndex < FunctionCount; ++FunctionIndex)
		{
			asIScriptFunction* const Function = Module->GetFunctionByIndex(FunctionIndex);
			if (Function == nullptr)
			{
				continue;
			}

			if (!Result.IsEmpty())
			{
				Result += TEXT(", ");
			}

			Result += UTF8_TO_TCHAR(Function->GetDeclaration());
		}

		return Result.IsEmpty() ? TEXT("<no functions>") : Result;
	}

	inline asIScriptEngine* CreateNativeEngine(FNativeMessageCollector* MessageCollector = nullptr)
	{
		FNativeMessageCollector* const Collector = MessageCollector != nullptr ? MessageCollector : &GetDefaultMessageCollector();
		Collector->Reset();

		asIScriptEngine* const ScriptEngine = asCreateScriptEngine(ANGELSCRIPT_VERSION);
		if (ScriptEngine == nullptr)
		{
			return nullptr;
		}

		ScriptEngine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1);
		ScriptEngine->SetEngineProperty(asEP_USE_CHARACTER_LITERALS, 1);
		ScriptEngine->SetEngineProperty(asEP_ALLOW_MULTILINE_STRINGS, 1);
		ScriptEngine->SetEngineProperty(asEP_SCRIPT_SCANNER, 1);
		ScriptEngine->SetEngineProperty(asEP_OPTIMIZE_BYTECODE, 1);
		ScriptEngine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
		ScriptEngine->SetEngineProperty(asEP_ALTER_SYNTAX_NAMED_ARGS, 1);
		ScriptEngine->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
		ScriptEngine->SetEngineProperty(asEP_ALLOW_IMPLICIT_HANDLE_TYPES, 1);
		ScriptEngine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);
		ScriptEngine->SetEngineProperty(asEP_ALWAYS_IMPL_DEFAULT_CONSTRUCT, 1);
		ScriptEngine->SetEngineProperty(asEP_ALWAYS_IMPL_DEFAULT_COPY, 1);
		ScriptEngine->SetEngineProperty(asEP_ALWAYS_IMPL_DEFAULT_COPY_CONSTRUCT, 1);
		ScriptEngine->SetEngineProperty(asEP_MEMBER_INIT_MODE, 0);
		ScriptEngine->SetEngineProperty(asEP_TYPECHECK_SWITCH_ENUMS, 1);
		ScriptEngine->SetEngineProperty(asEP_ALLOW_DOUBLE_TYPE, 1);

		const int CallbackResult = ScriptEngine->SetMessageCallback(asFUNCTION(CaptureNativeMessage), Collector, asCALL_CDECL);
		if (CallbackResult < 0)
		{
			ScriptEngine->ShutDownAndRelease();
			return nullptr;
		}

		return ScriptEngine;
	}

	inline void DestroyNativeEngine(asIScriptEngine* ScriptEngine)
	{
		if (ScriptEngine != nullptr)
		{
			ScriptEngine->ShutDownAndRelease();
		}
	}

	inline int CompileNativeModule(asIScriptEngine* ScriptEngine, const char* ModuleName, const char* Source, asIScriptModule*& OutModule)
	{
		OutModule = nullptr;
		if (ScriptEngine == nullptr || ModuleName == nullptr || Source == nullptr)
		{
			return asINVALID_ARG;
		}

		asIScriptModule* const Module = ScriptEngine->GetModule(ModuleName, asGM_ALWAYS_CREATE);
		if (Module == nullptr)
		{
			return asNO_MODULE;
		}

		OutModule = Module;
		const int AddSectionResult = Module->AddScriptSection(ModuleName, Source, static_cast<unsigned int>(std::strlen(Source)));
		if (AddSectionResult < 0)
		{
			return AddSectionResult;
		}

		return Module->Build();
	}

	inline asIScriptModule* BuildNativeModule(asIScriptEngine* ScriptEngine, const char* ModuleName, const char* Source)
	{
		asIScriptModule* Module = nullptr;
		return CompileNativeModule(ScriptEngine, ModuleName, Source, Module) >= 0 ? Module : nullptr;
	}

	inline asIScriptFunction* GetNativeFunctionByDecl(asIScriptModule* Module, const char* Declaration)
	{
		if (Module == nullptr || Declaration == nullptr)
		{
			return nullptr;
		}

		asIScriptFunction* Function = Module->GetFunctionByDecl(Declaration);
		if (Function != nullptr)
		{
			return Function;
		}

		const FString DeclarationString = UTF8_TO_TCHAR(Declaration);
		int32 OpenParenIndex = INDEX_NONE;
		if (!DeclarationString.FindChar(TEXT('('), OpenParenIndex))
		{
			return nullptr;
		}

		const FString Prefix = DeclarationString.Left(OpenParenIndex).TrimStartAndEnd();
		int32 NameSeparatorIndex = INDEX_NONE;
		if (!Prefix.FindLastChar(TEXT(' '), NameSeparatorIndex))
		{
			return nullptr;
		}

		const FString FunctionName = Prefix.Mid(NameSeparatorIndex + 1).TrimStartAndEnd();
		if (FunctionName.IsEmpty())
		{
			return nullptr;
		}

		const FTCHARToUTF8 FunctionNameUtf8(*FunctionName);
		Function = Module->GetFunctionByName(FunctionNameUtf8.Get());
		if (Function != nullptr)
		{
			return Function;
		}

		const asUINT FunctionCount = Module->GetFunctionCount();
		if (FunctionCount == 1)
		{
			return Module->GetFunctionByIndex(0);
		}

		for (asUINT FunctionIndex = 0; FunctionIndex < FunctionCount; ++FunctionIndex)
		{
			asIScriptFunction* const CandidateFunction = Module->GetFunctionByIndex(FunctionIndex);
			if (CandidateFunction != nullptr && FunctionName.Equals(UTF8_TO_TCHAR(CandidateFunction->GetName())))
			{
				return CandidateFunction;
			}
		}

		return nullptr;
	}

	inline int PrepareAndExecute(asIScriptContext* Context, asIScriptFunction* Function)
	{
		if (Context == nullptr || Function == nullptr)
		{
			return asINVALID_ARG;
		}

		const int PrepareResult = Context->Prepare(Function);
		return PrepareResult == asSUCCESS ? Context->Execute() : PrepareResult;
	}
}
