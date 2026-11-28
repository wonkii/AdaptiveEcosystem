#include "AI/Behavior/EcoSpeciesAIFragments.h"
void FEcoSpeciesAIMoveFragment::SetTarget(const FVector& Position, float InSpeed, float InRadius, double Now, const UEcoSpeciesAIProfile& P)
{
    if (Position.ContainsNaN()) { Hold(Now); return; }
    Speed=FMath::Clamp(InSpeed,0.f,P.RunSpeed); Radius=FMath::Max(1.f,InRadius);
    const bool bWaitingForRetry = Status == EEcoAINavigationStatus::Failed && Now < RetryAfter;
    const bool bFollowingDuringCooldown = Status == EEcoAINavigationStatus::Following && Now < RetryAfter;
    if (bWaitingForRetry || bFollowingDuringCooldown) return;

    // After arrival, a moving target can leave the arrival radius without crossing RetargetDistance.
    const float GoalTolerance = Status == EEcoAINavigationStatus::Arrived
        ? FMath::Min(Radius, P.RetargetDistance) : P.RetargetDistance;
    if (Revision==0 || Status==EEcoAINavigationStatus::None || Status==EEcoAINavigationStatus::Failed
        || FVector::DistSquared(Position,Target)>FMath::Square(GoalTolerance))
    {
        Target=Position; ++Revision; RequestedAt=Now; Deadline=Now+P.PathTimeout;
        RetryAfter=Now;
        Status=EEcoAINavigationStatus::Pending; BestDistance=MAX_flt; LastProgress=Now;
    }
}
void FEcoSpeciesAIMoveFragment::Hold(double Now)
{
    Speed=0;
    if (Status!=EEcoAINavigationStatus::None) { ++Revision; Status=EEcoAINavigationStatus::None; RequestedAt=Now; }
}
void FEcoSpeciesAIFeedingFragment::Add(double Start,double End,FName Region)
{
    if (!FMath::IsFinite(Start)||!FMath::IsFinite(End)||End<=Start) return;
    if (!Segments.IsEmpty() && Segments.Last().Region==Region && FMath::Abs(Segments.Last().End-Start)<.001)
        Segments.Last().End=End;
    else if (Segments.Num()<32) Segments.Add({Start,End,Region});
    else DroppedSeconds+=End-Start;
}
double FEcoSpeciesAIFeedingFragment::ConsumeThrough(double Time,FName Region)
{
    double Seconds=0;
    for (auto& Segment : Segments)
    {
        const double End=FMath::Min(Time,Segment.End);
        if (End>Segment.Start) { if (Segment.Region==Region) Seconds+=End-Segment.Start; else DroppedSeconds+=End-Segment.Start; Segment.Start=End; }
    }
    Segments.RemoveAll([](const auto& S){return S.Start>=S.End;});
    return Seconds;
}
