UCLASS()
class ACoverageExampleActor : AActor
{
	UPROPERTY(Category = "Coverage|Actor")
	int Health = 125;

	UPROPERTY(Category = "Coverage|Actor")
	FString DisplayName = "CoverageActor";

	UPROPERTY(Category = "Coverage|Actor")
	bool bBeginPlayTriggered = false;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		bBeginPlayTriggered = true;
	}

	UFUNCTION()
	int GetHealthValue()
	{
		return Health;
	}

	default bReplicates = true;
	default PrimaryActorTick.TickInterval = 0.25f;
	default Tags.Add(n"CoverageActor");
}
