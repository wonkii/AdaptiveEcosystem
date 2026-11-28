#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeSchema.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "EcoV3DeerDecisionTree.generated.h"

class UStateTree;

USTRUCT()
struct FEcoV3DeerDecisionInput
{
	GENERATED_BODY()

	uint8 Mask = 0;
	bool bRoute = false;
	float Energy = 0;
	float Darkness = 0;
	float Food = 0;
	float ThreatDistance = 1;
	bool bThreat = false;
	int32 CurrentBehavior = 0;
	int32 Result = INDEX_NONE;
};

UCLASS()
class UEcoV3DeerDecisionSchema : public UStateTreeSchema
{
	GENERATED_BODY()

	virtual bool IsStructAllowed(const UScriptStruct* Struct) const override;
	virtual bool IsExternalItemAllowed(const UStruct& Struct) const override;
};

USTRUCT()
struct FEcoV3DeerChoice
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Parameter", meta=(ClampMin="0", ClampMax="4"))
	int32 Slot = 0;

	UPROPERTY(EditAnywhere, Category="Parameter")
	bool bRoute = false;

	bool IsAllowed(const FEcoV3DeerDecisionInput& Input) const;
};

USTRUCT(meta=(DisplayName="Allowed V3 Deer Choice"))
struct FEcoV3DeerChoiceCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEcoV3DeerChoice;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	TStateTreeExternalDataHandle<FEcoV3DeerDecisionInput> InputHandle;
};

USTRUCT(meta=(DisplayName="V3 Deer Choice Utility"))
struct FEcoV3DeerChoiceUtility : public FStateTreeConsiderationCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEcoV3DeerChoice;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual float GetScore(FStateTreeExecutionContext& Context) const override;
	TStateTreeExternalDataHandle<FEcoV3DeerDecisionInput> InputHandle;
};

USTRUCT(meta=(DisplayName="Publish V3 Deer Choice"))
struct FEcoV3DeerChoiceTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FEcoV3DeerChoice;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool Link(FStateTreeLinker& Linker) override;
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const override;
	TStateTreeExternalDataHandle<FEcoV3DeerDecisionInput> InputHandle;
};

/** Owned by the integrated world. Each Native decision starts a fresh tree selection; Native owns behavior memory. */
UCLASS()
class UEcoV3DeerDecisionTree : public UObject
{
	GENERATED_BODY()

public:
	static constexpr const TCHAR* PackageName = TEXT("/Game/Experiments/SpeciesAI/V3Deer/ST_V3Deer");
	bool LoadTree();
	int32 Select(int32 Epoch, FName Region, int64 StableId, int64 Step, FEcoV3DeerDecisionInput Input);
	void RemoveMissingAgents(const TSet<int64>& Alive);
	void Dump(int64 StableId) const;
	bool HasFatalError() const { return bFatalError; }

private:
	struct FLastDecision
	{
		int32 Epoch = 0;
		FName Region;
		int64 Step = INDEX_NONE;
		FEcoV3DeerDecisionInput Input;
	};

	UPROPERTY(Transient)
	TObjectPtr<UStateTree> Tree;
	TMap<int64, FLastDecision> LastDecisions;
	int64 ActionSelections = 0;
	int64 RouteSelections = 0;
	int64 Fallbacks = 0;
	bool bFatalError = false;
};
