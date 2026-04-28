#include "ClassGenerator/AngelscriptASFunctionArgumentLifetimeTestTypes.h"

int32 FAngelscriptASFunctionArgumentLifetimeFixture::DestructorCount = 0;

FAngelscriptASFunctionArgumentLifetimeFixture::~FAngelscriptASFunctionArgumentLifetimeFixture()
{
	++DestructorCount;
}

void FAngelscriptASFunctionArgumentLifetimeFixture::ResetCounters()
{
	DestructorCount = 0;
}
