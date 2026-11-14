#pragma once

#include "AbilitySystemInterface.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "EcoPlayerState.generated.h"

class UAbilitySystemComponent;
class UEcoHealthAttributeSet;
class UEcoHandFoodDefinition;
class UEcoHandFoodComponent;
struct FOnAttributeChangeData;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEcoPlayerHealthChanged, float, Health, float, MaxHealth);

UCLASS(Blueprintable)
class ADAPTIVEECOSYSTEM_API AEcoPlayerState : public APlayerState, public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AEcoPlayerState();
    void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    UAbilitySystemComponent* GetAbilitySystemComponent() const override;
    void InitializeHealthForAvatar(APawn* Pawn);
    float ApplyCreatureDamage(APawn* ExpectedAvatar, float Damage, int64 AttackerId);
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Health") void SetInvulnerable(bool bEnabled);
    UFUNCTION(BlueprintPure, Category="Health") float GetHealth() const;
    UFUNCTION(BlueprintPure, Category="Health") bool IsAlive() const;
    UPROPERTY(BlueprintAssignable, Category="Health") FEcoPlayerHealthChanged OnHealthChanged;
    UPROPERTY(EditAnywhere, Category="Health") bool bInvulnerable = false;
    UPROPERTY(EditDefaultsOnly, Category="Health", meta=(ClampMin="0")) float RespawnDelay = 5.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Health") int64 LastCreatureAttacker = 0;
    uint32 AvatarGeneration = 0;

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Hand Food") TObjectPtr<UEcoHandFoodDefinition> HandFood;
    UPROPERTY(EditDefaultsOnly, Category="Hand Food", meta=(ClampMin="0")) int32 StartingHandFoodCount = 10;
    UFUNCTION(BlueprintPure, Category="Hand Food") int32 GetHandFoodCount() const { return HandFoodCount; }

protected:
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type Reason) override;

private:
    friend class UEcoHandFoodComponent;
    UPROPERTY(Replicated) int32 HandFoodCount = 0;
    void HealthChanged(const FOnAttributeChangeData& Change);
    void ApplyDeathToAvatar();
    void Respawn();
    FTimerHandle RespawnTimer;
    TWeakObjectPtr<APawn> HealthAvatar;
    bool bDeathHandled = false;
    UPROPERTY(VisibleAnywhere, Category="Health") TObjectPtr<UEcoHealthAttributeSet> HealthSet;
    // Shared classifications such as role or faction. PlayerId identifies the individual session player.
    UPROPERTY(EditDefaultsOnly, Category="Abilities")
    FGameplayTagContainer ServerClassificationTags;

    UPROPERTY(VisibleAnywhere, Category="Abilities")
    TObjectPtr<UAbilitySystemComponent> AbilitySystem;
};
