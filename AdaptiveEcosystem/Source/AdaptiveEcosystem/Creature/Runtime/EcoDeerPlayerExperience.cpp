#include "Core/EcoDeerPlayerExperience.h"
#include "AI/Policy/EcoDeerEncounterLearning.h"

#include "AI/Policy/EcoPolicyV3Fragments.h"
#include "AI/Policy/EcoPolicyV3IntegratedFragments.h"
#include "AI/Policy/EcoPolicyV3IntegratedSubsystem.h"
#include "Creature/Runtime/EcoCreatureIntegrationSpawner.h"
#include "Ecology/EcologySimulationSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Mass/EcoMassFragments.h"
#include "MassEntityManager.h"
#include "MassSpawnerSubsystem.h"
#include "Player/EcoPlayerState.h"
#include "World/EcoWorldClockSubsystem.h"

namespace
{
    bool IsFeedingPlayer(const APawn* Player)
    {
        if (!IsValid(Player) || !Player->HasAuthority() || !Player->IsPlayerControlled() || !Player->GetController()) return false;
        const auto* State = Player->GetPlayerState<AEcoPlayerState>();
        return !State || State->IsAlive();
    }

    bool CanReachDeer(const APawn& Player, const FEcoV3RegionContext& Context, const FVector& Position,
        double Range, bool bAimRequired, double& OutDistanceSq)
    {
        FVector Eye;
        FRotator Look;
        Player.GetActorEyesViewPoint(Eye, Look);
        const FVector Target = Position + FVector(0., 0., 75.);
        const FVector Delta = Target - Eye;
        OutDistanceSq = Delta.SizeSquared();
        if (OutDistanceSq > FMath::Square(Range) || FMath::Abs(Delta.Z) > 250.) return false;
        if (bAimRequired && FVector::DotProduct(Delta.GetSafeNormal2D(), Look.Vector().GetSafeNormal2D()) < .35) return false;
        const auto A = Context.CmToUnits(Eye);
        const auto B = Context.CmToUnits(Target);
        return !Context.Sim.Food.BlocksSight(A.X, A.Y, B.X, B.Y)
            && !Player.GetWorld()->LineTraceTestByObjectType(Eye, Target, FCollisionObjectQueryParams(ECC_WorldStatic),
                FCollisionQueryParams(SCENE_QUERY_STAT(EcoDeerFeed), false, &Player));
    }
}

bool AEcoCreatureIntegrationSpawner::UsesDeerPlayerExperience() const
{
    return DeerPlayerExperience.bEnabled && bUseV3Policy && bEnableEcosystemLife && bReady && GetNetMode() != NM_Client;
}

void AEcoCreatureIntegrationSpawner::RecordDeerPlayerExperience(FName Region, double Now, bool bFeeding, double Fraction)
{
    if (!UsesDeerPlayerExperience()) return;
    if (auto* Ecology = GetWorld()->GetSubsystem<UEcologySimulationSubsystem>())
        Ecology->RecordDeerPlayerExperience(Region, Now, bFeeding, Fraction, DeerPlayerExperience);
}

bool AEcoCreatureIntegrationSpawner::QueueDeerFeed(APawn& Player)
{
    if (!IsInGameThread() || !UsesDeerPlayerExperience() || !IsFeedingPlayer(&Player) || Player.GetWorld() != GetWorld())
        return false;
    const auto* Native = GetWorld()->GetSubsystem<UEcoPolicyV3IntegratedSubsystem>();
    auto& Entities = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>()->GetEntityManagerChecked();
    if (!Native || !Native->IsActive() || Entities.IsProcessing()) return false;

    AController* Controller = Player.GetController();
    auto* Inventory = DeerFeedInventories.Find(Controller);
    if (!Inventory)
    {
        for (auto It = DeerFeedInventories.CreateIterator(); It; ++It)
            if (!It.Key().IsValid()) It.RemoveCurrent();
        Inventory = &DeerFeedInventories.Add(Controller);
        Inventory->Rations = FMath::Clamp(DeerPlayerExperience.StartingRations, 0, 100);
    }
    const double RealNow = GetWorld()->GetTimeSeconds();
    if (RealNow < Inventory->NextRequest) return false;
    Inventory->NextRequest = RealNow + .75;
    if (Inventory->PendingDeer || Inventory->Rations <= 0 || PendingDeerFeeds.Num() >= 64)
    {
        if (DeerPlayerExperience.bLog && Inventory->Rations <= 0)
            UE_LOG(LogTemp, Log, TEXT("[Eco Deer Experience] Feed rejected: no rations"));
        return false;
    }

    const auto Time = GetWorld()->GetSubsystem<UEcoWorldClockSubsystem>()->GetServerTime();
    const double Range = FMath::Clamp(DeerPlayerExperience.FeedRange, 100.f, 1500.f);
    double BestDistanceSq = FMath::Square(Range);
    int64 BestId = 0;
    FName BestRegion;
    for (const auto& Entry : AgentLookup)
    {
        const auto Entity = Entry.Value;
        if (!Entities.IsEntityValid(Entity) || PendingDeerFeeds.Contains(Entry.Key)) continue;
        const auto* Body = Entities.GetFragmentDataPtr<FEcoHerbivoreBodyV3Fragment>(Entity);
        const auto* Link = Entities.GetFragmentDataPtr<FEcoIntegratedV3Fragment>(Entity);
        if (!Body || !Link || !Link->bAdopted || Link->bDead
            || Entities.GetFragmentDataChecked<FEcoVitalsFragment>(Entity).HP <= 0.f
            || Entities.GetFragmentDataChecked<FEcoIdentityFragment>(Entity).SpeciesId != FName(TEXT("Herbivore"))) continue;
        const FName Region = Entities.GetFragmentDataChecked<FEcoRegionFragment>(Entity).CurrentRegionId;
        const auto* Context = Native->GetContext(Link->ContextIndex);
        if (!Context || Context->RegionId != Region) continue;
        const FVector Position = Context->UnitsToCm(Body->Body.Pos.X, Body->Body.Pos.Y, Link->GroundZ);
        double DistanceSq;
        if (!CanReachDeer(Player, *Context, Position, Range, true, DistanceSq)
            || DistanceSq > BestDistanceSq || (DistanceSq == BestDistanceSq && BestId && Entry.Key > BestId)) continue;
        BestId = Entry.Key;
        BestRegion = Region;
        BestDistanceSq = DistanceSq;
    }
    if (!BestId)
    {
        if (DeerPlayerExperience.bLog) UE_LOG(LogTemp, Log, TEXT("[Eco Deer Experience] Feed rejected: no reachable deer"));
        return false;
    }
    --Inventory->Rations;
    Inventory->PendingDeer = BestId;
    PendingDeerFeeds.Add(BestId, {&Player, Controller, BestRegion, Time.WorldEpoch, Time.ServerTimeSeconds, Time.ServerTimeSeconds + 2.,
        FMath::Clamp(DeerPlayerExperience.RationEnergy, .01f, .5f)});
    return true;
}

