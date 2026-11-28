#include "Creature/Runtime/EcoCreatureIntegrationSpawner.h"
#include "Creature/Runtime/EcoCreatureRuntimeTypes.h"
#include "Creature/Runtime/EcoFaunaMotionClock.h"
#include "AI/Policy/EcoPolicyV3IntegratedFragments.h"
#include "AI/Policy/EcoPolicyV3IntegratedSubsystem.h"
#include "AI/Policy/EcoBehaviorConfig.h"
#include "Mass/EcoMassFragments.h"
#include "Mass/EntityFragments.h"
#include "MassSpawnerSubsystem.h"
#include "MassEntityManager.h"
#include "Engine/World.h"
#include "Creature/Audio/EcoFootstepAudioComponent.h"
#include "Mass/EcoMassPresentationFragments.h"
#include "Core/EcoRuntimeSettings.h"
#include "MassEntityQuery.h"
#include "MassExecutionContext.h"
#include "GameFramework/PlayerController.h"
#include "Player/EcoCombatRelevanceComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#if !UE_BUILD_SHIPPING
#include "Components/SkeletalMeshComponent.h"
#include "Network/EcoGameState.h"
#endif

void AEcoCreatureIntegrationSpawner::SyncVisuals(float Delta)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(EcoCreature_Visuals);
	if (GetNetMode() == NM_DedicatedServer) return;
	auto* Spawner = GetWorld()->GetSubsystem<UMassSpawnerSubsystem>(); if (!Spawner) return;
	auto& EM = Spawner->GetEntityManagerChecked();
	const auto& Budget = GetDefault<UEcoRuntimeSettings>()->ConnectionBudget;
	const double Now = GetWorld()->GetRealTimeSeconds();
	const bool bClient = GetNetMode() == NM_Client;
#if !UE_BUILD_SHIPPING
	const int64 TraceId = PendingVisibilityTraceId;
	PendingVisibilityTraceId = 0;
	const TCHAR* TraceStage = TEXT("NoProxy");
	FVector TracePosition = FVector::ZeroVector;
	double TraceDistanceCm = -1.0;
	uint32 TraceSequence = 0;
	uint32 TraceVisualRevision = 0;
	uint32 TraceResetRevision = 0;
	FName TraceSpecies;
