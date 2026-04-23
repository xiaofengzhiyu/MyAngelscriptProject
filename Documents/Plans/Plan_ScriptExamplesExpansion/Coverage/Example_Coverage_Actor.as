// Example_Coverage_Actor.as
// Coverage test companion script — validates reflected properties,
// default statements, BeginPlay override, replication, actor tags,
// tick interval, and a helper UFUNCTION.

UCLASS()
class ACoverageExampleActor : AActor
{
	UPROPERTY()
	int Health = 125;

	UPROPERTY()
	FString DisplayName = "CoverageActor";

	UPROPERTY()
	bool bBeginPlayTriggered = false;

	default bReplicates = true;
	default Tags.Add(n"CoverageActor");
	default PrimaryActorTick.TickInterval = 0.25;

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
}
