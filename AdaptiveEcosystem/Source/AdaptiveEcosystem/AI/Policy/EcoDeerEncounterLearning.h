#pragma once

#include "CoreMinimal.h"
#include "Core/EcoDeerPlayerExperience.h"
#include "Subsystems/WorldSubsystem.h"
#include "EcoDeerEncounterLearning.generated.h"

class AEcoCreatureIntegrationSpawner;
struct FEcoDeerEncounterSession;
struct FEcoDeerEncounterJob;

UCLASS()
class UEcoDeerEncounterLearning : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
    virtual void Deinitialize() override;

    void RequestMode(const TArray<FString>& Args, FOutputDevice& Output);
    void Describe(FOutputDevice& Output) const;
    void Boundary(AEcoCreatureIntegrationSpawner* Owner, int32 Epoch, int32 Environment, const FString& Brain);
    void ResetForRestore();

    bool IsActive() const { return Session.IsValid(); }
    bool IsCollecting() const;
    TOptional<EEcoDeerPlayerResponse> Decide(FName Region, int64 Deer, uint64 Step, double DistanceRatio,
        double ClosingSpeed, double Energy, double Damage, double Feeding);
    void EndEncounter(FName Region, int64 Deer, bool bDiscard = false);
    void PruneEncounters(const TMap<int64, FName>& LiveRegions);
    void ConfirmAction(FName Region, int64 Deer);
    void RecordOutcome(const AEcoCreatureIntegrationSpawner& Owner, FName Region, int64 Deer, bool bFeeding, double Fraction);

private:
    enum class EMode : uint8 { Off, Frozen, Online };
    void StopCollection();
    void ApplyMode(EMode Mode, AEcoCreatureIntegrationSpawner* Owner);
    void PollJob();
    void SubmitJob();

    TSharedPtr<FEcoDeerEncounterSession> Session;
    TSharedPtr<FEcoDeerEncounterJob, ESPMode::ThreadSafe> Job;
    TOptional<EMode> PendingMode;
    bool bRollback = false;
    int32 Generation = 0;
};
