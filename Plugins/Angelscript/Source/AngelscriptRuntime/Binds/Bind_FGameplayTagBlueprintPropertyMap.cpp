#include "AngelscriptBinds.h"

#include "Core/AngelscriptEngine.h"
#include "GameplayEffectTypes.h"

AS_FORCE_LINK const FAngelscriptBinds::FBind Bind_FGameplayTagBlueprintPropertyMap(FAngelscriptBinds::EOrder::Late, [] {
	auto FGameplayTagBlueprintPropertyMap_ = FAngelscriptBinds::ExistingClass("FGameplayTagBlueprintPropertyMap");
	FGameplayTagBlueprintPropertyMap_.Method("void Initialize(UObject Owner, UAbilitySystemComponent ASC)", [](FGameplayTagBlueprintPropertyMap* PropertyMap, UObject* Owner, UAbilitySystemComponent* ASC)
	{
		if (Owner == nullptr)
		{
			FAngelscriptEngine::Throw("GameplayTagBlueprintPropertyMap.Initialize received a null Owner.");
			return;
		}

		if (ASC == nullptr)
		{
			FAngelscriptEngine::Throw("GameplayTagBlueprintPropertyMap.Initialize received a null AbilitySystemComponent.");
			return;
		}

		PropertyMap->Initialize(Owner, ASC);
	});
	FGameplayTagBlueprintPropertyMap_.Method("void ApplyCurrentTags()", METHOD_TRIVIAL(FGameplayTagBlueprintPropertyMap, ApplyCurrentTags));
});
