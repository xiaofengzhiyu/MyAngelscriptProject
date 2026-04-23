// Example_Coverage_UObject.as
// Coverage test companion script — validates reflected defaults
// and a helper UFUNCTION that reads state.

UCLASS()
class UCoverageExampleObject : UObject
{
	UPROPERTY()
	int Counter = 9;

	UPROPERTY()
	FString ObjectLabel = "CoverageObject";

	UFUNCTION()
	int ComputeMarker()
	{
		return Counter + 5;
	}
}
