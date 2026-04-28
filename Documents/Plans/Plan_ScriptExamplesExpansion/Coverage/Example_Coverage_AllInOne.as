// ============================================================================
// SECTION: Global Helpers
// ============================================================================

enum ECoverageAllInOneEnum
{
	Zero,
	One,
	Two,
};

struct FCoverageAllInOneStruct
{
	UPROPERTY()
	int StructIntValue = 7;

	UPROPERTY()
	FString StructLabel = "CoverageStruct";

	UPROPERTY()
	FVector StructVector = FVector(1.0, 2.0, 3.0);
};

UINTERFACE()
interface UCoverageAllInOneInterface
{
	void TakeCoverageSignal(float Amount);
}

delegate void FCoverageAllInOneDelegate(UObject Object, float Value);
event void FCoverageAllInOneEvent(UObject Object, float Value);

UFUNCTION(Category = "Coverage|Global")
void CoverageAllInOneGlobalNoOp()
{
}

UFUNCTION(Category = "Coverage|Global")
int CoverageAllInOneGlobalAdd(int A, int B)
{
	return A + B;
}

UFUNCTION(Category = "Coverage|Global")
int CoverageAllInOneGlobalSubtract(int A, int B)
{
	return A - B;
}

UFUNCTION(Category = "Coverage|Global")
int CoverageAllInOneGlobalMultiply(int A, int B)
{
	return A * B;
}

UFUNCTION(Category = "Coverage|Global")
float CoverageAllInOneGlobalDivide(float A, float B)
{
	if (B == 0.0f)
	{
		return 0.0f;
	}

	return A / B;
}

UFUNCTION(Category = "Coverage|Global")
void CoverageAllInOneBuildVector(float X, float Y, float Z, FVector&out OutVector)
{
	OutVector = FVector(X, Y, Z);
}

// ============================================================================
// SECTION: Support Types
// ============================================================================

UCLASS()
class UCoverageAllInOneRootComponent : USceneComponent
{
}

UCLASS()
class UCoverageAllInOneMarkerComponent : UBillboardComponent
{
}

UCLASS()
class UCoverageAllInOneObject : UObject
{
	UPROPERTY(Category = "Coverage|Object")
	int Counter = 9;

	UPROPERTY(BlueprintReadOnly, Category = "Coverage|Object")
	FString ObjectLabel = "CoverageAllInOneObject";

	UFUNCTION(Category = "Coverage|Object")
	int ComputeMarker()
	{
		return Counter + 5;
	}
}

UCLASS()
class UCoverageAllInOneRuntimeComponent : UAngelscriptComponent
{
	UPROPERTY()
	bool bReady = false;

	UPROPERTY()
	int TickCount = 0;

	UPROPERTY()
	int ReadOwnerLoop = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		bReady = true;

		ACoverageAllInOneActor OwnerActor = Cast<ACoverageAllInOneActor>(GetOwner());
		if (OwnerActor != null)
		{
			ReadOwnerLoop = OwnerActor.Loop;
		}
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaSeconds)
	{
		TickCount += 1;
	}

	UFUNCTION(BlueprintOverride)
	void EndPlay(EEndPlayReason Reason)
	{
		bReady = false;
	}
}

// ============================================================================
// SECTION: Main Actor Coverage Surface
// ============================================================================

UCLASS()
class ACoverageAllInOneActor : AActor, UCoverageAllInOneInterface
{
	UPROPERTY(DefaultComponent, RootComponent)
	UCoverageAllInOneRootComponent RootScene;

	UPROPERTY(DefaultComponent, Attach = RootScene)
	UCoverageAllInOneMarkerComponent MarkerBillboard;

	UPROPERTY(DefaultComponent)
	UCoverageAllInOneRuntimeComponent RuntimeComponent;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	bool bBoolValue_Default = true;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	bool bBoolValue_DefaultFalse = false;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	int8 Int8Value_Default = 1;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	int16 Int16Value_Default = 2;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	int32 Int32Value_Default = 3;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	int64 Int64Value_Default = 4;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	uint8 UInt8Value_Default = 5;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	uint16 UInt16Value_Default = 6;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	uint32 UInt32Value_Default = 7;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	uint64 UInt64Value_Default = 8;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	float FloatValue_Default = 1.5f;

