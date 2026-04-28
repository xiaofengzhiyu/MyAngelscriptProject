UCLASS()
class UCoverageExampleObject : UObject
{
	UPROPERTY(Category = "Coverage|UObject")
	int Counter = 9;

	UPROPERTY(BlueprintReadOnly, Category = "Coverage|UObject")
	FString ObjectLabel = "CoverageObject";

	UFUNCTION()
	int ComputeMarker()
	{
		return Counter + 5;
	}
}
