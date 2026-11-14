#include "Ecology/EcologySimulationSubsystem.h"

bool FEcoSpeciesPlayerInteractionProfile::IsValid() const
{
    return !MouthOffsetCm.ContainsNaN()
        && FMath::IsFinite(MaxBodyDistanceCm) && MaxBodyDistanceCm > 0
        && FMath::IsFinite(MaxBiteDistanceCm) && MaxBiteDistanceCm > 0
        && FMath::IsFinite(MaxBiteHeightDifferenceCm) && MaxBiteHeightDifferenceCm >= 0
        && FMath::IsFinite(SightRangeCm) && SightRangeCm > 0
        && FMath::IsFinite(EyeHeightCm) && EyeHeightCm >= 0
        && FMath::IsFinite(MinimumScore) && FMath::IsFinite(MaximumScore) && FMath::IsFinite(InitialScore)
        && MinimumScore <= InitialScore && InitialScore <= MaximumScore
        && FMath::IsFinite(FeedGain) && FeedGain >= 0
        && FMath::IsFinite(DamageScale) && DamageScale >= 0 && PositiveLockoutSteps >= 0;
}

bool UEcologySimulationSubsystem::ReadHumanReputation(int32 Epoch, FName RegionId, FName SpeciesId,
    const FEcoSpeciesPlayerInteractionProfile& Profile, FEcoRegionalHumanReputation& Out) const
{
    Out = {};
    if (!IsInGameThread() || !IsAuthoritativeWorld() || Epoch <= 0
        || RegionId.IsNone() || SpeciesId.IsNone() || !RegionalStates.Contains(RegionId))
        return false;

    Out.Score = Profile.InitialScore;
    if (Epoch == HumanReputationEpoch)
        if (const auto* Region = HumanReputationByRegion.Find(RegionId))
            if (const auto* Entry = Region->Find(SpeciesId))
                Out = *Entry;
    return true;
}

bool UEcologySimulationSubsystem::RecordHumanReputation(int32 Epoch, FName RegionId, FName SpeciesId,
    const FEcoSpeciesPlayerInteractionProfile& Profile, int64 NativeStep, double AppliedDamageFraction,
    FEcoRegionalHumanReputation& Out, double& OutDelta)
{
    OutDelta = 0;
    if (!FMath::IsFinite(AppliedDamageFraction) || AppliedDamageFraction < 0
        || !ReadHumanReputation(Epoch, RegionId, SpeciesId, Profile, Out))
        return false;
    if (Epoch != HumanReputationEpoch)
    {
        HumanReputationByRegion.Reset();
        HumanReputationEpoch = Epoch;
    }
    const double Before = Out.Score;
    if (AppliedDamageFraction > 0)
    {
        Out.Score -= Profile.DamageScale * FMath::Min(1., AppliedDamageFraction);
        Out.LockUntilStep = NativeStep + Profile.PositiveLockoutSteps;
    }
    else if (NativeStep >= Out.LockUntilStep)
    {
        Out.Score += Profile.FeedGain;
    }
    Out.Score = FMath::Clamp(Out.Score, Profile.MinimumScore, Profile.MaximumScore);
    OutDelta = Out.Score - Before;
    ++Out.Revision;
    HumanReputationByRegion.FindOrAdd(RegionId).Add(SpeciesId, Out);
    return true;
}
