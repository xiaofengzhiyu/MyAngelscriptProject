#include "AngelscriptBlueprintCallableReflectiveFallbackTestTypes.h"

int32 UAngelscriptBlueprintCallableReflectiveFallbackTestLibrary::EligibleCallable(int32 Value)
{
	return Value;
}

int32 UAngelscriptBlueprintCallableReflectiveFallbackTestLibrary::TooManyArgumentsCallable(
	int32 Arg00,
	int32 Arg01,
	int32 Arg02,
	int32 Arg03,
	int32 Arg04,
	int32 Arg05,
	int32 Arg06,
	int32 Arg07,
	int32 Arg08,
	int32 Arg09,
	int32 Arg10,
	int32 Arg11,
	int32 Arg12,
	int32 Arg13,
	int32 Arg14,
	int32 Arg15,
	int32 Arg16)
{
	return Arg00 + Arg01 + Arg02 + Arg03 + Arg04 + Arg05 + Arg06 + Arg07 + Arg08
		+ Arg09 + Arg10 + Arg11 + Arg12 + Arg13 + Arg14 + Arg15 + Arg16;
}
