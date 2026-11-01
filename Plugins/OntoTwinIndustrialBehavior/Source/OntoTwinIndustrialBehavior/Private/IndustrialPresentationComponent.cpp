#include "IndustrialPresentationComponent.h"
#include "TwinInstance.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

UIndustrialPresentationComponent::UIndustrialPresentationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

UStaticMeshComponent* UIndustrialPresentationComponent::CreateIndicator(FName Name)
{
    auto* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/OntoTwinIndustrialBehavior/Materials/M_Indicator.M_Indicator"));
    if (!Mesh || !Material || !GetOwner()->GetRootComponent()) return nullptr;
    auto* Indicator = NewObject<UStaticMeshComponent>(GetOwner(), Name, RF_Transient);
    Indicator->ComponentTags.Add(TEXT("OntoTwinPresentation"));
    Indicator->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Indicator->SetCastShadow(false);
    Indicator->SetupAttachment(GetOwner()->GetRootComponent());
    Indicator->SetStaticMesh(Mesh);
    Indicator->SetMaterial(0, UMaterialInstanceDynamic::Create(Material, Indicator));
    Indicator->SetRelativeScale3D(FVector(.28f, .045f, .045f));
    GetOwner()->AddInstanceComponent(Indicator);
    Indicator->RegisterComponent();
    return Indicator;
}

bool UIndustrialPresentationComponent::Apply(const FString& Channel, const FString& Behavior)
{
    if (Channel == TEXT("animation"))
    {
        if (!Motion && !Behavior.StartsWith(TEXT("safe."))) Motion = CreateIndicator(TEXT("OT_MotionIndicator"));
        Animation = Behavior;
        if (Motion) Motion->SetVisibility(Behavior == TEXT("industrial.machine.running"));
        return Motion != nullptr || Behavior.StartsWith(TEXT("safe."));
    }
    if (Channel == TEXT("fx"))
    {
        if (!Beacon && !Behavior.StartsWith(TEXT("safe."))) Beacon = CreateIndicator(TEXT("OT_AlarmBeacon"));
        Effect = Behavior;
        if (Beacon)
        {
            if (auto* Material = Cast<UMaterialInstanceDynamic>(Beacon->GetMaterial(0)))
                Material->SetVectorParameterValue(TEXT("Color"), Behavior.Contains(TEXT("critical"))
                    ? FLinearColor(1.f,.02f,.01f) : FLinearColor(1.f,.55f,.02f));
            Beacon->SetRelativeScale3D(FVector(.12f));
            Beacon->SetVisibility(!Behavior.StartsWith(TEXT("safe.")));
        }
        return Beacon != nullptr || Behavior.StartsWith(TEXT("safe."));
    }
    if (Channel == TEXT("visual"))
    {
        Visual = Behavior;
        return UpdateVisual();
    }
    if (Channel == TEXT("label"))
    {
        if (!StatusLabel)
        {
            StatusLabel = NewObject<UTextRenderComponent>(GetOwner(), TEXT("OT_StatusLabel"), RF_Transient);
            StatusLabel->SetupAttachment(GetOwner()->GetRootComponent());
            StatusLabel->SetWorldSize(18.f);
            StatusLabel->SetHorizontalAlignment(EHTA_Center);
            GetOwner()->AddInstanceComponent(StatusLabel);
            StatusLabel->RegisterComponent();
        }
        StatusLabel->SetText(FText::FromString(TEXT("DEMO")));
        StatusLabel->SetVisibility(!Behavior.StartsWith(TEXT("safe.")));
        return true;
    }
    return false;
}

bool UIndustrialPresentationComponent::UpdateVisual()
{
    if (Visual.IsEmpty() || Visual.StartsWith(TEXT("safe.")))
    {
        for (auto& Pair : PreviousOverlays)
            if (Pair.Key.IsValid() && Pair.Key->GetOverlayMaterial() == Overlay)
                Pair.Key->SetOverlayMaterial(Pair.Value.Get());
        PreviousOverlays.Reset();
        return true;
    }
    if (!Overlay)
    {
        auto* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/OntoTwinIndustrialBehavior/Materials/M_StatusOverlay.M_StatusOverlay"));
        if (!Base) return false;
        Overlay = UMaterialInstanceDynamic::Create(Base, this);
    }
    const FLinearColor Color = Visual.Contains(TEXT("critical")) ? FLinearColor(1.f,.02f,.01f)
        : Visual.Contains(TEXT("maintenance")) ? FLinearColor(.05f,.35f,1.f) : FLinearColor(1.f,.6f,.01f);
    Overlay->SetVectorParameterValue(TEXT("Color"), Color);
    TArray<UMeshComponent*> Meshes;
    GetOwner()->GetComponents<UMeshComponent>(Meshes, true);
    int32 Count = 0;
    for (auto* Mesh : Meshes)
    {
        // Indicator meshes and UI are not targets for the material channel.
        if (!Mesh || Mesh->ComponentHasTag(TEXT("OntoTwinPresentation")) || !Mesh->IsA<UStaticMeshComponent>()) continue;
        if (!PreviousOverlays.Contains(Mesh)) PreviousOverlays.Add(Mesh, Mesh->GetOverlayMaterial());
        Mesh->SetOverlayMaterial(Overlay);
        ++Count;
    }
    return Count > 0;
}

void UIndustrialPresentationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    Phase += DeltaTime;
    // Derive bounds only from model meshes, excluding our own indicators.
    FBox Box(ForceInit);
    TArray<UStaticMeshComponent*> Meshes;
    GetOwner()->GetComponents<UStaticMeshComponent>(Meshes, true);
    for (auto* Mesh : Meshes)
        if (!Mesh->ComponentHasTag(TEXT("OntoTwinPresentation"))) Box += Mesh->Bounds.GetBox();
    const FVector Top = Box.IsValid ? FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 20.f) : GetOwner()->GetActorLocation() + FVector(0,0,100);
    if (Motion)
    {
        Motion->SetWorldLocation(Top);
        if (Animation == TEXT("industrial.machine.running")) Motion->SetRelativeRotation(FRotator(0, Phase * 180.f, 0));
    }
    if (Beacon)
    {
        Beacon->SetWorldLocation(Top + FVector(0,0,20));
        Beacon->SetVisibility(!Effect.StartsWith(TEXT("safe.")) && FMath::Fmod(Phase, 1.f) < .5f);
    }
    if (StatusLabel) StatusLabel->SetWorldLocation(Top + FVector(0,0,40));
    // Models can complete an asynchronous GLB load after the route arrived.
    if (!Visual.IsEmpty()) UpdateVisual();
}

void UIndustrialPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    Visual = TEXT("safe.visible");
    UpdateVisual();
    Super::EndPlay(Reason);
}
