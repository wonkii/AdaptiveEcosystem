# 에디터에서 사슴 이동 계측 확인

06의 첫 구현은 Insights 타이머와 사슴 한 마리의 논리 스텝 CSV다. 평상시 이동과 긴급 반응을 동일 개체의 기록으로 비교한다. 이동 제어 변경은 관찰 결과를 바탕으로 07에서 선택한다.

검증 범위는 UE5.8의 직접 UBT `AdaptiveEcosystemEditor Win64 Development` 컴파일과 링크 성공까지다. 아래 절차는 에디터에서 직접 확인할 항목이다.

## 1. 준비

1. Development Editor 빌드를 연다. `/Game/JWJ/1102_Integration_JWJ`에서 **Play Offline**으로 PIE를 시작하고 서식지 스트리밍 및 초기 스폰을 기다린다.
2. 기존 Inspector가 있는 맵에서는 `Tab`으로 개체를 넘기고 패널의 사슴 ID를 적는다. Inspector를 사용할 수 없으면 Output Log에서 `[Eco Integration][Spawn]`과 `Species=Herbivore`를 찾아 `Id=` 값을 읽는다. 같은 플레이 세션의 ID를 쓴다.
3. 필요하면 콘솔에서 `eco.V3Debug 1`을 입력해 개체 머리 위 상태를 본다. 성능 비교를 위해서는 이 표시를 끈다.

## 2. 평상시 먹이 행동

1. 늑대와 거리를 둔 사슴을 고른다. 콘솔에서 `eco.MoveTrace.Agent <사슴 ID>`를 입력한다. 예를 들어 ID가 17이면 `eco.MoveTrace.Agent 17`이다.
2. 콘솔을 닫고 평상시 먹기·걷기를 지켜본다. 통합 시뮬레이션 225스텝(약 30초)이 지나면 자동으로 저장된다. 짧게 기록하려면 플레이가 진행되는 동안 `eco.MoveTrace.Agent 0`을 입력해 저장한다. PIE를 종료하기 전에 Output Log에서 `[EcoMotion] Saved`를 확인한다.
3. `AdaptiveEcosystem/Saved/Profiling/EcoMovement/Deer_<ID>_<GUID>.csv`를 연다. 대상이 없는 경우에는 샘플 없음 경고가 뜬다. 같은 ID를 다시 기록하려면 먼저 `0`을 적용한 다음 ID를 입력한다. CVar는 프로세스 범위이므로 다음 PIE에 자동으로 기록하지 않으려면 종료 전 `0`으로 돌린다.

| 보고 싶은 것 | CSV에서 비교할 열 |
|---|---|
| 실제 행동을 바꿨는가 | `behavior`, `seq`의 연속 행. `policy_read`는 판단 적용 여부이며 행동 전환 수와 구분 |
| 같은 먹이 행동에서 걸었다 멈추는가 | `food_before`, `food_ok`, `final_gait`, `intake`. `food_ok`는 기본 제어의 문턱이며 hook이 최종 보행을 바꿀 수 있음 |
| 제어와 실제 이동 중 어디서 달라졌는가 | `pre_hook_dx/dy`는 기본·도주 보정 뒤, 통합 hook 전 제어. `final_dx/dy`는 이동 시점 제어. `actual_dx/dy`는 초식 이동 직후의 실제 변위 |

행동 번호는 0 GRAZE, 1 FLEE, 2 HIDE, 3 FREEZE, 4 SLEEP이다. 보행 번호는 0 정지, 1 걷기, 2 뛰기다. 방향 관련 열은 Native 단위/스텝이다. `rejected_move_attempts`는 기존 지형 검사 실패 카운터의 변화량이므로 실제 정지 여부는 `actual_dx/dy`로 판단한다.

## 3. 늑대 접근과 도주

1. 살아 있는 늑대가 주변을 순찰하는 사슴을 골라 기록을 시작한다. 사슴이나 늑대에게 다가가 플레이어 영향이 섞이지 않도록 거리를 두고 관찰한다. 이번 단계는 기존 맵에서 자연 발생한 접근을 관찰하므로 30초 안에 도주가 없으면 다른 개체나 구간에서 다시 기록한다.
2. `pred_count_after`와 `keep_next`로 스텝 끝 감지·도주 결정을 찾고, 다음 행의 `keep_applied`, `behavior`, `startle_left`, `actual_dx/dy`를 본다. 같은 지역의 연속 `global_step`에서 비교한다. `pred_count`는 Native 지각의 감지 수이며 특정 늑대 ID를 포함하지 않는다.
3. `keep_applied=2`는 긴급 도주, `behavior=1`은 FLEE다. 놀람 정지가 있으면 적용 행과 첫 실제 변위 행이 다를 수 있다. `route_read`는 도주 경로 정책 적용 여부다. 사망 행의 이후 요청 값은 판단 근거로 사용하지 않는다.

## 4. Insights와 Mass Debugger

1. CPU·Frame·Bookmark 채널로 Insights 기록을 시작한다. Timing Insights의 GameThread에서 `EcoV3_IntegratedProcessor`를 찾는다. `EcoV3_GatherDeerChunk`, `EcoV3_GatherWolfChunk`, `EcoV3_SortByAgentId`, `EcoV3_BuildRegionViews`, `EcoV3_GlobalStep`, `EcoV3_NativeRegionStep`, `EcoV3_IntegratedHerbControl`, `EcoV3_HerbMoveValidation`, `EcoV3_ThreatMoveValidation`, `EcoV3_VisualSync`를 확인한다.
2. CSV 기록과 같이 실행한 세션은 `EcoMotionBegin`/ `EcoMotionEnd` Bookmark의 World·ID로 구간을 찾는다. 파일 저장 시점과 관찰 로그가 포함된 프레임은 순수 성능 표본에서 제외한다. 성능 기준선은 `eco.MoveTrace.Agent 0`에서 따로 기록한다. Native 내부의 추론과 지각은 이번 scope에서 합산되므로 그 둘을 개별 비용으로 해석하지 않는다.
3. Mass Debugger에서 해당 PIE World를 골라 `EcoPolicyV3IntegratedStepProcessor`와 사슴 Entity의 `FEcoBehaviorStateFragment`를 확인한다. `BehaviorId`, `GaitId`, `Seq`와 v3 Tag 구성을 보고, 화면에 노출되지 않는 Native 값은 CSV로 본다.

단계별 확인 후 CSV 한 개씩과 재현 맵·ID·관찰 내용을 남긴다. `seq`가 유지되면서 최종 방향이나 보행만 자주 바뀌면 07의 평상시 제어 후보를 검토한다. 실제 변위가 안정적인 구간에서 화면만 흔들리면 표시 단계가 다음 확인 지점이다.