#endif
	const auto* Native = MixedNativeStep ? GetWorld()->GetSubsystem<UEcoPolicyV3IntegratedSubsystem>() : nullptr;
	const double NativeAlpha = Native ? FMath::Clamp(FaunaMotionTime * EcoFaunaMotionClock::StepsPerSecond / EcoBehaviorConfig::PolicyInterval
		- Native->GetGlobalStep(), 0., 1.) : 1.;
	struct FInput
	{
		int64 Id; FName Species; FName Region; FVector Position; float FacingYaw; FEcoCreaturePresentationFragment Visual;
		uint32 ResetRevision = 0; double Distance = 0.0;
	};
	TArray<FVector> Viewers;
	TArray<const UEcoCombatRelevanceComponent*> CombatViewers;
	for (auto It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		if (const auto* PC = It->Get(); PC && PC->IsLocalController())
		{
			// Client possession and its initial view target can arrive after the first proxy.
			if (bClient && !PC->GetViewTarget()) continue;
			FVector P; FRotator R; PC->GetPlayerViewPoint(P, R);
			if (P.ContainsNaN()) continue;
			Viewers.Add(P);
			if (const APawn* Pawn = PC->GetPawn())
				if (const auto* Relevance = Pawn->FindComponentByClass<UEcoCombatRelevanceComponent>())
					CombatViewers.Add(Relevance);
		}
	TArray<FInput> Inputs;
	FMassEntityQuery Query(EM.AsShared());
	Query.AddTagRequirement<FEcoIntegratedCreatureTag>(EMassFragmentPresence::All);
	Query.AddRequirement<FEcoIdentityFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FEcoRegionFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FEcoCreaturePresentationFragment>(EMassFragmentAccess::ReadOnly);
	Query.AddRequirement<FEcoClientPresentationFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddRequirement<FEcoMovementDiscontinuityFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddRequirement<FEcoVitalsFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	Query.AddRequirement<FEcoPolicyOutputFragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	if (Native) Query.AddRequirement<FEcoIntegratedV3Fragment>(EMassFragmentAccess::ReadOnly, EMassFragmentPresence::Optional);
	FMassExecutionContext Context = EM.CreateExecutionContext(Delta);
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& C)
	{
		if (bClient && (!C.GetFragmentView<FEcoVitalsFragment>().IsEmpty() || !C.GetFragmentView<FEcoPolicyOutputFragment>().IsEmpty()))
		{ FailRuntime(TEXT("Client contains authoritative fragments")); return; }
		const auto Ids = C.GetFragmentView<FEcoIdentityFragment>(); const auto T = C.GetFragmentView<FTransformFragment>();
		const auto V = C.GetFragmentView<FEcoCreaturePresentationFragment>(); const auto RegionsView = C.GetFragmentView<FEcoRegionFragment>();
		const auto Samples = C.GetFragmentView<FEcoClientPresentationFragment>();
		const auto Resets = C.GetFragmentView<FEcoMovementDiscontinuityFragment>();
		const auto NativeBodies = Native ? C.GetFragmentView<FEcoIntegratedV3Fragment>() : TConstArrayView<FEcoIntegratedV3Fragment>();
		for (int32 I = 0; I < C.GetNumEntities(); ++I)
		{
#if !UE_BUILD_SHIPPING
			const bool bTrace = TraceId > 0 && Ids[I].StableAgentId == TraceId;
			if (bTrace)
			{
				TraceSpecies = Ids[I].SpeciesId;
				TraceStage = TEXT("NoSample");
			}
#endif
			if (Ids[I].StableAgentId <= 0 || (bClient && Samples.IsEmpty())
				|| (!Samples.IsEmpty() && !Samples[I].bHasSample)) continue;
			FVector Position = Samples.IsEmpty() ? T[I].GetTransform().GetLocation()
				: Samples[I].GetRenderPosition(Now, InterpolationDelaySeconds);
			if (Samples.IsEmpty() && !NativeBodies.IsEmpty() && NativeBodies[I].bAdopted && !NativeBodies[I].bDead)
			{
				Position -= V[I].Velocity * ((1. - NativeAlpha) * EcoRepertoireV3::kStepSeconds);
				Position.Z = FMath::Lerp(NativeBodies[I].PrevGroundZ, NativeBodies[I].GroundZ, NativeAlpha);
			}
			double Distance = Viewers.IsEmpty() ? 0.0 : TNumericLimits<double>::Max();
			for (const auto& Viewer : Viewers) Distance = FMath::Min(Distance, FVector::DistSquared(Viewer, Position));
#if !UE_BUILD_SHIPPING
			if (bTrace)
			{
				TracePosition = Position;
				TraceDistanceCm = Viewers.IsEmpty() ? -1.0 : FMath::Sqrt(Distance);
				const auto& Sample = Samples[I].Current;
				TraceSequence = Sample.SampleSequence;
				TraceVisualRevision = Sample.VisualRevision;
				TraceResetRevision = Sample.MovementDiscontinuityRevision;
				TraceStage = Viewers.IsEmpty() ? TEXT("NoViewer") : TEXT("OutsideVisualRange");
			}
#endif
			if (!bClient && !Viewers.IsEmpty() && Distance > FMath::Square(HostVisualDistance)) continue;
			// Filter before retention bias/combat priority: neither may keep a distant visual alive.
			// Client proxies and the authority simulation remain independent of local Actor selection.
			if (bClient && (Viewers.IsEmpty()
				|| (ClientVisualDistance > 0.0f && Distance > FMath::Square(ClientVisualDistance)))) continue;
#if !UE_BUILD_SHIPPING
			if (bTrace) TraceStage = TEXT("ActorBudget");
#endif
			const uint32 Revision = Samples.IsEmpty() ? (Resets.IsEmpty() ? 0 : Resets[I].Revision) : Samples[I].Current.MovementDiscontinuityRevision;
			// A small retention bias prevents swapping equal-distance actors at the budget boundary.
			if (Visuals.Contains(Ids[I].StableAgentId)) Distance *= .81;
			const float FacingYaw = Samples.IsEmpty() ? T[I].GetTransform().Rotator().Yaw : Samples[I].Current.Rotation.Yaw;
			Inputs.Add({Ids[I].StableAgentId, Ids[I].SpeciesId, RegionsView[I].CurrentRegionId, Position, FacingYaw, V[I], Revision, Distance});
		}
	});
	if (!bReady) return;
	for (auto& Input : Inputs)
		for (const auto* Relevance : CombatViewers)
			if (Relevance->IsOffered(Input.Id)) { Input.Distance = -1; break; }
	Inputs.Sort([](const FInput& A, const FInput& B) { return A.Distance == B.Distance ? A.Id < B.Id : A.Distance < B.Distance; });
#if !UE_BUILD_SHIPPING
	const int32 CandidateCount = Inputs.Num();
