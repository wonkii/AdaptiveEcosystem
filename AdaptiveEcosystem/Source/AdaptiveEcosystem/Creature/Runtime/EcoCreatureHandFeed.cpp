#include "Creature/Runtime/EcoCreatureIntegrationSpawner.h"

#include "AI/Policy/EcoPolicyV3IntegratedFragments.h"
#include "AI/Policy/EcoPolicyV3Fragments.h"
#include "AI/Policy/EcoPolicyV3IntegratedSubsystem.h"
#include "Ecology/EcologySimulationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Mass/EcoMassFragments.h"
#include "MassEntityManager.h"
#include "MassSpawnerSubsystem.h"
#include "Player/EcoHandFoodComponent.h"
#include "Player/EcoPlayerState.h"
#include "World/EcoWorldClockSubsystem.h"

bool AEcoCreatureIntegrationSpawner::IsPlayerInteractionEnabled() const
{
    return bReady && bUseV3Policy && bEnableEcosystemLife && GetNetMode() != NM_Client
        && !PlayerInteractionProfiles.IsEmpty();
}

const FEcoSpeciesPlayerInteractionProfile* AEcoCreatureIntegrationSpawner::FindPlayerInteractionProfile(FName SpeciesId) const
{
    const auto* Profile = !SpeciesId.IsNone() && IsPlayerInteractionEnabled() ? PlayerInteractionProfiles.Find(SpeciesId) : nullptr;
    return Profile && Profile->IsValid() ? Profile : nullptr;
}

bool AEcoCreatureIntegrationSpawner::ReadRegionalHumanReputation(FName RegionId, FName SpeciesId,
    FEcoRegionalHumanReputation& Reputation) const
{
    Reputation = {};
    const auto* Profile = FindPlayerInteractionProfile(SpeciesId);
    const auto* Ecology = GetWorld()->GetSubsystem<UEcologySimulationSubsystem>();
    const auto* Clock = GetWorld()->GetSubsystem<UEcoWorldClockSubsystem>();
    return Profile && Ecology && Clock
        && Ecology->ReadHumanReputation(Clock->GetServerTime().WorldEpoch, RegionId, SpeciesId, *Profile, Reputation);
}

