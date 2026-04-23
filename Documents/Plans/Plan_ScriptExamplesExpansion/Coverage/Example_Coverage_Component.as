// Example_Coverage_Component.as
// Coverage test companion script — validates component BeginPlay,
// Tick counting, and reading an owning actor's reflected property.

UCLASS()
class ACoverageComponentOwnerActor : AActor
{
	UPROPERTY()
	int OwnerMarker = 77;
}

UCLASS()
class UCoverageExampleComponent : UAngelscriptComponent
{
	UPROPERTY()
	bool bReady = false;

	UPROPERTY()
	int TickCount = 0;

	UPROPERTY()
	int ReadOwnerValue = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		bReady = true;
		AActor Owner = GetOwner();
		if (Owner != nullptr)
		{
			ACoverageComponentOwnerActor TypedOwner = Cast<ACoverageComponentOwnerActor>(Owner);
			if (TypedOwner != nullptr)
			{
				ReadOwnerValue = TypedOwner.OwnerMarker;
			}
		}
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaSeconds)
	{
		TickCount += 1;
	}
}