#endif
	if (Inputs.Num() > Budget.MaxActorRepresentations) Inputs.SetNum(Budget.MaxActorRepresentations);
	TSet<int64> Selected; for (const auto& Input : Inputs) Selected.Add(Input.Id);
	for (auto It = Visuals.CreateIterator(); It; ++It) if (!Selected.Contains(It.Key()))
	{
		if (auto* Actor = It.Value().Actor.Get()) { Actor->ClearBinding(); VisualPool.Add(Actor); }
		It.RemoveCurrent();
	}
	int32 Adds = 0;
	for (const auto& Input : Inputs)
	{
#if !UE_BUILD_SHIPPING
		const bool bTrace = TraceId == Input.Id;
		if (bTrace) TraceStage = TEXT("AddBudget");
#endif
		auto* Existing = Visuals.Find(Input.Id);
		if (!Existing || !Existing->Actor.IsValid())
		{
			if (Adds >= Budget.MaxAddsPerUpdate) continue;
            const auto* Fauna = FindFauna(Input.Species);
            const auto Class = Fauna ? Fauna->VisualClass : Input.Species == TEXT("Wolf") ? WolfActorClass : HerbivoreActorClass;
#if !UE_BUILD_SHIPPING
			if (bTrace) TraceStage = Class ? TEXT("SpawnFailed") : TEXT("MissingClass");
#endif
			if (!Class) continue;
			AEcoCreatureRepresentationActor* Actor = nullptr;
			for (int32 I = VisualPool.Num() - 1; I >= 0; --I)
				if (IsValid(VisualPool[I]) && VisualPool[I]->GetClass() == Class.Get())
				{ Actor = VisualPool[I]; VisualPool.RemoveAtSwap(I); break; }
			if (!Actor)
			{
				// A species switch can leave a full pool of incompatible classes.
				// Evict before spawning so active + hidden actors never exceed the cap.
				while (!VisualPool.IsEmpty() && Visuals.Num() + VisualPool.Num() >= Budget.MaxActorRepresentations)
				{ AEcoCreatureRepresentationActor* Retired = VisualPool.Pop(); if (Retired) Retired->Destroy(); }
				if (Visuals.Num() >= Budget.MaxActorRepresentations) continue;
				FActorSpawnParameters Params; Params.Owner = this; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				Actor = GetWorld()->SpawnActor<AEcoCreatureRepresentationActor>(Class, Input.Position, FRotator::ZeroRotator, Params);
			}
			if (!Actor) continue;
#if !UE_BUILD_SHIPPING
			if (bTrace) TraceStage = TEXT("BindRejected");
#endif
			if (!Actor->BindIdentity(Input.Id, Input.Species)) { Actor->Destroy(); continue; }
			auto& Binding = Visuals.FindOrAdd(Input.Id); Binding.Actor = Actor; Binding.Sequence = 0; ++Adds;
			Existing = &Binding;
		}
		auto& Binding = *Existing;
		FEcoCreatureVisualState State; State.StableAgentId = Input.Id; State.SpeciesId = Input.Species; State.RegionId = Input.Region;
		// Local render serial only. Transport acceptance occurs exclusively in FEcoClientPresentationFragment.
		State.Sequence = ++Binding.Sequence; State.WorldTime = GetWorld()->GetTimeSeconds(); State.Position = Input.Position;
		State.bDiscontinuity = Binding.Sequence == 1 || Binding.MovementRevision != Input.ResetRevision;
		Binding.MovementRevision = Input.ResetRevision;
		State.Velocity = Input.Visual.Velocity; State.bAlive = (Input.Visual.Flags & 1) != 0;
		State.bHasFacing = true;
		State.FacingYawDegrees = Input.FacingYaw;
		State.bEating = (Input.Visual.Flags & 2) != 0; State.bPursuingPrey = (Input.Visual.Flags & 4) != 0;
		State.Activity = Input.Visual.Activity <= uint8(EEcoSpeciesActivity::ExitWater) ? EEcoSpeciesActivity(Input.Visual.Activity) : EEcoSpeciesActivity::Hold;
		State.SenseSource = EEcoSenseSource((Input.Visual.Flags>>4)&3);
		State.FocusYawDegrees = float(Input.Visual.FocusYaw)/182.0389f;
        State.BodyScale=Input.Visual.BodyScale/128.f;State.Maturity=Input.Visual.Maturity/255.f;State.CoatTone=Input.Visual.CoatTone/255.f;
        State.BodyCondition=Input.Visual.BodyCondition/128.f;
        State.SocialSignal=uint8(EcoFaunaSocial::Signal(Input.Visual.SocialPresentation));
        State.SocialYaw=float(EcoFaunaSocial::Yaw(Input.Visual.SocialPresentation));
        State.bFaunaAlarm = (Input.Visual.SocialPresentation & EcoFaunaSocial::AlarmPulse) != 0;
        State.Appearance=EcoPresentationFlags::Appearance(Input.Visual.Flags);
		State.bHasVitals = (Input.Visual.Flags & 8) != 0;
		State.NormalizedHealth = Input.Visual.Health / 255.f; State.NormalizedEnergy = Input.Visual.Energy / 255.f;
		Binding.Actor->Footsteps->AudioSet = FootstepAudioSet;
#if !UE_BUILD_SHIPPING
		const bool bAccepted = Binding.Actor->ConsumeVisualState(State, Delta);
		if (bTrace) TraceStage = bAccepted ? TEXT("Bound") : TEXT("VisualStateRejected");
#else
		Binding.Actor->ConsumeVisualState(State, Delta);
#endif
	}
