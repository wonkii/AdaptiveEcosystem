#include "Player/EcoHandFoodComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Creature/Runtime/EcoCreatureIntegrationSpawner.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"
#include "Player/EcoAimComponent.h"
#include "Player/EcoEquipmentComponent.h"
#include "Player/EcoMeleeHitComponent.h"
#include "Player/EcoPlayerState.h"
#include "TimerManager.h"

UEcoHandFoodComponent::UEcoHandFoodComponent()
{
    SetIsReplicatedByDefault(true);
}

void UEcoHandFoodComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UEcoHandFoodComponent, HeldFood);
}

void UEcoHandFoodComponent::BeginPlay()
{
    Super::BeginPlay();
    RefreshVisual();
}

AEcoPlayerState* UEcoHandFoodComponent::GetFoodOwner() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    return Pawn ? Pawn->GetPlayerState<AEcoPlayerState>() : nullptr;
}

bool UEcoHandFoodComponent::ActorIsHoldingFood(const AActor* Actor)
{
    const auto* Food = Actor ? Actor->FindComponentByClass<UEcoHandFoodComponent>() : nullptr;
    return Food && Food->IsHoldingFood();
}

bool UEcoHandFoodComponent::CanOffer() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* State = GetFoodOwner();
    return Pawn && Pawn->HasAuthority() && Pawn->IsPlayerControlled()
        && State && State->GetPawn() == Pawn && State->IsAlive() && State->HandFoodCount > 0
        && HeldFood && HeldFood == State->HandFood && FMath::IsFinite(HeldFood->UnitsPerBite)
        && HeldFood->UnitsPerBite > 0 && !HandOffsetCm.ContainsNaN();
}

FVector UEcoHandFoodComponent::GetHandAnchor() const
{
    return GetOwner()->GetActorTransform().TransformPositionNoScale(HandOffsetCm);
}

void UEcoHandFoodComponent::EquipHandFood()
{
    ServerSetFoodEquipped(true);
}

void UEcoHandFoodComponent::StowHandFood()
{
    ServerSetFoodEquipped(false);
}

void UEcoHandFoodComponent::RequestHandFeed()
{
    ServerRequestHandFeed();
}

void UEcoHandFoodComponent::ServerSetFoodEquipped_Implementation(bool bEquip)
{
    if (!bEquip)
    {
        ClearHeldFood();
        return;
    }
    if (HeldFood)
        return;
    const auto* State = GetFoodOwner();
    const auto* Character = Cast<ACharacter>(GetOwner());
    if (!State || !Character || !Character->IsPlayerControlled() || State->GetPawn() != Character
        || !State->IsAlive() || State->HandFoodCount <= 0 || !State->HandFood || State->HandFood->FoodId.IsNone()
        || !State->HandFood->Mesh || !FMath::IsFinite(State->HandFood->UnitsPerBite) || State->HandFood->UnitsPerBite <= 0
        || FoodGripSocket.IsNone() || !Character->GetMesh() || !Character->GetMesh()->DoesSocketExist(FoodGripSocket)
        || VisualOffset.ContainsNaN() || HandOffsetCm.ContainsNaN())
    {
        UE_LOG(LogTemp, Warning, TEXT("[EcoHandFood] Equip rejected for %s. Check food asset, quantity and grip socket %s."),
            *GetNameSafe(GetOwner()), *FoodGripSocket.ToString());
        return;
    }

    HeldFood = State->HandFood;
    ++OfferGeneration;
    if (auto* Aim = GetOwner()->FindComponentByClass<UEcoAimComponent>())
        Aim->SetAiming(false);
    if (auto* Melee = GetOwner()->FindComponentByClass<UEcoMeleeHitComponent>())
        Melee->ResetAttack();
    RefreshVisual();
    GetOwner()->ForceNetUpdate();
}

void UEcoHandFoodComponent::ClearHeldFood()
{
    if (!GetOwner()->HasAuthority() || !HeldFood)
        return;
    HeldFood = nullptr;
    ++OfferGeneration;
    RefreshVisual();
    GetOwner()->ForceNetUpdate();
}

void UEcoHandFoodComponent::RefreshVisual()
{
    if (const auto* Equipment = GetOwner()->FindComponentByClass<UEcoEquipmentComponent>())
        Equipment->RefreshWeaponVisibility();
    if (GetNetMode() == NM_DedicatedServer)
        return;
    const auto* Character = Cast<ACharacter>(GetOwner());
    auto* Mesh = Character ? Character->GetMesh() : nullptr;
    if (!HeldFood || !Mesh || !Mesh->DoesSocketExist(FoodGripSocket))
    {
        if (FoodVisual)
            FoodVisual->SetVisibility(false);
        return;
    }
    if (!FoodVisual)
    {
        FoodVisual = NewObject<UStaticMeshComponent>(GetOwner());
        FoodVisual->SetIsReplicated(false);
        FoodVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FoodVisual->SetGenerateOverlapEvents(false);
        FoodVisual->SetupAttachment(Mesh, FoodGripSocket);
        FoodVisual->RegisterComponent();
    }
    FoodVisual->SetStaticMesh(HeldFood->Mesh);
    FoodVisual->SetRelativeTransform(VisualOffset);
    FoodVisual->SetVisibility(true);
}

void UEcoHandFoodComponent::ServerRequestHandFeed_Implementation()
{
    const double Now = GetWorld()->GetTimeSeconds();
    if (Now < NextRequestTime)
        return;
    NextRequestTime = Now + 0.1; // Bound RPC work independently of simulation speed.
    EEcoHandFeedResult Result = EEcoHandFeedResult::Unavailable;
    if (CanOffer())
        for (TActorIterator<AEcoCreatureIntegrationSpawner> It(GetWorld()); It; ++It)
            if (It->IsPlayerInteractionEnabled())
            {
                Result = It->QueueHandFeed(*this);
                break;
            }
    if (Result != EEcoHandFeedResult::None)
    {
        FEcoHandFeedReceipt Receipt;
        Receipt.Result = Result;
        ReportFeedResult(Receipt);
    }
}

void UEcoHandFoodComponent::ConsumeReservedBite(int64 Step)
{
    auto* State = GetFoodOwner();
    --State->HandFoodCount;
    State->ForceNetUpdate();
    NextFeedStep = Step + FMath::Max(0, CooldownSteps);
    if (State->HandFoodCount == 0)
        ClearHeldFood();
}

void UEcoHandFoodComponent::ReportFeedResult(const FEcoHandFeedReceipt& Receipt)
{
    // The C4 hook runs on the game thread during Mass processing. Publish to Blueprint
    // after processing so callbacks cannot invalidate borrowed fragment addresses.
    GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Receipt]
    {
        if (const auto* Pawn = Cast<APawn>(GetOwner()); Pawn && Pawn->IsPlayerControlled())
        {
            if (Pawn->IsLocallyControlled())
                OnFeedCompleted.Broadcast(Receipt);
            else
                ClientFeedResult(Receipt);
        }
    }));
}

void UEcoHandFoodComponent::ClientFeedResult_Implementation(const FEcoHandFeedReceipt& Receipt)
{
    OnFeedCompleted.Broadcast(Receipt);
}
