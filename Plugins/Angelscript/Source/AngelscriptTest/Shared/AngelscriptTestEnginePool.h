#pragma once

#include "Shared/AngelscriptTestUtilities.h"

#include "HAL/PlatformTime.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectIterator.h"

namespace AngelscriptTestSupport
{
	struct FAngelscriptTestEnginePoolMetrics
	{
		int32 SourceEngineCreateCount = 0;
		int32 ModuleCleanAcquireCount = 0;
		int32 ModuleCleanCount = 0;
		int32 ActiveModuleDiscardCount = 0;
		int32 RawModuleDiscardCount = 0;
		int32 DetachedClassCleanupCount = 0;
		int32 RootedDetachedClassCleanupCount = 0;
		int32 GarbageCollectCount = 0;
		int32 LastActiveModuleDiscardCount = 0;
		int32 LastRawModuleDiscardCount = 0;
		int32 LastDetachedClassCount = 0;
		int32 LastRootedDetachedClassCount = 0;
		double LastModuleCleanSeconds = 0.0;
		double LastGarbageCollectSeconds = 0.0;
	};

	struct FAngelscriptTestEngineModuleSnapshot
	{
		TSet<FString> ActiveModuleNames;
		TSet<FString> RawModuleNames;
	};

	class FAngelscriptTestEnginePool
	{
	public:
		static FAngelscriptTestEnginePool& Get()
		{
			static FAngelscriptTestEnginePool Pool;
			return Pool;
		}

		void Startup(bool bPrewarmEngine)
		{
			if (bPrewarmEngine)
			{
				PrewarmSourceEngine();
			}
		}

		void Shutdown()
		{
			DestroySharedTestEngine();
			Metrics = FAngelscriptTestEnginePoolMetrics();
			GarbageCollectEveryNCleanups = DefaultGarbageCollectEveryNCleanups;
		}

		FAngelscriptEngine& PrewarmSourceEngine()
		{
			if (!GetSharedTestEngineStorage().IsValid())
			{
				++Metrics.SourceEngineCreateCount;
			}

			return GetOrCreateSharedCloneEngine();
		}

		FAngelscriptEngine& AcquireModuleCleanEngine()
		{
			++Metrics.ModuleCleanAcquireCount;
			return PrewarmSourceEngine();
		}

		FAngelscriptTestEngineModuleSnapshot CaptureSnapshot(FAngelscriptEngine& Engine) const
		{
			FAngelscriptTestEngineModuleSnapshot Snapshot;
			for (const TSharedRef<FAngelscriptModuleDesc>& Module : Engine.GetActiveModules())
			{
				Snapshot.ActiveModuleNames.Add(Module->ModuleName);
			}

			if (asCScriptEngine* ScriptEngine = reinterpret_cast<asCScriptEngine*>(Engine.GetScriptEngine()))
			{
				const asUINT ModuleCount = ScriptEngine->GetModuleCount();
				for (asUINT ModuleIndex = 0; ModuleIndex < ModuleCount; ++ModuleIndex)
				{
					if (asIScriptModule* Module = ScriptEngine->GetModuleByIndex(ModuleIndex))
					{
						Snapshot.RawModuleNames.Add(UTF8_TO_TCHAR(Module->GetName()));
					}
				}
			}

			return Snapshot;
		}

