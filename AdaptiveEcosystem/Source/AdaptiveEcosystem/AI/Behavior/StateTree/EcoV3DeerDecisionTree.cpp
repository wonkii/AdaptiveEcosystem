#include "AI/Behavior/StateTree/EcoV3DeerDecisionTree.h"

#include "AI/Policy/EcoRepertoire.h"
#include "HAL/IConsoleManager.h"
#include "StateTree.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"

namespace
{
	TAutoConsoleVariable<int32> TraceDeer(TEXT("eco.V3Deer.TraceId"), 0,
		TEXT("Log StateTree decisions for this StableAgentId. 0 disables tracing."));
}

bool UEcoV3DeerDecisionSchema::IsStructAllowed(const UScriptStruct* Struct) const
{
	return Struct == FEcoV3DeerChoiceCondition::StaticStruct()
		|| Struct == FEcoV3DeerChoiceUtility::StaticStruct()
		|| Struct == FEcoV3DeerChoiceTask::StaticStruct();
}

bool UEcoV3DeerDecisionSchema::IsExternalItemAllowed(const UStruct& Struct) const
{
	return &Struct == FEcoV3DeerDecisionInput::StaticStruct();
}

bool FEcoV3DeerChoice::IsAllowed(const FEcoV3DeerDecisionInput& Input) const
{
	return bRoute == Input.bRoute && Slot >= 0 && Slot < EcoRepertoireV3::kNumBehaviors
		&& (Input.Mask & (1 << Slot)) != 0;
}

bool FEcoV3DeerChoiceCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(InputHandle);
	return true;
}

bool FEcoV3DeerChoiceCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	return Context.GetInstanceData(*this).IsAllowed(Context.GetExternalData(InputHandle));
}

bool FEcoV3DeerChoiceUtility::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(InputHandle);
	return true;
}

float FEcoV3DeerChoiceUtility::GetScore(FStateTreeExecutionContext& Context) const
{
	using namespace EcoRepertoireV3;
	const auto& Choice = Context.GetInstanceData(*this);
	const auto& Input = Context.GetExternalData(InputHandle);
	if (Choice.bRoute) return Choice.Slot == kSleep ? 1.0f : 0.5f; // Route slot 4 is SHELTER.
	switch (Choice.Slot)
	{
	case kGraze: return Input.bThreat ? 0.1f : 0.35f + 0.45f * (1 - Input.Energy) + 0.15f * Input.Food;
	case kFlee: return Input.bThreat ? 0.9f + 0.1f * (1 - Input.ThreatDistance) : 0.0f;
	case kHide: return Input.bThreat ? 0.8f : 0.0f;
	case kFreeze: return Input.bThreat ? 0.7f : 0.0f;
	case kSleep: return Input.bThreat ? 0.0f : 0.9f * Input.Darkness * Input.Energy;
	default: return 0.0f;
	}
}

bool FEcoV3DeerChoiceTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(InputHandle);
	return true;
}

EStateTreeRunStatus FEcoV3DeerChoiceTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&) const
{
	const auto& Choice = Context.GetInstanceData(*this);
	auto& Input = Context.GetExternalData(InputHandle);
	if (!Choice.IsAllowed(Input)) return EStateTreeRunStatus::Failed;
	Input.Result = Choice.Slot;
	return EStateTreeRunStatus::Running;
}

bool UEcoV3DeerDecisionTree::LoadTree()
{
	const FString ObjectPath = FString(PackageName) + TEXT(".ST_V3Deer");
	Tree = LoadObject<UStateTree>(nullptr, *ObjectPath);
	return Tree && Tree->IsReadyToRun() && Cast<UEcoV3DeerDecisionSchema>(Tree->GetSchema());
}

int32 UEcoV3DeerDecisionTree::Select(int32 Epoch, FName Region, int64 StableId, int64 Step, FEcoV3DeerDecisionInput Input)
{
	check(IsInGameThread());
	auto& Previous = LastDecisions.FindOrAdd(StableId);
	if (Previous.Epoch == Epoch && Previous.Step == Step && Previous.Region == Region
		&& Previous.Input.bRoute == Input.bRoute && Previous.Input.Mask == Input.Mask)
	{
		return Previous.Input.Result;
	}

	if (Input.Mask)
	{
		FStateTreeInstanceData Instance;
		const auto Collect = FOnCollectStateTreeExternalData::CreateLambda(
			[&Input](const FStateTreeExecutionContext&, const UStateTree*,
				TArrayView<const FStateTreeExternalDataDesc> Descs, TArrayView<FStateTreeDataView> Views)
			{
				for (int32 Index = 0; Index < Descs.Num(); ++Index)
				{
					if (Descs[Index].Struct != FEcoV3DeerDecisionInput::StaticStruct()) return false;
					Views[Index] = FStateTreeDataView(FStructView::Make(Input));
				}
				return true;
			});
		FStateTreeExecutionContext Context(*this, *Tree, Instance, Collect);
		Context.Start();
		Context.Stop();
	}

	const auto IsAllowed = [&Input](int32 Slot)
	{
		return Slot >= 0 && Slot < EcoRepertoireV3::kNumBehaviors && (Input.Mask & (1 << Slot)) != 0;
	};
	if (IsAllowed(Input.Result))
	{
		++(Input.bRoute ? RouteSelections : ActionSelections);
	}
	else
	{
		++Fallbacks;
		const int32 Preferred = Input.bRoute ? EcoRepertoireV3::kFlee : Input.CurrentBehavior;
		Input.Result = IsAllowed(Preferred) ? Preferred : INDEX_NONE;
		for (int32 Slot = 0; Input.Result == INDEX_NONE && Slot < EcoRepertoireV3::kNumBehaviors; ++Slot)
		{
			if (IsAllowed(Slot)) Input.Result = Slot;
		}
		if (Input.Result == INDEX_NONE)
		{
			if (!bFatalError)
			{
				UE_LOG(LogTemp, Error, TEXT("[V3DeerDecision] Empty mask: Region=%s Id=%lld Step=%lld Route=%d"),
					*Region.ToString(), StableId, Step, Input.bRoute);
			}
			bFatalError = true;
			Input.Result = Preferred;
		}
	}
	Previous = {Epoch, Region, Step, Input};
	if (TraceDeer.GetValueOnGameThread() == StableId)
	{
		Dump(StableId);
	}
	return Input.Result;
}

void UEcoV3DeerDecisionTree::RemoveMissingAgents(const TSet<int64>& Alive)
{
	for (auto It = LastDecisions.CreateIterator(); It; ++It)
	{
		if (!Alive.Contains(It.Key())) It.RemoveCurrent();
	}
}

void UEcoV3DeerDecisionTree::Dump(int64 StableId) const
{
	UE_LOG(LogTemp, Display, TEXT("[V3DeerDecision] Backend=StateTree ActionSelections=%lld RouteSelections=%lld Fallbacks=%lld Fatal=%d Tracked=%d"),
		ActionSelections, RouteSelections, Fallbacks, bFatalError, LastDecisions.Num());
	if (const auto* Last = LastDecisions.Find(StableId))
	{
		const auto& Input = Last->Input;
		UE_LOG(LogTemp, Display, TEXT("[V3DeerDecision] Region=%s Epoch=%d Id=%lld Step=%lld Channel=%s Mask=0x%02X Request=%d CurrentAction=%d Energy01=%.3f Threat=%d"),
			*Last->Region.ToString(), Last->Epoch, StableId, Last->Step, Input.bRoute ? TEXT("Route") : TEXT("Action"),
			Input.Mask, Input.Result, Input.CurrentBehavior, Input.Energy, Input.bThreat);
	}
}