	UPROPERTY(Category = "Coverage|Property|Primitive")
	double DoubleValue_Default = 2.5;

	UPROPERTY(Category = "Coverage|Property|Text")
	FString StringValue_Default = "CoverageString";

	UPROPERTY(Category = "Coverage|Property|Text")
	FName NameValue_Default = n"CoverageName";

	UPROPERTY(Category = "Coverage|Property|Text")
	FText TextValue_Default;

	UPROPERTY(Category = "Coverage|Property|Complex")
	FVector VectorValue_Default = FVector(10.0, 20.0, 30.0);

	UPROPERTY(Category = "Coverage|Property|Complex")
	ECoverageAllInOneEnum EnumValue_Default = ECoverageAllInOneEnum::One;

	UPROPERTY(Category = "Coverage|Property|Complex")
	FCoverageAllInOneStruct StructValue_Default;

	UPROPERTY(Category = "Coverage|Property|Complex")
	ACoverageAllInOneActor ActorReference_Default;

	UPROPERTY(Category = "Coverage|Property|Complex")
	UCoverageAllInOneObject HelperObject_Default;

	UPROPERTY(Category = "Coverage|Property|Complex")
	TSubclassOf<AActor> ActorClassType_Default;

	UPROPERTY(Category = "Coverage|Property|Container")
	TArray<int32> IntArrayValues_Default;

	UPROPERTY(Category = "Coverage|Property|Container")
	TSet<int32> IntSetValues_Default;

	UPROPERTY(Category = "Coverage|Property|Container")
	TMap<int32, int32> IntMapValues_Default;

	UPROPERTY(NotEditable, Category = "Coverage|Property|Flags")
	bool bBoolValue_NotEditable = true;

	UPROPERTY(EditConst, Category = "Coverage|Property|Flags")
	bool bBoolValue_EditConst = false;

	UPROPERTY(BlueprintReadOnly, Category = "Coverage|Property|Flags")
	bool bBoolValue_BlueprintReadOnly = false;

	UPROPERTY(EditDefaultsOnly, Category = "Coverage|Property|Flags")
	bool bBoolValue_EditDefaultsOnly = true;

	UPROPERTY(EditDefaultsOnly, Category = "Coverage|Property|Flags")
	float FloatValue_EditDefaultsOnly = 2.5f;

	UPROPERTY(meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float FloatValue_ClampMinMax = 0.5f;

	UPROPERTY(meta = (EditCondition = "bBoolValue_InlineEditConditionToggle"))
	float FloatValue_EditConditionDriven = 3.0f;

	UPROPERTY(meta = (InlineEditConditionToggle), Category = "Coverage|Property|Flags")
	bool bBoolValue_InlineEditConditionToggle = true;

	UPROPERTY(meta = (EditCondition = "bBoolValue_InlineEditConditionToggle"), Category = "Coverage|Property|Flags")
	bool bBoolValue_EditConditionDriven = false;

	UPROPERTY(meta = (MakeEditWidget))
	FVector VectorValue_MakeEditWidget = FVector(100.0, 0.0, 50.0);

	UPROPERTY(Category = "Coverage|State")
	bool bBeginPlayTriggered = false;

	UPROPERTY(Category = "Coverage|State")
	bool bEndPlayTriggered = false;

	UPROPERTY(Category = "Coverage|State")
	bool bImplementsCoverageInterface = false;

	UPROPERTY(Category = "Coverage|State")
	int TickInvocationCount = 0;

	UPROPERTY(Category = "Coverage|State")
	int EventInvocationCount = 0;

	UPROPERTY(Category = "Coverage|Benchmark")
	int Loop = 16;

	UPROPERTY(Category = "Coverage|Benchmark")
	TArray<FString> Names;

	UPROPERTY(Category = "Coverage|Benchmark")
	TArray<double> Checksums;

	UPROPERTY(Category = "Coverage|Benchmark")
	TArray<FString> BenchmarkNames;

	UPROPERTY(Category = "Coverage|Benchmark")
	TArray<int32> BenchmarkIterations;

	UPROPERTY(Category = "Coverage|Benchmark")
	TArray<double> BenchmarkChecksums;

	UPROPERTY(Category = "Coverage|Benchmark")
	FString CsvOutput;

	UPROPERTY(Category = "Coverage|Benchmark")
	FCoverageAllInOneEvent CoverageEvent;

	UPROPERTY(Category = "Coverage|Benchmark")
	FCoverageAllInOneDelegate CoverageDelegate;

	default bReplicates = true;
	default PrimaryActorTick.TickInterval = 0.25f;
	default Tags.Add(n"CoverageAllInOne");

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		bBeginPlayTriggered = true;
		TextValue_Default = FText::FromString("CoverageText");
		bBoolValue_BlueprintReadOnly = true;
		ActorClassType_Default = ACoverageAllInOneActor::StaticClass();

		InitializeContainerValues();
		BindCoverageDelegates();
		RunBenchmarkSuite();
		TriggerCoverageEvent();

		if (this.ImplementsInterface(UCoverageAllInOneInterface::StaticClass()))
		{
			bImplementsCoverageInterface = true;
		}
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaSeconds)
	{
		TickInvocationCount += 1;
	}

