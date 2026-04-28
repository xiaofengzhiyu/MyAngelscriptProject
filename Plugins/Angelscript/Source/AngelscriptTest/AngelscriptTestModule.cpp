#include "Core/AngelscriptTestModule.h"

#include "Shared/AngelscriptTestEnginePool.h"

#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

IMPLEMENT_MODULE(FAngelscriptTestModule, AngelscriptTest);

DEFINE_LOG_CATEGORY_STATIC(LogAngelscriptTest, Log, All);

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
