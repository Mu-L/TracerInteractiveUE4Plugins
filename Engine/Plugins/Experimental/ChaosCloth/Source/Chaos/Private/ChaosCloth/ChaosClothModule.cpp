// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosCloth/ChaosClothModule.h"
#include "ChaosCloth/ChaosClothPrivate.h"
#include "ChaosCloth/ChaosClothingSimulationFactory.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

//////////////////////////////////////////////////////////////////////////
// FChaosClothModule

class FChaosClothModule : public IChaosClothModuleInterface, public IClothingSimulationFactoryClassProvider
{
  public:
    virtual void StartupModule() override
    {
        check(GConfig);
#if WITH_CHAOS
		IModularFeatures::Get().RegisterModularFeature(IClothingSimulationFactoryClassProvider::FeatureName, this);
#endif
    }

    virtual void ShutdownModule() override
    {
#if WITH_CHAOS
		IModularFeatures::Get().UnregisterModularFeature(IClothingSimulationFactoryClassProvider::FeatureName, this);
#endif
    }

	TSubclassOf<UClothingSimulationFactory> GetClothingSimulationFactoryClass() const override
	{
#if WITH_CHAOS
		return UChaosClothingSimulationFactory::StaticClass();
#endif
		return nullptr;
	}
};

//////////////////////////////////////////////////////////////////////////

IMPLEMENT_MODULE(FChaosClothModule, ChaosCloth);
DEFINE_LOG_CATEGORY(LogChaosCloth);
