/**
 * Script interface example using UINTERFACE().
 *
 * This example demonstrates declaring and implementing a script-defined
 * interface with the UINTERFACE() + interface syntax, then using
 * Cast<> to call interface methods through a polymorphic reference.
 *
 * This is the AngelScript equivalent of the C++ pattern:
 *   UINTERFACE(BlueprintType)
 *   class UMyInterface : public UInterface { GENERATED_BODY() };
 *   class IMyInterface { virtual float GetValue() const = 0; };
 *
 * Key rules:
 *   - Interface names must start with UI prefix.
 *   - Implementing classes must use UCLASS().
 *   - Implementing methods must be marked with UFUNCTION().
 */

// ---- Interface declaration ----
UINTERFACE()
interface UIExampleDamageable
{
	float TakeDamage(float Amount);
	bool IsAlive() const;
	float GetHealthPercent() const;
};

// ---- Implementation A: standard actor ----
UCLASS()
class AExampleScriptDamageableActor : AActor, UIExampleDamageable
{
	UPROPERTY(Category = "Health")
	float MaxHealth = 100.0;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 100.0;

	UFUNCTION()
	float TakeDamage(float Amount)
	{
		CurrentHealth = Math::Max(CurrentHealth - Amount, 0.0);
		Print(f"[Damageable] {GetName()} took {Amount} damage -> {CurrentHealth}/{MaxHealth}");
		return CurrentHealth;
	}

	UFUNCTION()
	bool IsAlive() const
	{
		return CurrentHealth > 0.0;
	}

	UFUNCTION()
	float GetHealthPercent() const
	{
		if (MaxHealth <= 0.0)
			return 0.0;
		return CurrentHealth / MaxHealth;
	}
};

// ---- Implementation B: armored actor with damage reduction ----
UCLASS()
class AExampleArmoredActor : AActor, UIExampleDamageable
{
	UPROPERTY(Category = "Health")
	float MaxHealth = 200.0;

	UPROPERTY(BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 200.0;

	UPROPERTY(Category = "Armor")
	float DamageReduction = 0.5;

	UFUNCTION()
	float TakeDamage(float Amount)
	{
		float ReducedAmount = Amount * (1.0 - DamageReduction);
		CurrentHealth = Math::Max(CurrentHealth - ReducedAmount, 0.0);
		Print(f"[Armored] {GetName()} took {ReducedAmount} (reduced from {Amount}) -> {CurrentHealth}/{MaxHealth}");
		return CurrentHealth;
	}

	UFUNCTION()
	bool IsAlive() const
	{
		return CurrentHealth > 0.0;
	}

	UFUNCTION()
	float GetHealthPercent() const
	{
		if (MaxHealth <= 0.0)
			return 0.0;
		return CurrentHealth / MaxHealth;
	}
};

// ---- Utility: polymorphic dispatch through interface ----
/**
 * Applies damage to any actor implementing UIExampleDamageable.
 * Demonstrates Cast<> to interface type and calling interface methods.
 */
UFUNCTION(Category = "Example Interface")
void ApplyDamageViaInterface(AActor Target, float Damage)
{
	if (Target == nullptr)
		return;

	// Cast to the script interface — works with any implementing class.
	UIExampleDamageable Damageable = Cast<UIExampleDamageable>(Target);
	if (Damageable == nullptr)
	{
		Print(f"{Target.GetName()} does not implement UIExampleDamageable.");
		return;
	}

	Damageable.TakeDamage(Damage);
	Print(f"  Alive: {Damageable.IsAlive()}, Health: {Damageable.GetHealthPercent() * 100.0}%");
}

// ---- Using TScriptInterface<> across the reflection boundary (Phase 2, 2026-04-24) ----
/**
 * Actor that tracks damageable targets, demonstrating the TScriptInterface<>
 * patterns supported by the AngelScript plugin as of Phase 2:
 *
 *   1. `TScriptInterface<UIFoo>` as a local variable
 *   2. Construction/assignment from a UObject (auto-computes InterfacePointer
 *      and raises an AS exception on non-implementers)
 *   3. `Obj.Implements<UIFoo>()` (Phase 5 preprocessor sugar) as a pre-flight
 *      check before constructing a TScriptInterface<>
 *
 * The AS form `TScriptInterface<UIExampleDamageable>` is memory-layout
 * compatible with the C++ form `TScriptInterface<IExampleDamageable>` used on
 * the reflection side — the plugin handles the `U`→`I` prefix translation
 * automatically when generating C++ forms.
 *
 * Note: The Phase 2 closure landed `TScriptInterface<>` support for local
 * variables and C++-side UPROPERTY round-trip. Script-owned `UPROPERTY`
 * fields, UFUNCTION parameters, and UFUNCTION return values of type
 * `TScriptInterface<UIFoo>` are **not yet** accepted by the class generator
 * analysis path — that surface is tracked for a later iteration. For now,
 * store a bare UObject / AActor in UPROPERTY / parameter slots, then build
 * a local `TScriptInterface<>` inside the method body when you need the
 * offset-aware dispatch path.
 */
UCLASS()
class AExampleDamageTracker : AActor
{
	// Plain UObject/AActor UPROPERTY — the tracker accepts anything and
	// defers the interface check to the moment of dispatch.
	UPROPERTY(Category = "Tracker")
	AActor TargetActor;

	UFUNCTION(BlueprintCallable, Category = "Tracker")
	void SetTarget(UObject NewTarget)
	{
		// Phase 5 sugar: `Obj.Implements<T>()` rewrites to
		// `Obj.ImplementsInterface(T::StaticClass())` at preprocess time.
		if (NewTarget == nullptr || !NewTarget.Implements<UIExampleDamageable>())
		{
			TargetActor = nullptr;
			return;
		}

		TargetActor = Cast<AActor>(NewTarget);
	}

	UFUNCTION(BlueprintCallable, Category = "Tracker")
	void DamageCurrentTarget(float Amount)
	{
		if (TargetActor == nullptr)
			return;

		if (!TargetActor.Implements<UIExampleDamageable>())
			return;

		// Local TScriptInterface<>: construction from a UObject validates
		// ImplementsInterface and computes the correct InterfacePointer.
		// If the object doesn't implement the interface, an AS exception
		// is raised — the Implements<> check above makes that path safe.
		TScriptInterface<UIExampleDamageable> DamageableRef = TargetActor;
		Print(f"[Tracker] TScriptInterface valid={DamageableRef.IsValid()} target={TargetActor.GetName()}");

		// Dispatch through the interface type (offset-aware on C++ natives).
		UIExampleDamageable Damageable = Cast<UIExampleDamageable>(DamageableRef.Get());
		if (Damageable != nullptr)
			Damageable.TakeDamage(Amount);
	}
};

