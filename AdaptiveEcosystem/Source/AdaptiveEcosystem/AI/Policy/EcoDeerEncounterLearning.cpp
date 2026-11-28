#include "AI/Policy/EcoDeerEncounterLearning.h"

#include "AI/Policy/EcoPolicyV3IntegratedSubsystem.h"
#include "Algo/Count.h"
#include "Async/Async.h"
#include "Creature/Runtime/EcoCreatureIntegrationSpawner.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include <cmath>

DEFINE_LOG_CATEGORY_STATIC(LogEcoDeerEncounter, Log, All);

namespace
{
    constexpr int32 FeatureCount = 8;
    constexpr int32 ActionCount = 3;
    constexpr int32 WeightCount = FeatureCount * ActionCount;
    constexpr uint64 DecisionSteps = 3;
    constexpr int32 MaxEpisodeSteps = 24;
    constexpr int32 MaxOpen = 128;
    constexpr int32 MaxClosed = 1024;
    constexpr int32 MaxRegions = 64;
    constexpr int32 MinOutcomes = 6;
    constexpr int32 MaxOutcomes = 192;
    constexpr double WeightLimit = 8.;
    constexpr TCHAR Contract[] = TEXT("deer-encounter-2");
    using FFeatures = TStaticArray<double, FeatureCount>;
    using FWeights = TStaticArray<double, WeightCount>;
    using FProbabilities = TStaticArray<double, ActionCount>;

    // Authored starting point for the experiment. Only the regional output weights learn.
    const FWeights InitialWeights = {
        .9, 1.5, -.3, -.3, -.6, .7, 0., 0.,
        .1, -.2, .3, 0., .2, -.2, .4, 0.,
        -.7, -1.3, .3, .3, .5, -.6, 0., .3
    };

    FProbabilities Probabilities(const FWeights& Weights, const FFeatures& X)
    {
        FProbabilities P;
        double Max = -DBL_MAX;
        for (int32 A = 0; A < ActionCount; ++A)
        {
            double Score = 0.;
            for (int32 K = 0; K < FeatureCount; ++K) Score += Weights[A * FeatureCount + K] * X[K];
            P[A] = Score;
            Max = FMath::Max(Max, Score);
        }
        double Sum = 0.;
        for (double& Score : P) { Score = FMath::Exp(Score - Max); Sum += Score; }
        for (double& Score : P) Score /= Sum;
        return P;
    }

    template <uint32 N>
    TArray<TSharedPtr<FJsonValue>> Numbers(const TStaticArray<double, N>& Values)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        for (double Value : Values) Result.Add(MakeShared<FJsonValueNumber>(Value));
        return Result;
    }

    template <uint32 N>
    bool ReadNumbers(const FJsonObject& Object, const TCHAR* Field, TStaticArray<double, N>& Out, double Limit)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
        if (!Object.TryGetArrayField(Field, Values) || Values->Num() != Out.Num()) return false;
        for (int32 I = 0; I < Out.Num(); ++I)
        {
            if (!(*Values)[I]->TryGetNumber(Out[I]) || !FMath::IsFinite(Out[I]) || FMath::Abs(Out[I]) > Limit)
                return false;
        }
        return true;
    }

    struct FTransition
    {
        FFeatures X;
        double Probability = 0., Damage = 0., Feeding = 0.;
        int32 Action = 0;
        bool bConfirmed = false;
    };

    struct FEncounter
    {
        uint64 SelectedAt = 0;
        int32 Action = 0;
        TArray<FTransition> Steps;
    };

    struct FEpisode
    {
        TArray<FTransition> Steps;
        TOptional<FFeatures> Bootstrap;
        bool bHoldout = false;
    };

    struct FOutcome
    {
        double Fraction;
        bool bFeeding;
    };

    struct FRegion
    {
        explicit FRegion(FName Region) : Random(GetTypeHash(Region.ToString())) {}
        FRandomStream Random;
        FWeights Weights = InitialWeights;
        TOptional<FWeights> Prior;
        int32 Version = 0;
        int32 EpisodeCount = 0;
        int32 ClosedSteps = 0;
        int32 Missed = 0;
        TMap<int64, FEncounter> Open;
        TArray<FEpisode> Closed;
        TArray<FOutcome> Outcomes;

        void ResetDecisions()
        {
            Open.Reset();
            Closed.Reset();
            ClosedSteps = 0;
        }

        void ResetExperience()
        {
            ResetDecisions();
            Outcomes.Reset();
        }

        void Complete(TArray<FTransition>& Steps, TOptional<FFeatures> Bootstrap = {})
        {
            if (!Steps.IsEmpty() && !Steps.Last().bConfirmed) Steps.Pop();
            if (Steps.IsEmpty()) return;
            if (ClosedSteps + Steps.Num() > MaxClosed) { ++Missed; Steps.Reset(); return; }
            ClosedSteps += Steps.Num();
            // Split whole encounters in advance of training, independent of observed rewards.
            const bool bHoldout = (++EpisodeCount % 3) == 0;
            Closed.Add({MoveTemp(Steps), MoveTemp(Bootstrap), bHoldout});
        }
    };

    struct FResult
    {
        FString Message;
        TOptional<FWeights> Weights;
        bool bWorkerFailed = false;
    };

    FString PythonExe()
    {
#if PLATFORM_WINDOWS
        return FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
#else
        return FPlatformMisc::GetEnvironmentVariable(TEXT("ECO_DEER_PYTHON"));
#endif
    }

    FString WorkerScript()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Tools/DeerEncounterLearning/train.py"));
    }
}

