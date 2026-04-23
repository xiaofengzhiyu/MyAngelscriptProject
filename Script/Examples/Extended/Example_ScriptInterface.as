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
