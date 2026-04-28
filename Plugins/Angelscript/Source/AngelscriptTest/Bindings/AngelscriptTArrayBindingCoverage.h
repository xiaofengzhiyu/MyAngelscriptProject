#pragma once

#include "CoreMinimal.h"

class FAutomationTestBase;
struct FAngelscriptEngine;

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTest_Bindings_TArrayCoverage
{
	enum class EArraySyntaxCoverage : uint8
	{
		ExplicitTArray,
		ShorthandArray,
	};

	bool RunTArrayBindingCoverageSections(
		FAutomationTestBase& Test,
		FAngelscriptEngine& Engine,
		EArraySyntaxCoverage Syntax);
}

#endif
