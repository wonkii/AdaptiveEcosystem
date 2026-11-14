#pragma once

#include "Components/ActorComponent.h"
#include "Core/EcoPlayerInteractionTypes.h"
#include "EcoHandFoodComponent.generated.h"

class UStaticMeshComponent;
class AEcoPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEcoHandFeedCompleted, const FEcoHandFeedReceipt&, Receipt);

UCLASS(ClassGroup=(Ecology), meta=(BlueprintSpawnableComponent))
class ADAPTIVEECOSYSTEM_API UEcoHandFoodComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEcoHandFoodComponent();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    void BeginPlay() override;

    UPROPERTY(EditAnywhere, Category="Hand Food|Visual") FName FoodGripSocket = TEXT("socket_Food_R");
    UPROPERTY(EditAnywhere, Category="Hand Food|Visual") FTransform VisualOffset =
        FTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.12));
    UPROPERTY(EditAnywhere, Category="Hand Food|Contact") FVector HandOffsetCm = FVector(45, 25, 0);
    UPROPERTY(EditAnywhere, Category="Hand Food|Contact", meta=(ClampMin="1")) int32 RequestLifetimeSteps = 8;
    UPROPERTY(EditAnywhere, Category="Hand Food|Contact", meta=(ClampMin="0")) int32 CooldownSteps = 8;
    UPROPERTY(EditAnywhere, Category="Hand Food|Contact", meta=(ClampMin="0", ClampMax="180")) float TargetHalfAngleDegrees = 45;
    UPROPERTY(BlueprintAssignable, Category="Hand Food") FEcoHandFeedCompleted OnFeedCompleted;

    UFUNCTION(BlueprintCallable, Category="Hand Food") void EquipHandFood();
    UFUNCTION(BlueprintCallable, Category="Hand Food") void StowHandFood();
    UFUNCTION(BlueprintCallable, Category="Hand Food") void RequestHandFeed();
    UFUNCTION(BlueprintPure, Category="Hand Food") bool IsHoldingFood() const { return HeldFood != nullptr; }

    static bool ActorIsHoldingFood(const AActor* Actor);
    FVector GetHandAnchor() const;
    const UEcoHandFoodDefinition* GetHeldFood() const { return HeldFood; }
    uint32 GetOfferGeneration() const { return OfferGeneration; }
    bool CanOffer() const;
    void ClearHeldFood();
    void InvalidatePendingOffer() { ++OfferGeneration; }
    void ConsumeReservedBite(int64 Step);
    void ReportFeedResult(const FEcoHandFeedReceipt& Receipt);
    bool IsCoolingDown(int64 Step) const { return Step < NextFeedStep; }

private:
    AEcoPlayerState* GetFoodOwner() const;
    UFUNCTION(Server, Reliable) void ServerSetFoodEquipped(bool bEquip);
    UFUNCTION(Server, Reliable) void ServerRequestHandFeed();
    UFUNCTION(Client, Reliable) void ClientFeedResult(const FEcoHandFeedReceipt& Receipt);
    UFUNCTION() void RefreshVisual();
    UPROPERTY(ReplicatedUsing=RefreshVisual) TObjectPtr<UEcoHandFoodDefinition> HeldFood;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> FoodVisual;
    uint32 OfferGeneration = 0;
    int64 NextFeedStep = 0;
    double NextRequestTime = 0;
};
