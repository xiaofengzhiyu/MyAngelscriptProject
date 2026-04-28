UCLASS()
class UCoveragePropertyRootComponent : USceneComponent
{
}

UCLASS()
class UCoveragePropertyBillboardComponent : UBillboardComponent
{
}

UCLASS()
class ACoveragePropertySpecifierActor : AActor
{
	UPROPERTY(DefaultComponent, RootComponent)
	UCoveragePropertyRootComponent RootScene;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UCoveragePropertyBillboardComponent MarkerBillboard;

	UPROPERTY(Category = "Coverage|Property")
	float CategorizedFloat = 1337.0f;

	UPROPERTY(NotEditable)
	bool bHiddenToggle = true;

	UPROPERTY(EditConst)
	bool bLockedToggle = false;

	UPROPERTY(BlueprintReadOnly, Category = "Coverage|Property")
	bool bBlueprintReadable = true;

	UPROPERTY(EditDefaultsOnly, Category = "Coverage|Property")
	float DefaultsOnlyValue = 2.5f;

	UPROPERTY(meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float ClampedValue = 0.5f;

	UPROPERTY(meta = (EditCondition = "bCanEditConditional"))
	float ConditionalFloat = 3.0f;

	UPROPERTY(meta = (InlineEditConditionToggle))
	bool bCanEditConditional = true;

	UPROPERTY(meta = (MakeEditWidget))
	FVector WidgetEditableVector = FVector(10.0f, 20.0f, 30.0f);
}
