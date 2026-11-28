#pragma once

#include "CoreMinimal.h"
#include "EcoDeerPlayerExperience.generated.h"

/** Regional human encounter tuning for the integrated Native deer. */
USTRUCT(BlueprintType)
struct FEcoDeerPlayerExperienceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Experience")
    bool bEnabled = false;

    /** Scales the distance change from regional experience. Zero gives neutral encounter distances. */
    UPROPERTY(EditAnywhere, Category="Experience", meta=(ClampMin="0", ClampMax="3"))
    float Sensitivity = 1.f;

    UPROPERTY(EditAnywhere, Category="Experience", meta=(ClampMin="0", ClampMax="10"))
    float DamageGain = 2.5f;

    UPROPERTY(EditAnywhere, Category="Experience", meta=(ClampMin="0", ClampMax="10"))
    float FeedingGain = 4.f;

    /** Authority game seconds for a region's damage pressure and feeding familiarity to halve. */
    UPROPERTY(EditAnywhere, Category="Experience", meta=(ClampMin="1", ClampMax="7200", Units="s"))
    float HalfLife = 240.f;

    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="100", ClampMax="3600", Units="cm"))
    float AlertDistance = 1200.f;

    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="50", ClampMax="3600", Units="cm"))
    float RetreatDistance = 400.f;

    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="0", ClampMax="3600", Units="cm"))
    float AlertDistanceShift = 1200.f;

    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="0", ClampMax="3600", Units="cm"))
    float RetreatDistanceShift = 850.f;

    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="0", ClampMax="1500", Units="cm"))
    float ReleaseMargin = 200.f;

    /** Extra alert distance when the observed player closes at 900 cm/s. */
    UPROPERTY(EditAnywhere, Category="Encounter", meta=(ClampMin="0", ClampMax="1500", Units="cm"))
    float FastApproachBonus = 450.f;

    UPROPERTY(EditAnywhere, Category="Feeding", meta=(ClampMin="0", ClampMax="100"))
    int32 StartingRations = 12;

    UPROPERTY(EditAnywhere, Category="Feeding", meta=(ClampMin="100", ClampMax="1500", Units="cm"))
    float FeedRange = 800.f;

    /** Maximum useful Energy supplied by one ration, as a fraction of MaxEnergy. */
    UPROPERTY(EditAnywhere, Category="Feeding", meta=(ClampMin="0.01", ClampMax="0.5"))
    float RationEnergy = .2f;

    UPROPERTY(EditAnywhere, Category="Diagnostics")
    bool bLog = false;
};

/** One server-world entry per RegionId for SpeciesId Herbivore. */
struct FEcoDeerPlayerExperience
{
    double Damage = 0., Feeding = 0., UpdatedAt = 0.;
    double HalfLifeSeconds = 240.;
    uint64 DamageEvents = 0, FeedingEvents = 0;

    double Attitude() const { return Damage - Feeding; }

    void AdvanceTo(double Now)
    {
        if (Now <= UpdatedAt) return;
        if (Damage > 0. || Feeding > 0.)
        {
            const double Factor = FMath::Pow(.5, (Now - UpdatedAt) / HalfLifeSeconds);
            Damage *= Factor;
            Feeding *= Factor;
        }
        UpdatedAt = Now;
    }
};

enum class EEcoDeerPlayerResponse : uint8 { None, Alert, Retreat };

/** Latest local sight evidence. No player identity survives the sight scan. */
struct FEcoDeerPlayerEncounter
{
    FName Region;
    FVector2D SeenPosition = FVector2D::ZeroVector;
    double SeenAt = -1.;
    double ClosingSpeed = 0.;
    EEcoDeerPlayerResponse Response = EEcoDeerPlayerResponse::None;
};