EEcoHandFeedResult AEcoCreatureIntegrationSpawner::QueueHandFeed(UEcoHandFoodComponent& Source)
{
    if (!IsInGameThread() || !IsPlayerInteractionEnabled() || Source.GetWorld() != GetWorld() || !Source.CanOffer())
        return EEcoHandFeedResult::Unavailable;
    auto* Native = GetWorld()->GetSubsystem<UEcoPolicyV3IntegratedSubsystem>();
    auto* Spawner = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>();
    const auto* Clock = GetWorld()->GetSubsystem<UEcoWorldClockSubsystem>();
    if (!Native || !Native->IsActive() || !Spawner || !Clock || Spawner->GetEntityManagerChecked().IsProcessing())
        return EEcoHandFeedResult::Unavailable;
    const int64 NativeStep = Native->GetGlobalStep();
    const int32 Epoch = Clock->GetServerTime().WorldEpoch;
    if (Epoch <= 0)
        return EEcoHandFeedResult::Unavailable;
    CancelInvalidHandFeeds(Epoch, NativeStep);
    if (Source.IsCoolingDown(NativeStep))
        return EEcoHandFeedResult::Busy;
    for (const auto& Pair : HandFeedRequests)
        if (Pair.Value.ContainsByPredicate([&](const FHandFeedRequest& Request) { return Request.Source.Get() == &Source; }))
            return EEcoHandFeedResult::Busy;

    auto& Manager = Spawner->GetEntityManagerChecked();
    const auto* Pawn = CastChecked<APawn>(Source.GetOwner());
    FVector Eyes;
    FRotator ViewRotation;
    Pawn->GetActorEyesViewPoint(Eyes, ViewRotation);
    const FVector Forward = ViewRotation.Vector().GetSafeNormal2D();
    const double AimCos = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Source.TargetHalfAngleDegrees, 0.f, 180.f)));
    int64 TargetId = 0;
    FName TargetRegion;
    double BestDistance = MAX_dbl;
    for (const FMassEntityHandle Entity : Owned)
    {
        if (!Manager.IsEntityValid(Entity))
            continue;
        const auto& Id = Manager.GetFragmentDataChecked<FEcoIdentityFragment>(Entity);
        const auto* Profile = FindPlayerInteractionProfile(Id.SpeciesId);
        if (!Profile || !Profile->AcceptedFoodIds.Contains(Source.GetHeldFood()->FoodId))
            continue;
        const auto* Link = Manager.GetFragmentDataPtr<FEcoIntegratedV3Fragment>(Entity);
        const auto* Body = Manager.GetFragmentDataPtr<FEcoHerbivoreBodyV3Fragment>(Entity);
        if (!Link || !Body || !Link->bAdopted || Link->bDead || Manager.GetFragmentDataChecked<FEcoVitalsFragment>(Entity).HP <= 0)
            continue;
        const auto* Context = Native->GetContext(Link->ContextIndex);
        const FName Region = Manager.GetFragmentDataChecked<FEcoRegionFragment>(Entity).CurrentRegionId;
        if (!Context || Context->RegionId != Region || Id.StableAgentId <= 0)
            continue;
        const FVector BodyLocation = Context->UnitsToCm(Body->Body.Pos.X, Body->Body.Pos.Y, Link->GroundZ);
        const double Distance = FVector::DistSquared(BodyLocation, Pawn->GetActorLocation());
        const FVector Point = BodyLocation + FVector(0, 0, Profile->EyeHeightCm);
        const FVector Direction = (Point - Eyes).GetSafeNormal2D();
        if (Distance > FMath::Square(Profile->MaxBodyDistanceCm) || Distance > BestDistance
            || (Distance == BestDistance && Id.StableAgentId >= TargetId)
            || FVector::DotProduct(Direction, Forward) < AimCos
            || GetWorld()->LineTraceTestByObjectType(Eyes, Point, FCollisionObjectQueryParams(ECC_WorldStatic),
                FCollisionQueryParams(SCENE_QUERY_STAT(EcoHandFeedTarget), false, Pawn)))
            continue;
        TargetId = Id.StableAgentId;
        TargetRegion = Region;
        BestDistance = Distance;
    }
    if (TargetId <= 0)
        return EEcoHandFeedResult::NoEligibleTarget;
    auto& Request = HandFeedRequests.FindOrAdd(TargetId).AddDefaulted_GetRef();
    Request.Source = &Source;
    Request.RequestId = ++HandFeedSequence;
    Request.ExpiresAtStep = NativeStep + FMath::Max(1, Source.RequestLifetimeSteps);
    Request.Epoch = Epoch;
    Request.AvatarGeneration = Pawn->GetPlayerState<AEcoPlayerState>()->AvatarGeneration;
    Request.OfferGeneration = Source.GetOfferGeneration();
    Request.TargetRegion = TargetRegion;
    return EEcoHandFeedResult::None;
}

void AEcoCreatureIntegrationSpawner::CancelInvalidHandFeeds(int32 Epoch, int64 NativeStep)
{
    for (auto It = HandFeedRequests.CreateIterator(); It; ++It)
    {
        const int64 TargetId = It.Key();
        auto& Requests = It.Value();
        Requests.RemoveAll([&](const FHandFeedRequest& Request)
        {
            auto* Source = Request.Source.Get();
            const auto* Pawn = Source ? Cast<APawn>(Source->GetOwner()) : nullptr;
            const auto* State = Pawn ? Pawn->GetPlayerState<AEcoPlayerState>() : nullptr;
            const bool bCancelled = !IsPlayerInteractionEnabled() || Request.Epoch != Epoch
                || !Source || !Source->CanOffer() || Source->GetOfferGeneration() != Request.OfferGeneration
                || !State || State->AvatarGeneration != Request.AvatarGeneration;
            if (!bCancelled && NativeStep < Request.ExpiresAtStep)
                return false;
            if (Source)
            {
                FEcoHandFeedReceipt Receipt;
                Receipt.Result = bCancelled ? EEcoHandFeedResult::Cancelled : EEcoHandFeedResult::Expired;
                Receipt.RequestId = Request.RequestId;
                Receipt.TargetId = TargetId;
                Receipt.NativeStep = NativeStep;
                Receipt.WorldEpoch = Request.Epoch;
                Receipt.RegionId = Request.TargetRegion;
                Source->ReportFeedResult(Receipt);
            }
            return true;
        });
        if (Requests.IsEmpty())
            It.RemoveCurrent();
    }
}