void AEcoCreatureIntegrationSpawner::CancelDeerFeed(int64 DeerId, const TCHAR* Reason)
{
    FDeerFeedReservation Reservation;
    if (!PendingDeerFeeds.RemoveAndCopyValue(DeerId, Reservation)) return;
    if (auto* Inventory = DeerFeedInventories.Find(Reservation.Controller))
    {
        ++Inventory->Rations;
        Inventory->PendingDeer = 0;
    }
    if (DeerPlayerExperience.bLog)
        UE_LOG(LogTemp, Log, TEXT("[Eco Deer Experience] Feed returned Region=%s Reason=%s"), *Reservation.Region.ToString(), Reason);
}

void AEcoCreatureIntegrationSpawner::ExpireDeerFeeds(const FEcoServerTimeSnapshot& Time)
{
    for (auto It = DeerFeedInventories.CreateIterator(); It; ++It)
        if (!It.Key().IsValid()) It.RemoveCurrent();
    if (PendingDeerFeeds.IsEmpty()) return;

    const auto& Entities = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>()->GetEntityManagerChecked();
    TArray<int64, TInlineAllocator<8>> Expired;
    for (const auto& Entry : PendingDeerFeeds)
    {
        const auto* Entity = AgentLookup.Find(Entry.Key);
        const auto* Player = Entry.Value.Player.Get();
        if (!UsesDeerPlayerExperience() || Entry.Value.Epoch != Time.WorldEpoch || Time.ServerTimeSeconds >= Entry.Value.ExpiresAt
            || !IsFeedingPlayer(Player) || Player->GetController() != Entry.Value.Controller.Get()
            || !Entity || !Entities.IsEntityValid(*Entity) || Entities.GetFragmentDataChecked<FEcoVitalsFragment>(*Entity).HP <= 0.f)
            Expired.Add(Entry.Key);
    }
    for (const int64 DeerId : Expired) CancelDeerFeed(DeerId, TEXT("expired or invalid"));
}

double AEcoCreatureIntegrationSpawner::OfferDeerFeed(int64 DeerId, const FEcoV3RegionContext& Context, FName Region,
    const FVector& Position, double AuthorityTime, bool bCanEat)
{
    const auto* Entry = PendingDeerFeeds.Find(DeerId);
    if (!Entry) return 0.;
    const auto Time = GetWorld()->GetSubsystem<UEcoWorldClockSubsystem>()->GetServerTime();
    AuthorityTime = AuthorityTime >= 0. ? AuthorityTime : Time.ServerTimeSeconds;
    if (AuthorityTime + 1.e-6 < Entry->IssuedAt) return 0.;
    auto* Player = Entry->Player.Get();
    double DistanceSq;
    if (!bCanEat || Entry->Epoch != Time.WorldEpoch || AuthorityTime >= Entry->ExpiresAt || Entry->Region != Region || !IsFeedingPlayer(Player)
        || Player->GetController() != Entry->Controller.Get()
        || !CanReachDeer(*Player, Context, Position, FMath::Clamp(DeerPlayerExperience.FeedRange, 100.f, 1500.f), false, DistanceSq))
    {
        CancelDeerFeed(DeerId, TEXT("unsafe, expired or out of reach"));
        return 0.;
    }
    return Entry->Energy;
}

void AEcoCreatureIntegrationSpawner::ResolveDeerFeed(int64 DeerId, FName Region, double GameTime, double EnergyFraction)
{
    if (EnergyFraction <= 0.)
    {
        CancelDeerFeed(DeerId, TEXT("no useful energy"));
        return;
    }
    FDeerFeedReservation Reservation;
    if (!PendingDeerFeeds.RemoveAndCopyValue(DeerId, Reservation)) return;
    if (auto* Inventory = DeerFeedInventories.Find(Reservation.Controller)) Inventory->PendingDeer = 0;
    RecordDeerPlayerExperience(Region, GameTime, true, EnergyFraction);
    if (auto* Learning = GetWorld()->GetSubsystem<UEcoDeerEncounterLearning>())
        Learning->RecordOutcome(*this, Region, DeerId, true, EnergyFraction);
}

void AEcoCreatureIntegrationSpawner::CancelDeerFeedsForRestore()
{
    TArray<int64> Pending;
    PendingDeerFeeds.GetKeys(Pending);
    for (const int64 DeerId : Pending) CancelDeerFeed(DeerId, TEXT("checkpoint restored"));
}
