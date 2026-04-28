UCLASS()
class ACoverageComponentOwnerActor : AActor
{
	UPROPERTY()
	int OwnerValue = 77;
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

		ACoverageComponentOwnerActor OwnerActor = Cast<ACoverageComponentOwnerActor>(GetOwner());
		if (OwnerActor != null)
		{
			ReadOwnerValue = OwnerActor.OwnerValue;
		}
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaSeconds)
	{
		TickCount += 1;
	}
}
