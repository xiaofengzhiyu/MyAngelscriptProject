# AS Inline Code Formatting Rule

## Scope

This rule governs the formatting of **AngelScript code embedded in C++ test files** via raw string literals. It applies to all files under:

- `Plugins/Angelscript/Source/AngelscriptTest/`
- `Plugins/Angelscript/Source/AngelscriptRuntime/Tests/`
- `Plugins/Angelscript/Source/AngelscriptEditor/Tests/`

Any new or modified inline AS code must conform to this rule.

## Rule Summary

| Aspect | Requirement |
|--------|-------------|
| String delimiter | UCLASS scripts: `TEXT(R"AS(...)AS")`; global functions: `TEXT(R"(...)")` |
| Column origin | AS code starts at **column 0** (no indentation relative to C++ context) |
| Indentation | **Tab** (consistent with UE C++ style) |
| Brace style | **Allman** — always, including single-line function bodies |
| Function spacing | **One blank line** between every function/method |
| Property spacing | **One blank line** after each `UPROPERTY()` + declaration pair |
| Class spacing | **One blank line** between multiple `UCLASS` definitions |
| ASSDK tests | Must use raw string literals (no `"\n"` concatenation) |

## Detailed Rules

### 1. Raw String Delimiter

- Scripts containing `UCLASS()` / `UFUNCTION()` / `UPROPERTY()` use the `AS` delimiter to avoid conflict with `)` characters in AS code:
  ```cpp
  TEXT(R"AS(
  ...
  )AS")
  ```

- Scripts containing only global functions (no UE macros) may use the plain delimiter:
  ```cpp
  TEXT(R"(
  ...
  )")
  ```

- ASSDK-layer tests that previously used `"\n"` string concatenation must migrate to raw string literals.

### 2. Column Origin

AS code begins at column 0 regardless of the surrounding C++ indentation depth:

```cpp
        // C++ is indented deeply here
        UClass* ScriptClass = CompileScriptModule(*TestRunner, Engine, ModuleName,
            TEXT("MyScript.as"),
            TEXT(R"AS(
UCLASS()
class AMyActor : AActor
{
    ...
}
)AS"),
            TEXT("AMyActor"));
```

### 3. Indentation

- Use **Tab** characters (not spaces).
- Class members (property declarations, method signatures, UPROPERTY/UFUNCTION macros): **1 Tab**.
- Method body code: **2 Tabs**.
- Nested blocks (if/for/while): **+1 Tab** per nesting level.

### 4. Brace Style — Allman Only

The opening brace `{` always appears on its own line, aligned with the owning statement. **No K&R single-line exceptions**.

### 5. Blank Line Rules

- **Between functions** (global or member methods): always one blank line.
- **Between UPROPERTY groups**: one blank line after `UPROPERTY() + declaration`.
- **Between UFUNCTION + method and the next UFUNCTION**: one blank line after the closing `}`.
- **Between UCLASS definitions**: one blank line between the closing `}` of one class and the `UCLASS()` of the next.

### 6. Opening and Closing Lines

- The opening delimiter line (`TEXT(R"AS(` or `TEXT(R"(`) ends immediately; AS content starts on the next line.
- The closing delimiter (`)AS")` or `)")`) occupies its own line, separate from the last `}` of the AS code.

## Correct Examples

### UCLASS Script (standard pattern)

```cpp
TEXT(R"AS(
UCLASS()
class AMyTestActor : AActor
{
	UPROPERTY()
	int EventCount = 0;

	UPROPERTY()
	float TotalTime = 0.f;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		EventCount += 1;
	}

	UFUNCTION(BlueprintOverride)
	void Tick(float DeltaTime)
	{
		TotalTime += DeltaTime;
	}
}
)AS")
```

### Global Functions (binding tests)

```cpp
TEXT(R"(
int Add(int A, int B)
{
	return A + B;
}

int Multiply(int A, int B)
{
	return A * B;
}

FString BuildGreeting(const FString& in Name)
{
	return "Hello, " + Name + "!";
}
)")
```

### Multiple UCLASS Definitions

```cpp
TEXT(R"AS(
UCLASS()
class ABaseActor : AActor
{
	UFUNCTION()
	int GetValue()
	{
		return 1;
	}
}

UCLASS()
class ADerivedActor : ABaseActor
{
	UFUNCTION()
	int GetValue()
	{
		return 2;
	}
}
)AS")
```

### Nested Control Flow

```cpp
TEXT(R"AS(
UCLASS()
class AComplexActor : AActor
{
	UPROPERTY()
	int Result = 0;

	UFUNCTION(BlueprintOverride)
	void BeginPlay()
	{
		for (int i = 0; i < 10; ++i)
		{
			if (i % 2 == 0)
			{
				Result += i;
			}
		}
	}
}
)AS")
```

## Incorrect Examples

### Wrong: K&R Single-Line

```cpp
// WRONG — brace on same line as signature
int Add() { return 2 + 3; }
```

Must be:

```cpp
// CORRECT
int Add()
{
	return 2 + 3;
}
```

### Wrong: Missing Blank Line Between Functions

```cpp
// WRONG — no blank line separating functions
int Foo()
{
	return 1;
}
int Bar()
{
	return 2;
}
```

Must be:

```cpp
// CORRECT
int Foo()
{
	return 1;
}

int Bar()
{
	return 2;
}
```

### Wrong: AS Code Indented to Match C++ Context

```cpp
// WRONG — AS code indented to align with C++ nesting
        FCoverageModuleScope Mod(*TestRunner, Engine, Profile, TEXT("Test"), TEXT(R"(
        int GetValue()
        {
            return 42;
        }
        )"));
```

Must be:

```cpp
// CORRECT — AS code at column 0
        FCoverageModuleScope Mod(*TestRunner, Engine, Profile, TEXT("Test"), TEXT(R"(
int GetValue()
{
	return 42;
}
)"));
```

### Wrong: `\n` String Concatenation (ASSDK Legacy)

```cpp
// WRONG — old \n concatenation style
"int Multiply(int A, int B)  \n"
"{                           \n"
"  return A * B;             \n"
"}                           \n"
```

Must be migrated to raw string literal format.

## Enforcement

- This rule is enforced for all new and modified inline AS code.
- Existing code that does not conform should be updated when the containing TEST_METHOD is modified for other reasons (opportunistic cleanup).
- AI agents must apply this rule automatically when generating or editing inline AS code in test files.