	UFUNCTION(BlueprintOverride)
	void EndPlay(EEndPlayReason Reason)
	{
		bEndPlayTriggered = true;
	}

	UFUNCTION(BlueprintPure, Category = "Coverage|Functions")
	bool IsCoverageReady()
	{
		return bBeginPlayTriggered && bBoolValue_BlueprintReadOnly;
	}

	UFUNCTION(BlueprintEvent, Category = "Coverage|Functions")
	void OnCoverageBlueprintEvent()
	{
		EventInvocationCount += 1;
	}

	UFUNCTION(NotBlueprintCallable, Category = "Coverage|Functions")
	void HiddenUtility()
	{
		TickInvocationCount += 10;
	}

	UFUNCTION(CallInEditor, Category = "Coverage|Functions")
	void RebuildCoverageData()
	{
		InitializeContainerValues();
	}

	UFUNCTION(Category = "Coverage|Functions")
	int AddValues(int A, int B)
	{
		return A + B;
	}

	UFUNCTION(Category = "Coverage|Functions")
	void BuildStructAndVector(int InInt, FString InLabel, FVector&out OutVector)
	{
		StructValue_Default.StructIntValue = InInt;
		StructValue_Default.StructLabel = InLabel;
		OutVector = FVector(InInt, InInt + 1, InInt + 2);
	}

	UFUNCTION(Category = "Coverage|Functions")
	void SetBoolValue(bool InValue)
	{
		bBoolValue_Default = InValue;
	}

	UFUNCTION(BlueprintPure, Category = "Coverage|Functions")
	bool GetBoolValue()
	{
		return bBoolValue_Default;
	}

	UFUNCTION(Category = "Coverage|Functions")
	void SetStructValue(FCoverageAllInOneStruct InValue)
	{
		StructValue_Default = InValue;
	}

	UFUNCTION(BlueprintPure, Category = "Coverage|Functions")
	FCoverageAllInOneStruct GetStructValue()
	{
		return StructValue_Default;
	}

	UFUNCTION(Category = "Coverage|Delegates")
	void HandleCoverageEvent(UObject InObject, float InValue)
	{
		EventInvocationCount += int(InValue);
	}

	UFUNCTION(Category = "Coverage|Interface")
	void TakeCoverageSignal(float Amount)
	{
		FloatValue_Default += Amount;
	}

	void InitializeContainerValues()
	{
		IntArrayValues_Default.Empty();
		IntArrayValues_Default.Add(1);
		IntArrayValues_Default.Add(2);
		IntArrayValues_Default.Add(3);
		IntArrayValues_Default.Add(4);

		IntSetValues_Default.Empty();
		IntSetValues_Default.Add(10);
		IntSetValues_Default.Add(20);
		IntSetValues_Default.Add(30);

		IntMapValues_Default.Empty();
		IntMapValues_Default.Add(0, 100);
		IntMapValues_Default.Add(1, 200);
		IntMapValues_Default.Add(2, 300);
	}