void AEcoCreatureIntegrationSpawner::CancelHandFeedTarget(int64 TargetId, int64 NativeStep)
{
    TArray<FHandFeedRequest> Requests;
    if (!HandFeedRequests.RemoveAndCopyValue(TargetId, Requests))
        return;
    for (const auto& Request : Requests)
        if (auto* Source = Request.Source.Get())
        {
            FEcoHandFeedReceipt Receipt;
            Receipt.Result = EEcoHandFeedResult::Cancelled;
            Receipt.RequestId = Request.RequestId;
            Receipt.TargetId = TargetId;
            Receipt.NativeStep = NativeStep;
            Receipt.WorldEpoch = Request.Epoch;
            Receipt.RegionId = Request.TargetRegion;
            Source->ReportFeedResult(Receipt);
        }
}

double AEcoCreatureIntegrationSpawner::ConsumeHandFeed(int64 TargetId, FName RegionId, FName SpeciesId,
    const FEcoV3RegionContext& Context, const FVector& BodyLocation, const FVector& MouthLocation, bool bSafe, double Appetite)
{
    TArray<FHandFeedRequest> Requests;
    if (!HandFeedRequests.RemoveAndCopyValue(TargetId, Requests))
        return 0;
    const auto* Profile = FindPlayerInteractionProfile(SpeciesId);
    auto* Ecology = GetWorld()->GetSubsystem<UEcologySimulationSubsystem>();
    const int64 NativeStep = Context.Sim.T;
    double Consumed = 0;
    for (const auto& Request : Requests)
    {
        auto* Source = Request.Source.Get();
        if (!Source)
            continue;
        FEcoHandFeedReceipt Receipt;
        Receipt.RequestId = Request.RequestId;
        Receipt.TargetId = TargetId;
        Receipt.WorldEpoch = Request.Epoch;
        Receipt.NativeStep = NativeStep;
        Receipt.RegionId = RegionId;
        Receipt.SpeciesId = SpeciesId;
        const auto* Food = Source->GetHeldFood();
        if (Consumed > 0)
            Receipt.Result = EEcoHandFeedResult::Busy;
        else if (Request.TargetRegion != RegionId || Context.RegionId != RegionId
            || !Source->CanOffer() || Source->GetOfferGeneration() != Request.OfferGeneration)
            Receipt.Result = EEcoHandFeedResult::Cancelled;
        else if (!Profile || !Profile->AcceptedFoodIds.Contains(Food->FoodId))
            Receipt.Result = EEcoHandFeedResult::Unavailable;
        else if (!bSafe)
            Receipt.Result = EEcoHandFeedResult::UnsafeBehavior;
        else
        {
            const FVector Hand = Source->GetHandAnchor();
            const FVector PawnPosition = Source->GetOwner()->GetActorLocation();
            if (FVector::DistSquared(PawnPosition, BodyLocation) > FMath::Square(Profile->MaxBodyDistanceCm)
                || FVector::DistSquared(Hand, MouthLocation) > FMath::Square(Profile->MaxBiteDistanceCm)
                || FMath::Abs(Hand.Z - MouthLocation.Z) > Profile->MaxBiteHeightDifferenceCm
                || GetWorld()->LineTraceTestByObjectType(Hand, MouthLocation, FCollisionObjectQueryParams(ECC_WorldStatic),
                    FCollisionQueryParams(SCENE_QUERY_STAT(EcoHandFeedContact), false, Source->GetOwner())))
                Receipt.Result = EEcoHandFeedResult::OutOfReach;
            else if (Appetite < Food->UnitsPerBite)
                Receipt.Result = EEcoHandFeedResult::InsufficientAppetite;
            else if (!Ecology || !Ecology->RecordHumanReputation(Request.Epoch, RegionId, SpeciesId, *Profile, NativeStep, 0.,
                Receipt.Reputation, Receipt.ReputationDelta))
                Receipt.Result = EEcoHandFeedResult::Unavailable;
            else
            {
                Consumed = Food->UnitsPerBite;
                Source->ConsumeReservedBite(NativeStep);
                Receipt.ActualFood = Consumed;
                Receipt.Result = EEcoHandFeedResult::Success;
            }
        }
        UE_LOG(LogTemp, Log, TEXT("[EcoHandFood] Request=%lld Agent=%lld Region=%s Species=%s Step=%lld Result=%s Food=%.4f Score=%.2f Delta=%.2f Revision=%lld"),
            Receipt.RequestId, TargetId, *RegionId.ToString(), *SpeciesId.ToString(), NativeStep, *UEnum::GetValueAsString(Receipt.Result),
            Receipt.ActualFood, Receipt.Reputation.Score, Receipt.ReputationDelta, Receipt.Reputation.Revision);
        Source->ReportFeedResult(Receipt);
    }
    return Consumed;
}
