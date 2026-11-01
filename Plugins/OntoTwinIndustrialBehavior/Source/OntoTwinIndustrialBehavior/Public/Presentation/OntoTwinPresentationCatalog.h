#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeatures.h"

/**
 * A logical resource exposed to Nexus by an installed presentation package.
 *
 * The mock industrial package deliberately stores a logical asset reference
 * instead of a /Game path.  A real package can replace the reference with a
 * Primary Asset or adapter binding without changing the Nexus contract.
 */
struct ONTOTWININDUSTRIALBEHAVIOR_API FIndustrialPresentationCatalogEntry
{
    FString ResourceId;
    int32 Revision = 1;
    FString DisplayName;
    FString ResourceType;
    FString Channel;
    FString Slot;
    TArray<FString> SupportedStates;
    TArray<FString> SupportedObjectTypes;
    FString LogicalAssetReference;
    FString PreviewKind;
    FString Status = TEXT("published");
};

/**
 * Optional provider for a package's user-facing presentation catalog.
 * OntoTwinSync can discover providers through modular features while the
 * executor remains responsible for applying an already selected route.
 */
class ONTOTWININDUSTRIALBEHAVIOR_API IOntoTwinPresentationCatalogProvider
    : public IModularFeature
{
public:
    static FName GetModularFeatureName()
    {
        static const FName Name(TEXT("OntoTwinPresentationCatalogProvider"));
        return Name;
    }

    virtual FString GetPresentationCatalogId() const = 0;
    virtual FString GetPresentationCatalogVersion() const = 0;
    virtual void GetPresentationCatalog(
        TArray<FIndustrialPresentationCatalogEntry>& OutEntries) const = 0;
};