	void BindCoverageDelegates()
	{
		CoverageDelegate.BindUFunction(this, n"HandleCoverageEvent");
		CoverageEvent.AddUFunction(this, n"HandleCoverageEvent");
	}

	void TriggerCoverageEvent()
	{
		if (CoverageDelegate.IsBound())
		{
			CoverageDelegate.Execute(this, 3.0f);
		}

		CoverageEvent.Broadcast(this, 2.0f);
		OnCoverageBlueprintEvent();
		TakeCoverageSignal(1.0f);
	}

	void StartTest()
	{
		Names.Empty();
		Checksums.Empty();
		BenchmarkNames.Empty();
		BenchmarkIterations.Empty();
		BenchmarkChecksums.Empty();
		CsvOutput = "Name,Iterations,Checksum\n";
	}

	void EndTest(FString SampleName, int Iterations, double Checksum)
	{
		Names.Add(SampleName);
		Checksums.Add(Checksum);
		BenchmarkNames.Add(SampleName);
		BenchmarkIterations.Add(Iterations);
		BenchmarkChecksums.Add(Checksum);
		CsvOutput += SampleName + "," + Iterations + "," + Checksum + "\n";
	}

	void EmptyOperation()
	{
	}

	int MemberAddOperation(int A, int B)
	{
		return A + B;
	}

	void RunBenchmarkSuite()
	{
		StartTest();
		RunEmptyOperationBenchmark();
		RunGlobalAddBenchmark();
		RunGlobalSubtractBenchmark();
		RunGlobalMultiplyBenchmark();
		RunGlobalDivideBenchmark();
		RunMemberAddBenchmark();
		RunDirectBoolBenchmark();
		RunGetterSetterBoolBenchmark();
		RunArrayElementBenchmark();
		RunMapElementBenchmark();
	}

	void RunEmptyOperationBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			EmptyOperation();
			Checksum += Index;
		}
		EndTest("EmptyOperation", Loop, Checksum);
	}

	void RunGlobalAddBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			Checksum += CoverageAllInOneGlobalAdd(Index, 2);
		}
		EndTest("GlobalAdd", Loop, Checksum);
	}

	void RunGlobalSubtractBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			Checksum += CoverageAllInOneGlobalSubtract(Index + 5, 2);
		}
		EndTest("GlobalSubtract", Loop, Checksum);
	}

	void RunGlobalMultiplyBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			Checksum += CoverageAllInOneGlobalMultiply(Index, 2);
		}
		EndTest("GlobalMultiply", Loop, Checksum);
	}

	void RunGlobalDivideBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 1; Index <= Loop; ++Index)
		{
			Checksum += CoverageAllInOneGlobalDivide(Index * 2.0f, 2.0f);
		}
		EndTest("GlobalDivide", Loop, Checksum);
	}

	void RunMemberAddBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			Checksum += MemberAddOperation(Index, 3);
		}
		EndTest("MemberAdd", Loop, Checksum);
	}

	void RunDirectBoolBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			bBoolValue_Default = (Index % 2) == 0;
			Checksum += bBoolValue_Default ? 1.0 : 0.0;
		}
		EndTest("DirectBool", Loop, Checksum);
	}

	void RunGetterSetterBoolBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			SetBoolValue((Index % 2) == 1);
			Checksum += GetBoolValue() ? 1.0 : 0.0;
		}
		EndTest("GetterSetterBool", Loop, Checksum);
	}

	void RunArrayElementBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			const int ArrayIndex = Index % IntArrayValues_Default.Num();
			IntArrayValues_Default[ArrayIndex] = IntArrayValues_Default[ArrayIndex] + 1;
			Checksum += IntArrayValues_Default[ArrayIndex];
		}
		EndTest("ArrayElement", Loop, Checksum);
	}

	void RunMapElementBenchmark()
	{
		double Checksum = 0.0;
		for (int Index = 0; Index < Loop; ++Index)
		{
			const int Key = Index % 3;
			IntMapValues_Default[Key] = IntMapValues_Default[Key] + 1;
			Checksum += IntMapValues_Default[Key];
		}
		EndTest("MapElement", Loop, Checksum);
	}
}