struct FEcoDeerEncounterSession
{
    TWeakObjectPtr<AEcoCreatureIntegrationSpawner> Owner;
    bool bOnline = false;
    int32 Epoch = 0, Environment = 0;
    FString Brain;
    FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
    TMap<FName, FRegion> Regions;
};

struct FEcoDeerEncounterJob
{
    TAtomic<bool> Cancelled{false};
    int32 Generation = 0, Parent = 0;
    FName Region;
    TFuture<FResult> Future;
};

namespace
{
    FResult RunJob(const FEcoDeerEncounterJob& Job, const FString& Exe, const FString& Script,
        const FString& Directory, const FString& Token, FWeights Weights, TArray<FEpisode> Episodes, TArray<FOutcome> Outcomes)
    {
        if (Job.Cancelled.Load()) return {TEXT("cancelled")};
        auto Input = MakeShared<FJsonObject>();
        Input->SetStringField(TEXT("contract"), Contract);
        Input->SetStringField(TEXT("token"), Token);
        Input->SetArrayField(TEXT("weights"), Numbers(Weights));
        TArray<TSharedPtr<FJsonValue>> Encoded;
        for (const FEpisode& Episode : Episodes)
        {
            auto Record = MakeShared<FJsonObject>();
            Record->SetBoolField(TEXT("holdout"), Episode.bHoldout);
            if (Episode.Bootstrap) Record->SetArrayField(TEXT("bootstrap"), Numbers(*Episode.Bootstrap));
            TArray<TSharedPtr<FJsonValue>> Steps;
            for (const FTransition& Step : Episode.Steps)
            {
                auto Sample = MakeShared<FJsonObject>();
                Sample->SetArrayField(TEXT("x"), Numbers(Step.X));
                Sample->SetNumberField(TEXT("action"), Step.Action);
                Sample->SetNumberField(TEXT("p"), Step.Probability);
                Sample->SetNumberField(TEXT("damage"), Step.Damage);
                Sample->SetNumberField(TEXT("feed"), Step.Feeding);
                Steps.Add(MakeShared<FJsonValueObject>(Sample));
            }
            Record->SetArrayField(TEXT("steps"), MoveTemp(Steps));
            Encoded.Add(MakeShared<FJsonValueObject>(Record));
        }
        Input->SetArrayField(TEXT("episodes"), MoveTemp(Encoded));
        TArray<TSharedPtr<FJsonValue>> RecordedOutcomes;
        for (const FOutcome& Outcome : Outcomes)
        {
            auto Record = MakeShared<FJsonObject>();
            Record->SetBoolField(TEXT("feeding"), Outcome.bFeeding);
            Record->SetNumberField(TEXT("fraction"), Outcome.Fraction);
            RecordedOutcomes.Add(MakeShared<FJsonValueObject>(Record));
        }
        Input->SetArrayField(TEXT("outcomes"), MoveTemp(RecordedOutcomes));
        FString Payload;
        const FString Request = Directory / TEXT("batch.json");
        const FString Reply = Directory / TEXT("result.json");
        if (!FJsonSerializer::Serialize(Input, TJsonWriterFactory<>::Create(&Payload))
            || !IFileManager::Get().MakeDirectory(*Directory, true)
            || !FFileHelper::SaveStringToFile(Payload, *(Request + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            || !IFileManager::Get().Move(*Request, *(Request + TEXT(".tmp"))))
            return {TEXT("batch write failed"), {}, true};
        if (Job.Cancelled.Load()) return {TEXT("cancelled")};

        const FString Args = FString::Printf(TEXT("-I -B \"%s\" \"%s\" \"%s\""), *Script, *Request, *Reply);
        FProcHandle Process = FPlatformProcess::CreateProc(*Exe, *Args, false, true, true, nullptr, 0, nullptr, nullptr);
        if (!Process.IsValid()) return {TEXT("Python launch failed"), {}, true};
        const double Deadline = FPlatformTime::Seconds() + 30.;
        while (FPlatformProcess::IsProcRunning(Process) && !Job.Cancelled.Load() && FPlatformTime::Seconds() < Deadline)
            FPlatformProcess::Sleep(.025f);
        const bool bStopped = FPlatformProcess::IsProcRunning(Process);
        if (bStopped) FPlatformProcess::TerminateProc(Process);
        int32 ExitCode = -1;
        // Reap the child on the worker before allowing a new job.
        FPlatformProcess::WaitForProc(Process);
        FPlatformProcess::GetProcReturnCode(Process, &ExitCode);
        FPlatformProcess::CloseProc(Process);
        if (Job.Cancelled.Load()) return {TEXT("cancelled")};
        if (bStopped || ExitCode != 0) return {TEXT("Python failed or timed out"), {}, true};

        FString Json;
        TSharedPtr<FJsonObject> Result;
        FString ContractId, ResultToken, Message;
        bool bAccepted = false;
        const int64 Size = IFileManager::Get().FileSize(*Reply);
        if (Size <= 0 || Size > 16384 || !FFileHelper::LoadFileToString(Json, *Reply)
            || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Result) || !Result
            || !Result->TryGetStringField(TEXT("contract"), ContractId) || ContractId != Contract
            || !Result->TryGetStringField(TEXT("token"), ResultToken) || ResultToken != Token
            || !Result->TryGetStringField(TEXT("message"), Message)
            || !Result->TryGetBoolField(TEXT("accepted"), bAccepted))
            return {TEXT("invalid candidate response"), {}, true};
        if (!bAccepted) return {Message};
        FWeights Candidate;
        FProbabilities Expected;
        const FFeatures Probe = {1., -.2, .3, -.4, .5, -.6, .7, -.8};
        if (!ReadNumbers(*Result, TEXT("weights"), Candidate, WeightLimit)
            || !ReadNumbers(*Result, TEXT("probe"), Expected, 1.))
            return {TEXT("invalid candidate tensor"), {}, true};
        const auto Actual = Probabilities(Candidate, Probe);
        for (int32 I = 0; I < ActionCount; ++I)
            if (!FMath::IsNearlyEqual(Actual[I], Expected[I], 1.e-8))
                return {TEXT("Python/C++ inference differs"), {}, true};
        return {Message, Candidate};
    }
}

bool UEcoDeerEncounterLearning::DoesSupportWorldType(EWorldType::Type Type) const
{
    return !UE_BUILD_SHIPPING && (Type == EWorldType::PIE || Type == EWorldType::Game);
}

void UEcoDeerEncounterLearning::Deinitialize()
{
    ResetForRestore();
    Job.Reset();
    Super::Deinitialize();
}

void UEcoDeerEncounterLearning::StopCollection()
{
    ++Generation;
    if (Job) Job->Cancelled.Store(true);
    if (!Session) return;
    Session->bOnline = false;
    for (auto& Pair : Session->Regions) Pair.Value.ResetExperience();
}

void UEcoDeerEncounterLearning::ResetForRestore()
{
    StopCollection();
    Session.Reset();
    PendingMode.Reset();
    bRollback = false;
}

bool UEcoDeerEncounterLearning::IsCollecting() const
{
    return Session && Session->bOnline;
}

void UEcoDeerEncounterLearning::RequestMode(const TArray<FString>& Args, FOutputDevice& Output)
{
    if (GetWorld()->GetNetMode() == NM_Client)
    {
        Output.Log(TEXT("Deer encounter mode requires the authority world."));
        return;
    }
    if (Args.Num() == 1)
    {
        if (Args[0].Equals(TEXT("Off"), ESearchCase::IgnoreCase)) PendingMode = EMode::Off;
        else if (Args[0].Equals(TEXT("Frozen"), ESearchCase::IgnoreCase)) PendingMode = EMode::Frozen;
        else if (Args[0].Equals(TEXT("Online"), ESearchCase::IgnoreCase)) PendingMode = EMode::Online;
        else if (Args[0].Equals(TEXT("Rollback"), ESearchCase::IgnoreCase)) bRollback = true;
        else { Describe(Output); return; }
        Output.Log(TEXT("Deer encounter request queued for the next Native step."));
        return;
    }
    Describe(Output);
}

void UEcoDeerEncounterLearning::Describe(FOutputDevice& Output) const
{
    Output.Logf(TEXT("Deer encounter %s | job=%s | eco.DeerEncounter.Mode Off|Frozen|Online|Rollback"),
        Session ? (Session->bOnline ? TEXT("Online") : TEXT("Frozen")) : TEXT("Off"), Job ? TEXT("pending") : TEXT("idle"));
    if (Session)
        for (const auto& Pair : Session->Regions)
        {
            const auto& Region = Pair.Value;
            const int32 Feeding = int32(Algo::CountIf(Region.Outcomes, [](const FOutcome& Outcome) { return Outcome.bFeeding; }));
            Output.Logf(TEXT("  %s v%d | open=%d buffered=%d pending_damage=%d pending_feeding=%d dropped=%d"),
                *Pair.Key.ToString(), Region.Version, Region.Open.Num(), Region.ClosedSteps,
                Region.Outcomes.Num() - Feeding, Feeding, Region.Missed);
        }
}

void UEcoDeerEncounterLearning::ApplyMode(EMode Mode, AEcoCreatureIntegrationSpawner* Owner)
{
    if (Mode == EMode::Off)
    {
        StopCollection();
        Session.Reset();
    }
    else
    {
        if (!Owner || !Owner->bAllowDeerEncounterLearning || !Owner->UsesDeerPlayerExperience())
        {
            UE_LOG(LogEcoDeerEncounter, Warning, TEXT("Mode refused: enable the learning and experience settings on the ready v3 life spawner."));
            return;
        }
        if (Session && Session->bOnline == (Mode == EMode::Online)) return;
        if (Mode == EMode::Online && (Job || !FPaths::FileExists(PythonExe()) || !FPaths::FileExists(WorkerScript())))
        {
            UE_LOG(LogEcoDeerEncounter, Warning, TEXT("Online refused: waiting for previous worker, or Python/trainer missing."));
            return;
        }
        if (!Session)
        {
            Session = MakeShared<FEcoDeerEncounterSession>();
            Session->Owner = Owner;
        }
        if (Mode == EMode::Frozen && Session->bOnline) StopCollection();
        Session->bOnline = Mode == EMode::Online;
        // The first collected decision starts at the Online boundary.
        for (auto& Pair : Session->Regions) Pair.Value.Open.Reset();
    }
    UE_LOG(LogEcoDeerEncounter, Display, TEXT("Mode=%s"), Mode == EMode::Off ? TEXT("Off") :
        Mode == EMode::Frozen ? TEXT("Frozen") : TEXT("Online"));
}

void UEcoDeerEncounterLearning::Boundary(AEcoCreatureIntegrationSpawner* Owner, int32 Epoch, int32 Environment, const FString& Brain)
{
    if (!Session && !Job && !PendingMode && !bRollback) return;
    if (Session && (Session->Owner.Get() != Owner || !Owner || !Owner->bAllowDeerEncounterLearning
        || !Owner->UsesDeerPlayerExperience() || Session->Epoch != Epoch))
        ResetForRestore();
    if (IsCollecting() && (Session->Environment != Environment || Session->Brain != Brain))
    {
        StopCollection();
        UE_LOG(LogEcoDeerEncounter, Display, TEXT("Base policy or environment revision changed. Mode=Frozen"));
    }
    if (PendingMode)
    {
        ApplyMode(*PendingMode, Owner);
        PendingMode.Reset();
    }
    if (Session)
    {
        Session->Epoch = Epoch;
        Session->Environment = Environment;
        Session->Brain = Brain;
        if (bRollback)
        {
            const bool bOnline = Session->bOnline;
            StopCollection();
            Session->bOnline = bOnline;
            for (auto& Pair : Session->Regions)
            {
                auto& Region = Pair.Value;
                if (!Region.Prior) continue;
                Region.Weights = *Region.Prior;
                Region.Prior.Reset();
                ++Region.Version;
                UE_LOG(LogEcoDeerEncounter, Display, TEXT("Rolled back %s to previous weights (v%d)"), *Pair.Key.ToString(), Region.Version);
            }
        }
    }
    bRollback = false;
    PollJob();
    if (IsCollecting() && !Job) SubmitJob();
}

TOptional<EEcoDeerPlayerResponse> UEcoDeerEncounterLearning::Decide(FName RegionId, int64 Deer, uint64 Step,
    double DistanceRatio, double ClosingSpeed, double Energy, double Damage, double Feeding)
{
    if (!Session || RegionId.IsNone() || !FMath::IsFinite(DistanceRatio) || !FMath::IsFinite(ClosingSpeed)
        || !FMath::IsFinite(Energy) || !FMath::IsFinite(Damage) || !FMath::IsFinite(Feeding)) return {};
    auto* Region = Session->Regions.Find(RegionId);
    if (!Region && Session->Regions.Num() < MaxRegions) Region = &Session->Regions.Add(RegionId, FRegion(RegionId));
    if (!Region) return {};
    auto* Encounter = Region->Open.Find(Deer);
    if (Encounter && Step - Encounter->SelectedAt < DecisionSteps)
        return static_cast<EEcoDeerPlayerResponse>(Encounter->Action);
    if (!Encounter && Region->Open.Num() >= MaxOpen) return {};

    const double XDistance = 2. * FMath::Clamp(DistanceRatio, 0., 1.) - 1.;
    const double XSpeed = FMath::Clamp(ClosingSpeed / 900., -1., 1.);
    const FFeatures X = {1., std::tanh(XDistance), std::tanh(XSpeed), std::tanh(2. * FMath::Clamp(Energy, 0., 1.) - 1.),
        std::tanh(FMath::Clamp(Damage, 0., 1.)), std::tanh(FMath::Clamp(Feeding, 0., 1.)),
        std::tanh(XSpeed - XDistance), std::tanh(FMath::Clamp(Damage, 0., 1.) - XDistance)};

    if (!Encounter) Encounter = &Region->Open.Add(Deer);
    if (Encounter->Steps.Num() >= MaxEpisodeSteps)
        Region->Complete(Encounter->Steps, X);
    const auto P = Probabilities(Region->Weights, X);
    const double Sample = Region->Random.GetFraction();
    const int32 Action = Sample < P[0] ? 0 : Sample < P[0] + P[1] ? 1 : 2;
    Encounter->SelectedAt = Step;
    Encounter->Action = Action;
    if (Session->bOnline) Encounter->Steps.Add({X, P[Action], 0., 0., Action, false});
    return static_cast<EEcoDeerPlayerResponse>(Action);
}

void UEcoDeerEncounterLearning::EndEncounter(FName RegionId, int64 Deer, bool bDiscard)
{
    auto* Region = Session ? Session->Regions.Find(RegionId) : nullptr;
    if (!Region) return;
    if (auto* Encounter = Region->Open.Find(Deer))
    {
        if (!bDiscard) Region->Complete(Encounter->Steps);
        Region->Open.Remove(Deer);
    }
}

void UEcoDeerEncounterLearning::PruneEncounters(const TMap<int64, FName>& LiveRegions)
{
    if (!Session) return;
    for (auto& Pair : Session->Regions)
        for (auto It = Pair.Value.Open.CreateIterator(); It; ++It)
        {
            const FName* Region = LiveRegions.Find(It.Key());
            if (!Region) Pair.Value.Complete(It.Value().Steps);
            if (!Region || *Region != Pair.Key) It.RemoveCurrent();
        }
}

void UEcoDeerEncounterLearning::ConfirmAction(FName RegionId, int64 Deer)
{
    auto* Region = IsCollecting() ? Session->Regions.Find(RegionId) : nullptr;
    auto* Encounter = Region ? Region->Open.Find(Deer) : nullptr;
    if (Encounter && !Encounter->Steps.IsEmpty()) Encounter->Steps.Last().bConfirmed = true;
}

void UEcoDeerEncounterLearning::RecordOutcome(const AEcoCreatureIntegrationSpawner& Owner, FName RegionId,
    int64 Deer, bool bFeeding, double Fraction)
{
    if (!IsCollecting() || Session->Owner.Get() != &Owner || RegionId.IsNone() || !FMath::IsFinite(Fraction) || Fraction <= 0.) return;
    auto* Region = Session->Regions.Find(RegionId);
    if (!Region && Session->Regions.Num() < MaxRegions) Region = &Session->Regions.Add(RegionId, FRegion(RegionId));
    if (!Region) return;
    const double Magnitude = FMath::Min(Fraction, 1.);
    // An authoritative outcome supplies regional feedback even when there was no policy action to credit.
    if (Region->Outcomes.Num() < MaxOutcomes) Region->Outcomes.Add({Magnitude, bFeeding});
    else ++Region->Missed;

    auto* Encounter = Region->Open.Find(Deer);
    if (!Encounter || Encounter->Steps.IsEmpty() || !Encounter->Steps.Last().bConfirmed) return;
    auto& Sample = Encounter->Steps.Last();
    (bFeeding ? Sample.Feeding : Sample.Damage) += Magnitude;
    if (!bFeeding) EndEncounter(RegionId, Deer);
}

void UEcoDeerEncounterLearning::PollJob()
{
    if (!Job || !Job->Future.IsReady()) return;
    const auto Finished = MoveTemp(Job);
    const FResult Result = Finished->Future.Get();
    if (!IsCollecting() || Finished->Generation != Generation || Finished->Cancelled.Load()) return;
    auto* Region = Session->Regions.Find(Finished->Region);
    if (!Region || Region->Version != Finished->Parent) return;
    if (Result.bWorkerFailed) StopCollection();
    if (!Result.Weights)
    {
        UE_LOG(LogEcoDeerEncounter, Display, TEXT("%s v%d rejected: %s"), *Finished->Region.ToString(), Region->Version, *Result.Message);
        return;
    }
    Region->Prior = Region->Weights;
    Region->Weights = *Result.Weights;
    ++Region->Version;
    // Outcomes arriving while the worker ran are valid under either policy.
    Region->ResetDecisions();
    UE_LOG(LogEcoDeerEncounter, Display, TEXT("%s v%d accepted (parent v%d): %s"),
        *Finished->Region.ToString(), Region->Version, Finished->Parent, *Result.Message);
}

void UEcoDeerEncounterLearning::SubmitJob()
{
    for (auto& Pair : Session->Regions)
    {
        auto& Region = Pair.Value;
        if (Region.Outcomes.Num() < MinOutcomes) continue;
        Job = MakeShared<FEcoDeerEncounterJob, ESPMode::ThreadSafe>();
        Job->Generation = Generation;
        Job->Parent = Region.Version;
        Job->Region = Pair.Key;
        const FString Token = FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DeerEncounterLearning") /
            Session->Id / FString::Printf(TEXT("%08x"), GetTypeHash(Pair.Key.ToString())) / Token);
        auto Batch = MoveTemp(Region.Closed);
        TArray<FOutcome> Outcomes = MoveTemp(Region.Outcomes);
        const int32 Feeding = int32(Algo::CountIf(Outcomes, [](const FOutcome& Outcome) { return Outcome.bFeeding; }));
        UE_LOG(LogEcoDeerEncounter, Display, TEXT("%s v%d training: damage=%d feeding=%d decisions=%d"),
            *Pair.Key.ToString(), Region.Version, Outcomes.Num() - Feeding, Feeding, Region.ClosedSteps);
        Region.Outcomes.Reset();
        Region.Closed.Reset();
        Region.ClosedSteps = 0;
        Job->Future = Async(EAsyncExecution::ThreadPool, [Running = Job, Exe = PythonExe(), Script = WorkerScript(),
            Directory, Token, Weights = Region.Weights, Batch = MoveTemp(Batch), Outcomes = MoveTemp(Outcomes)]() mutable
        {
            return RunJob(*Running, Exe, Script, Directory, Token, Weights, MoveTemp(Batch), MoveTemp(Outcomes));
        });
        return;
    }
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorldArgsAndOutputDevice DeerEncounterMode(
    TEXT("eco.DeerEncounter.Mode"), TEXT("Set the authority world deer encounter mode, or show its status."),
    FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateLambda(
        [](const TArray<FString>& Args, UWorld* World, FOutputDevice& Output)
        {
            if (auto* Learning = World ? World->GetSubsystem<UEcoDeerEncounterLearning>() : nullptr)
                Learning->RequestMode(Args, Output);
        }));
#endif
