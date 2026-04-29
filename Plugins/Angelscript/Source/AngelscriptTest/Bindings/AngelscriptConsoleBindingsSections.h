#pragma once

#include "AngelscriptEngine.h"
#include "Misc/AutomationTest.h"
#include "Shared/AngelscriptBindingsCoverage.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace AngelscriptTestBindings
{
	const FBindingsCoverageProfile& GetConsoleBindingsProfile();

	bool RunConsoleVariableTypesSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleVariableExistingSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleVariableIdentitySection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandBasicSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandArgumentEmptySection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandArgumentContentSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandReplacementSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandLifecycleSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandMissingHandlerSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleCommandWrongSignatureSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
	bool RunConsoleLeakSelfCheckSection(FAutomationTestBase& Test, FAngelscriptEngine& Engine, const FBindingsCoverageProfile& Profile);
}

#endif
