#include "Core/AngelscriptTestModule.h"

#include "Preprocessor/AngelscriptPreprocessorTestHelpers.h"
#include "Shared/AngelscriptTestEnginePool.h"

#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_MODULE(FAngelscriptTestModule, AngelscriptTest);

DEFINE_LOG_CATEGORY_STATIC(LogAngelscriptTest, Log, All);

#if WITH_DEV_AUTOMATION_TESTS
// Definition for the log category declared in AngelscriptPreprocessorTestHelpers.h.
// Default verbosity is NoLogging; enable on demand via -LogCmds or
// LogPreprocessorDump.SetVerbosity(...) inside a TEST_METHOD.
DEFINE_LOG_CATEGORY(LogPreprocessorDump);
#endif

void FAngelscriptTestModule::StartupModule()
{
	const bool bPrewarmEngine = FParse::Param(FCommandLine::Get(), TEXT("AngelscriptTestPrewarmEngine"));
	AngelscriptTestSupport::StartupTestEnginePool(bPrewarmEngine);
	UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module started."));
}

void FAngelscriptTestModule::ShutdownModule()
{
	AngelscriptTestSupport::ShutdownTestEnginePool();
	UE_LOG(LogAngelscriptTest, Log, TEXT("AngelscriptTest module shut down."));
}
