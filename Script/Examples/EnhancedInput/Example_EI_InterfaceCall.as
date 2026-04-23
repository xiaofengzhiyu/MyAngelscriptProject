/**
 * Enhanced Input interface auto-binding example.
 *
 * This example demonstrates calling C++ UInterface methods that are
 * now automatically bound to AngelScript by the Phase 5 interface
 * auto-registration in Bind_BlueprintType.cpp.
 *
 * Before this feature, IEnhancedInputSubsystemInterface methods like
 * AddMappingContext / RemoveMappingContext were invisible to AS because:
 *   1. TFieldIterator<UFunction>(ExcludeSuper) skipped interface methods.
 *   2. BlueprintCallableReflectiveFallback rejected CLASS_Interface.
 *
 * Now the PlayerController can obtain the EnhancedInput subsystem and
 * call interface methods directly, just like in C++.
 */
class AExampleEIInterfaceCallController : APlayerController
{
	UPROPERTY(Category = "Enhanced Input")
	UInputMappingContext DefaultMappingContext;

	UPROPERTY(Category = "Enhanced Input")
	int DefaultMappingPriority = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		SetupInputMapping();
	}

	/**
	 * Demonstrates the full interface call chain:
	 *   1. Get the UEnhancedInputLocalPlayerSubsystem
	 *   2. Cast<> to UEnhancedInputSubsystemInterface
	 *   3. Call AddMappingContext through the interface reference
	 */
	void SetupInputMapping()
	{
		// --- Step 1: Get the subsystem instance ---
		ULocalPlayer LP = GetLocalPlayer();
		if (LP == nullptr)
		{
			Print("[EI Interface] No local player found.");
			return;
		}

		UEnhancedInputLocalPlayerSubsystem Subsystem =
			UEnhancedInputLocalPlayerSubsystem::Get(LP);
		if (Subsystem == nullptr)
		{
			Print("[EI Interface] EnhancedInput subsystem not available.");
			return;
		}

		// --- Step 2: Cast to the interface type ---
		// This Cast<> works because Phase 5 registered the interface type
		// and CanCastScriptObjectToUnrealInterface handles the runtime check.
		UEnhancedInputSubsystemInterface InputInterface =
			Cast<UEnhancedInputSubsystemInterface>(Subsystem);
		if (InputInterface == nullptr)
		{
			Print("[EI Interface] Cast to UEnhancedInputSubsystemInterface failed!");
			return;
		}

		Print("[EI Interface] Cast to interface succeeded.");

		// --- Step 3: Call interface methods ---
		if (DefaultMappingContext != nullptr)
		{
			// AddMappingContext is a method on IEnhancedInputSubsystemInterface,
			// now auto-bound by Phase 5. This call goes through:
			//   CallInterfaceMethod → FindFunction → ProcessEvent
			FModifyContextOptions Options;
			InputInterface.AddMappingContext(DefaultMappingContext, DefaultMappingPriority, Options);
			Print(f"[EI Interface] Added mapping context: {DefaultMappingContext.GetName()} at priority {DefaultMappingPriority}");

			// HasMappingContext: verify the context was added
			int FoundPriority = -1;
			bool bHasContext = InputInterface.HasMappingContext(DefaultMappingContext, FoundPriority);
			Print(f"[EI Interface] HasMappingContext: {bHasContext}, priority: {FoundPriority}");
		}
		else
		{
			Print("[EI Interface] No DefaultMappingContext assigned — skipping AddMappingContext.");
		}

		// QueryKeysMappedToAction: demonstrates a method with a return value
		// (requires a valid InputAction, so we just show the call compiles)
		// TArray<FKey> Keys = InputInterface.QueryKeysMappedToAction(SomeAction);
	}

	UFUNCTION(BlueprintOverride)
	void EndPlay(EEndPlayReason EndPlayReason)
	{
		// --- Cleanup: remove the mapping context through the interface ---
		ULocalPlayer LP = GetLocalPlayer();
		if (LP == nullptr)
			return;

		UEnhancedInputLocalPlayerSubsystem Subsystem =
			UEnhancedInputLocalPlayerSubsystem::Get(LP);
		if (Subsystem == nullptr)
			return;

		UEnhancedInputSubsystemInterface InputInterface =
			Cast<UEnhancedInputSubsystemInterface>(Subsystem);

		if (InputInterface != nullptr && DefaultMappingContext != nullptr)
		{
			FModifyContextOptions Options;
			InputInterface.RemoveMappingContext(DefaultMappingContext, Options);
			Print(f"[EI Interface] Removed mapping context: {DefaultMappingContext.GetName()}");
		}
	}
};