		void CleanupModuleCleanEngine(FAngelscriptEngine& Engine, const FAngelscriptTestEngineModuleSnapshot& Baseline)
		{
			const double StartSeconds = FPlatformTime::Seconds();
			int32 ActiveDiscardCount = 0;
			int32 RawDiscardCount = 0;

			TArray<FString> ActiveModuleNames;
			for (const TSharedRef<FAngelscriptModuleDesc>& Module : Engine.GetActiveModules())
			{
				if (!Baseline.ActiveModuleNames.Contains(Module->ModuleName))
				{
					ActiveModuleNames.Add(Module->ModuleName);
				}
			}

			for (const FString& ModuleName : ActiveModuleNames)
			{
				if (Engine.DiscardModule(*ModuleName))
				{
					++ActiveDiscardCount;
				}
			}

			if (asCScriptEngine* ScriptEngine = reinterpret_cast<asCScriptEngine*>(Engine.GetScriptEngine()))
			{
				TArray<FString> RawModuleNames;
				const asUINT ModuleCount = ScriptEngine->GetModuleCount();
				RawModuleNames.Reserve(static_cast<int32>(ModuleCount));
				for (asUINT ModuleIndex = 0; ModuleIndex < ModuleCount; ++ModuleIndex)
				{
					if (asIScriptModule* Module = ScriptEngine->GetModuleByIndex(ModuleIndex))
					{
						RawModuleNames.Add(UTF8_TO_TCHAR(Module->GetName()));
					}
				}

				for (const FString& ModuleName : RawModuleNames)
				{
					if (!Baseline.RawModuleNames.Contains(ModuleName))
					{
						const auto ModuleNameAnsi = StringCast<ANSICHAR>(*ModuleName);
						ScriptEngine->DiscardModule(ModuleNameAnsi.Get());
						++RawDiscardCount;
					}
				}

				if (RawDiscardCount > 0)
				{
					ScriptEngine->DeleteDiscardedModules();
				}
			}

			const FDetachedClassCleanupResult DetachedClassResult = CleanupDetachedClasses();
			++Metrics.ModuleCleanCount;
			Metrics.ActiveModuleDiscardCount += ActiveDiscardCount;
			Metrics.RawModuleDiscardCount += RawDiscardCount;
			Metrics.DetachedClassCleanupCount += DetachedClassResult.DetachedClassCount;
			Metrics.RootedDetachedClassCleanupCount += DetachedClassResult.RootedDetachedClassCount;
			Metrics.LastActiveModuleDiscardCount = ActiveDiscardCount;
			Metrics.LastRawModuleDiscardCount = RawDiscardCount;
			Metrics.LastDetachedClassCount = DetachedClassResult.DetachedClassCount;
			Metrics.LastRootedDetachedClassCount = DetachedClassResult.RootedDetachedClassCount;
			Metrics.LastModuleCleanSeconds = FPlatformTime::Seconds() - StartSeconds;

			if (Metrics.ModuleCleanCount % GarbageCollectEveryNCleanups == 0)
			{
				CollectGarbageForPool();
			}
		}

		const FAngelscriptTestEnginePoolMetrics& GetMetrics() const
		{
			return Metrics;
		}

		void SetGarbageCollectEveryNCleanups(int32 InGarbageCollectEveryNCleanups)
		{
			GarbageCollectEveryNCleanups = FMath::Max(1, InGarbageCollectEveryNCleanups);
		}

	private:
		struct FDetachedClassCleanupResult
		{
			int32 DetachedClassCount = 0;
			int32 RootedDetachedClassCount = 0;
		};

		FDetachedClassCleanupResult CleanupDetachedClasses()
		{
			FDetachedClassCleanupResult Result;
			for (TObjectIterator<UASClass> It; It; ++It)
			{
				if (It->ScriptTypePtr != nullptr)
				{
					continue;
				}

				++Result.DetachedClassCount;
				if (It->IsRooted())
				{
					It->RemoveFromRoot();
					++Result.RootedDetachedClassCount;
				}
				It->ClearFlags(RF_Standalone);
			}
			return Result;
		}

		void CollectGarbageForPool()
		{
			const double StartSeconds = FPlatformTime::Seconds();
			CollectGarbage(RF_NoFlags, true);
			++Metrics.GarbageCollectCount;
			Metrics.LastGarbageCollectSeconds = FPlatformTime::Seconds() - StartSeconds;
		}

		FAngelscriptTestEnginePoolMetrics Metrics;
		static constexpr int32 DefaultGarbageCollectEveryNCleanups = 25;
		int32 GarbageCollectEveryNCleanups = DefaultGarbageCollectEveryNCleanups;
	};

	struct FScopedModuleCleanEngine
	{
		explicit FScopedModuleCleanEngine(FAngelscriptEngine& InEngine)
			: Engine(InEngine)
			, Baseline(FAngelscriptTestEnginePool::Get().CaptureSnapshot(InEngine))
		{
		}

		~FScopedModuleCleanEngine()
		{
			FAngelscriptTestEnginePool::Get().CleanupModuleCleanEngine(Engine, Baseline);
		}

	private:
		FAngelscriptEngine& Engine;
		FAngelscriptTestEngineModuleSnapshot Baseline;
	};

	inline void StartupTestEnginePool(bool bPrewarmEngine)
	{
		FAngelscriptTestEnginePool::Get().Startup(bPrewarmEngine);
	}

	inline void ShutdownTestEnginePool()
	{
		FAngelscriptTestEnginePool::Get().Shutdown();
	}

	inline FAngelscriptEngine& PrewarmTestEnginePool()
	{
		return FAngelscriptTestEnginePool::Get().PrewarmSourceEngine();
	}

	inline FAngelscriptEngine& AcquireModuleCleanSharedEngine()
	{
		return FAngelscriptTestEnginePool::Get().AcquireModuleCleanEngine();
	}

	inline FAngelscriptTestEnginePoolMetrics GetTestEnginePoolMetrics()
	{
		return FAngelscriptTestEnginePool::Get().GetMetrics();
	}
}
