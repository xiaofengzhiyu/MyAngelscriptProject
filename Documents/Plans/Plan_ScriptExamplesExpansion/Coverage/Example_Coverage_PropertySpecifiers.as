// Example_Coverage_PropertySpecifiers.as
// Coverage test companion script — validates UPROPERTY specifiers:
// Category, NotEditable, EditConst, BlueprintReadOnly, EditDefaultsOnly,
// ClampMin/ClampMax, EditCondition, InlineEditConditionToggle, MakeEditWidget,
// DefaultComponent, RootComponent, and Attach hierarchy.

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
	UPROPERTY(Category = "Coverage|Property")
	float CategorizedFloat = 0.0;

	UPROPERTY(NotEditable)
	bool bHiddenToggle = false;

	UPROPERTY(EditConst)
	bool bLockedToggle = false;

	UPROPERTY(BlueprintReadOnly)
	bool bBlueprintReadable = false;

	UPROPERTY(EditDefaultsOnly)
	int DefaultsOnlyValue = 0;

	UPROPERTY(meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ClampedValue = 0.5;

	UPROPERTY(meta = (InlineEditConditionToggle))
	bool bCanEditConditional = false;

	UPROPERTY(meta = (EditCondition = "bCanEditConditional"))
	float ConditionalFloat = 0.0;

	UPROPERTY(meta = (MakeEditWidget))
	FVector WidgetEditableVector;

	UPROPERTY(DefaultComponent, RootComponent)
	UCoveragePropertyRootComponent ScriptedRoot;

	UPROPERTY(DefaultComponent, Attach = ScriptedRoot)
	UCoveragePropertyBillboardComponent AttachedBillboard;
}
