/**
 * Subsystem lifecycle example.
 *
 * Unreal Engine provides several subsystem types that are automatically
 * created and destroyed with their outer object. AngelScript classes can
 * derive from the Script* base classes to add modular, self-contained
 * gameplay systems with full lifecycle and tick support.
 *
 * Available subsystem base classes:
 *   - UScriptWorldSubsystem       (per-world, with tick and streaming)
 *   - UScriptGameInstanceSubsystem (per-game-instance, with tick)
 *   - UScriptLocalPlayerSubsystem  (per-local-player)
 *   - UScriptEngineSubsystem       (engine lifetime, with tick)
 *
 * Each subsystem type is automatically instantiated by the engine when
 * its outer object is created, and destroyed when the outer is torn down.
 * Use Initialize / Deinitialize to hook into the lifecycle, and
 * Tick for per-frame updates.
 *
 * NOTE: The C++ events are named BP_Initialize, BP_Deinitialize, and
 * BP_Tick, but AngelScript exposes them with the stripped ScriptName
 * prefix — so use Initialize, Deinitialize, and Tick in script.
 *
 * The preprocessor auto-generates a static Get() method for each
 * subsystem subclass. The returned type is the concrete subclass,
 * e.g.:
 *   UMyWorldSystem System = UMyWorldSystem::Get();
 *
 * Note: The Get() method relies on Cast<> which requires the UClass
 * to be fully registered. It works at runtime but may fail during
 * initial hot-compile if the class has never been loaded before.
 */

/**
 * A world subsystem that tracks per-world state and ticks every frame.
 * Created automatically when a world is initialized; destroyed with it.
 */
UCLASS()
class UExampleWorldTracker : UScriptWorldSubsystem
{
	UPROPERTY(BlueprintReadOnly, Category = "Example")
	int TickCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Example")
	bool bIsActive = false;

	UFUNCTION(BlueprintOverride)
	void Initialize()
	{
		bIsActive = true;
		Log("ExampleWorldTracker: Initialized");
	}

	UFUNCTION(BlueprintOverride)
	void Deinitialize()
	{
		bIsActive = false;
		Log("ExampleWorldTracker: Deinitialized");
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaTime)
	{
		TickCount += 1;
	}

	UFUNCTION(BlueprintPure, Category = "Example")
	int GetCurrentTickCount()
	{
		return TickCount;
	}
};

/**
 * A game instance subsystem that persists across level transitions.
 * Created once when the game instance is initialized; destroyed on shutdown.
 */
UCLASS()
class UExampleSessionTracker : UScriptGameInstanceSubsystem
{
	UPROPERTY(BlueprintReadOnly, Category = "Example")
	int SessionInitCount = 0;

	UFUNCTION(BlueprintOverride)
	void Initialize()
	{
		SessionInitCount += 1;
		Log(f"ExampleSessionTracker: Session initialized (count: {SessionInitCount})");
	}

	UFUNCTION(BlueprintOverride)
	void Deinitialize()
	{
		Log("ExampleSessionTracker: Session deinitialized");
	}

	UFUNCTION(BlueprintPure, Category = "Example")
	int GetSessionInitCount()
	{
		return SessionInitCount;
	}
};