#if !UE_BUILD_SHIPPING
	if (TraceId > 0)
	{
		const auto* GameState = GetWorld()->GetGameState<AEcoGameState>();
		const auto* Binding = Visuals.Find(TraceId);
		const auto* Actor = Binding ? Binding->Actor.Get() : nullptr;
		const auto* Mesh = Actor ? Actor->CreatureMesh.Get() : nullptr;
		const auto* Fauna = FindFauna(TraceSpecies);
		const auto Class = Fauna ? Fauna->VisualClass : TraceSpecies == TEXT("Wolf") ? WolfActorClass : HerbivoreActorClass;
		UE_LOG(LogTemp, Display, TEXT("[Eco Visibility] World=%s Epoch=%d ID=%lld Species=%s Stage=%s Position=%s DistanceCm=%.1f RangeCm=%.1f Candidates=%d Selected=%d ActorBudget=%d Adds=%d AddBudget=%d Sequence=%u VisualRev=%u ResetRev=%u"),
			*GetWorld()->GetPathName(), GameState ? GameState->GetWorldEpoch() : 0, TraceId, *TraceSpecies.ToString(),
			TraceStage, *TracePosition.ToCompactString(), TraceDistanceCm, ClientVisualDistance, CandidateCount, Inputs.Num(),
			Budget.MaxActorRepresentations, Adds, Budget.MaxAddsPerUpdate, TraceSequence, TraceVisualRevision, TraceResetRevision);
		for (int32 Index = 0; Index < Viewers.Num(); ++Index)
		{
			UE_LOG(LogTemp, Display, TEXT("[Eco Visibility] ID=%lld Viewer=%d Camera=%s"), TraceId, Index, *Viewers[Index].ToCompactString());
		}
		UE_LOG(LogTemp, Display, TEXT("[Eco Visibility] ID=%lld ExpectedClass=%s Actor=%s ActorClass=%s Bound=%d Hidden=%d ActorPosition=%s Mesh=%s MeshVisible=%d Scale=%s"),
			TraceId, *GetPathNameSafe(Class.Get()), *GetNameSafe(Actor), *GetNameSafe(Actor ? Actor->GetClass() : nullptr),
			Actor && Actor->bBound, Actor && Actor->IsHidden(), Actor ? *Actor->GetActorLocation().ToCompactString() : TEXT("-"),
			*GetPathNameSafe(Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr), Mesh && Mesh->IsVisible(),
			Actor ? *Actor->GetActorScale3D().ToCompactString() : TEXT("-"));
	}
#endif
    TArray<int64> PresentedIds;
    for (const auto& Pair:Visuals)
        if (const auto* Actor=Pair.Value.Actor.Get(); Actor && Actor->bBound && !Actor->IsHidden() && Actor->VisualState.bAlive) PresentedIds.Add(Pair.Key);
    PresentedIds.Sort();
    for (auto It=GetWorld()->GetPlayerControllerIterator();It;++It)
        if (auto* PC=It->Get(); PC && PC->IsLocalController() && PC->GetPawn())
            if (auto* R=PC->GetPawn()->FindComponentByClass<UEcoCombatRelevanceComponent>()) R->SetPresented(this,PresentedIds);
	// Bound hidden pool separately: changing species mix must not grow retained actors without limit.
	const int32 MaxPool = FMath::Max(0, Budget.MaxActorRepresentations - Visuals.Num());
	while (VisualPool.Num() > MaxPool) { AEcoCreatureRepresentationActor* Actor = VisualPool.Pop(); if (Actor) Actor->Destroy(); }
}
