#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EcoPlayerInteractionTypes.generated.h"

class UStaticMesh;

UCLASS(BlueprintType)
class ADAPTIVEECOSYSTEM_API UEcoHandFoodDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Food") FName FoodId;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Food") TObjectPtr<UStaticMesh> Mesh;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Food", meta=(ClampMin="0.000001")) double UnitsPerBite = 0.04;
};

USTRUCT(BlueprintType)
struct FEcoSpeciesPlayerInteractionProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Food") TSet<FName> AcceptedFoodIds;
    UPROPERTY(EditAnywhere, Category="Food") FVector MouthOffsetCm = FVector(30, 0, 75);
    UPROPERTY(EditAnywhere, Category="Food", meta=(ClampMin="1")) double MaxBodyDistanceCm = 200;
    UPROPERTY(EditAnywhere, Category="Food", meta=(ClampMin="1")) double MaxBiteDistanceCm = 100;
    UPROPERTY(EditAnywhere, Category="Food", meta=(ClampMin="0")) double MaxBiteHeightDifferenceCm = 50;
    UPROPERTY(EditAnywhere, Category="Sight", meta=(ClampMin="1")) double SightRangeCm = 1600;
    UPROPERTY(EditAnywhere, Category="Sight", meta=(ClampMin="0")) double EyeHeightCm = 75;
    UPROPERTY(EditAnywhere, Category="Reputation") double MinimumScore = -100;
    UPROPERTY(EditAnywhere, Category="Reputation") double MaximumScore = 100;
    UPROPERTY(EditAnywhere, Category="Reputation") double InitialScore = 0;
    UPROPERTY(EditAnywhere, Category="Reputation", meta=(ClampMin="0")) double FeedGain = 5;
    UPROPERTY(EditAnywhere, Category="Reputation", meta=(ClampMin="0")) double DamageScale = 60;
    UPROPERTY(EditAnywhere, Category="Reputation", meta=(ClampMin="0")) int32 PositiveLockoutSteps = 75;

    bool IsValid() const;
};

USTRUCT(BlueprintType)
struct FEcoRegionalHumanReputation
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Reputation") double Score = 0;
    UPROPERTY(BlueprintReadOnly, Category="Reputation") int64 Revision = 0;
    int64 LockUntilStep = 0;
};

UENUM(BlueprintType)
enum class EEcoHandFeedResult : uint8
{
    None,
    Success,
    Unavailable,
    NoEligibleTarget,
    Busy,
    OutOfReach,
    UnsafeBehavior,
    InsufficientAppetite,
    Cancelled,
    Expired
};

USTRUCT(BlueprintType)
struct FEcoHandFeedReceipt
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Food") EEcoHandFeedResult Result = EEcoHandFeedResult::None;
    UPROPERTY(BlueprintReadOnly, Category="Food") int64 RequestId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") int64 TargetId = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") int64 NativeStep = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") int32 WorldEpoch = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") FName RegionId;
    UPROPERTY(BlueprintReadOnly, Category="Food") FName SpeciesId;
    UPROPERTY(BlueprintReadOnly, Category="Food") double ActualFood = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") double ReputationDelta = 0;
    UPROPERTY(BlueprintReadOnly, Category="Food") FEcoRegionalHumanReputation Reputation;
};

/** Native evidence for the game-side handoff. Identity is for target matching. */
struct FEcoVisiblePlayerInteraction
{
    int32 PlayerId = INDEX_NONE;
    uint32 AvatarGeneration = 0;
    FVector RelativePositionUnits = FVector::ZeroVector;
    FVector VelocityUnitsPerStep = FVector::ZeroVector;
    bool bHandKnown = false;
    FName VisibleFoodId;
    FVector RelativeFoodPositionUnits = FVector::ZeroVector;
    bool bReputationKnown = false;
    FEcoRegionalHumanReputation Reputation;
};
