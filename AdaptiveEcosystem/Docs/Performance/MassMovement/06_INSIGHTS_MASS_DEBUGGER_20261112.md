# Unreal Insights·Mass Debugger 진단 및 계측 명세

2026-11-12. 기준 `jwj-1024 ef44df15` 작업 트리, 설치 UE5.8.3. 상태는 설계이며 이번 작업에서 새 trace/PIE/빌드 측정은 수행하지 않았다. [통합 성능 계획](RL_PRESERVING_EPIC_MASS_PLAN_20261112.md)의 M0를 구체화하고 [이동 자연스러움 설계](07_GRAZE_FLIGHT_LOCOMOTION_20261112.md)의 판단 근거를 만든다.

## 1. 이번 진단의 질문

1. 같은 사슴이 행동을 자주 바꾸는가, 같은 GRAZE 안에서 방향/보행만 바꾸는가?
2. 평상시 먹이 이동과 긴급 반응의 실제 지연은 얼마인가?
3. Native 입력·제어·실제 이동·화면 회전 중 어느 단계에서 불연속이 생기는가?
4. Gather/정렬/지각/정책/제어/지형/표현 중 어느 비용이 실제 frame/step 병목인가?

성능 분석은 Insights, 구성·Query·선택 개체 확인은 Mass Debugger를 사용한다. 개체 추적의 연결 키는 `RunId, WorldEpoch, NetMode, RegionId, StableAgentId, ContextGeneration, GlobalStep, RegionStep, Phase`로 제안한다. EntityHandle의 index/serial은 해당 World의 일시적 조회 키로만 기록한다. 서버/Client의 서로 다른 handle을 같은 개체로 대조하지 않는다.

## 2. 시작 전 기록

실제 HEAD/diff, 엔진 버전, 빌드 종류, CPU/GPU, 맵, seed, brain ID/hash, world profile, life/Social/이주 설정, 지역별 실제 alive 수, 카메라, 해상도, FPS 제한을 manifest에 적는다. 사용자의 미커밋 `EcoCreatureInspection.cpp` 변경은 현재 구현을 읽고 필요한 부분만 확장한다.

현재 스폰/통합 설정에는 총개체512 검사가 있다. 기존 계획의 지역당1,024·다지역 목표는 Unreal에서 바로 재현 가능하다고 가정하지 않는다. 첫 측정은 현재 정상 생성 가능한 수로 실행하고 Native 벤치마크1,024 결과를 별도 기록한다. 인구 상한 확장은 별도 변경·검증으로 처리한다.

관찰 문제 재현은 기존1102 맵에서 시작할 수 있다. 통제된 배치가 필요하면 owned fixture를 만든다. 원본 팀 맵의 authoring을 변경하지 않는다. trace 등 로컬 산출물은 프로젝트 `Saved/Profiling/EcoMovement/<RunId>/`에 두고 Git에는 요약 명세·실행 기록만 추가한다.

## 3. Unreal Insights 실행 절차

1. 설치 엔진의 `Engine/Binaries/Win64/UnrealInsights.exe` 또는 Editor의 Trace/Insights 메뉴로 Session Browser를 연다. 기존 활성 trace 여부를 먼저 확인한다.
2. 대상 게임/PIE에서 `Trace.Status`로 상태를 확인한다. Trace Control에서 대상 프로세스를 선택해 CPU·Frame·Bookmark 채널을 활성화하고 기록한다. 화면 비용이 의심되면 GPU도 포함한다. 채널 명칭·지원은 설치5.8에서 확인한다. 전용 서버와 Client는 각각 별도 세션이다.
3. `Trace.Bookmark EcoBaselineBegin`, `Trace.Bookmark EcoThreatIntroduced`, `Trace.Bookmark EcoBaselineEnd`로 구간을 표시한다. 이 명령은 기존 엔진 명령이며 실제 위협 생성은 fixture가 별도로 수행한다.
4. 같은 시나리오를 실행하고 trace를 중지한 뒤 `.utrace`를 연다. Timing Insights에서 GameThread의 긴 frame과 Native Step 구간을 선택한다.
5. Timers/Callers/Callees에서 inclusive/exclusive 시간, 호출 수, 지역 수, catch-up step 수를 함께 확인한다. 같은 frame의 부모·자식 scope 시간을 합해 중복 계산하지 않는다.
6. frame별/step별 timing event를 CSV/TSV로 export한다. p50/p95/p99는 원시 event duration에서 계산하며 전체 session 평균이나 Timers 집계 평균을 percentile로 대체하지 않는다. 측정 구간·표본 수를 결과와 함께 저장한다.

CPU 기준선은 상세 개체 로그·Mass Debugger refresh를 끈 실행에서도 측정한다. 메모리 추적·과도한 로그·모든 개체 동적 scope 이름은 측정 부하를 늘릴 수 있으므로 필요한 진단 실행에만 사용한다. [Trace Quick Start](https://dev.epicgames.com/documentation/unreal-engine/trace-quick-start-guide-in-unreal-engine), [Timing Insights](https://dev.epicgames.com/documentation/unreal-engine/timing-insights-in-unreal-engine?lang=en-US), [Timers export](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-timers-and-counters-tabs-in-unreal-insights-for-unreal-engine).

## 4. 추가 계측 구현 명세

아래 scope 이름은 구현 제안이다. 실제 기존 `TRACE_CPUPROFILER_EVENT_SCOPE`와 겹치는 것을 먼저 재사용한다.

| 대상 | scope/집계 후보 | 구현 위치 |
|---|---|---|
| Entity 수집 | Gather, Sort, BuildRegionViews | `EcoPolicyV3IntegratedProcessors.cpp` |
| Native 전체 | RegionStep, GlobalStep, CatchUpSteps | 통합 Processor의 Step 호출 경계 |
| 공간/지각 | GridBuild, CandidateQuery, Perception, 후보/수락/fallback 수 | `EcoSpatialIndexV3.h`, `EcoObservationV3.h`, `EcoWorldV3.h` |
| 정책 | Prepare, Inference, Commit, 재판단 수/실제 전환 수 | `EcoWorldV3.h`, `EcoRepertoire.h` |
| 실행 | BaseControl, FlightControl, IntegratedHook, TerrainValidation, Outcome | Native Step와 기존 hook |
| 표시 | NativeVisualSync, SyncVisuals, ActorConsume, animation/IK | 통합 Visual Processor, `EcoCreaturePresentation.cpp`, 표현 Actor |

Native 헤더는 엔진 비의존 계약을 유지한다. 하위 단계 계측은 기본 비활성인 계측 callback/기존 diagnostics 확장 등으로 구현하고, 실행 순서·난수·산술을 변경하지 않는다. 계측 없이 실행할 때 추가 할당·문자열 생성이 없는지 확인한다. phase별 성능 scope와 선택 개체의 상세 상태 기록은 별도 스위치로 둔다.

고정 이름 CPU scope와 Counter를 우선 사용한다. ID별 동적 scope를 매 스텝 생성하지 않는다. Bookmark는 시나리오 경계·행동 전환 등 드문 사건에 쓴다. 처음에는 선택 개체의 bounded ring buffer를 CSV로 export하고 bookmark에 RunId/ID/Step을 넣어 연결한다. custom trace event는 전용 analyzer가 있어야 해석할 수 있으므로 필요성이 확인된 후 추가한다. [Trace Developer Guide](https://dev.epicgames.com/documentation/unreal-engine/developer-guide-to-tracing-in-unreal-engine).

### 선택 개체 기록 형식

제안 기본은 개체1개, 최대8개, 최근30초의 논리 step 기록이다. 변경 가능한 설정으로 두며 버퍼 overflow/drop 수를 표시한다. 전체 개체는 집계만 한다.

- 시점과 상태는 위 연결 키, simulation time, wall-clock trace 대응 시각, brain revision, `Behavior`, `Seq`, `LockLeft`, `bReads`, `CommittedRequest`, `ExecutedBehavior`, `KeepActive`, `Route`, `Gait`를 기록한다. Seq는 wrap하므로 Step과 함께 비교한다.
- 입력은 현재 셀 먹이와 `bFoodOk`, 먹이 방향, 위협 감지 결과, 선택된 목표/lease/plan, 필요할 때 관측 배열을 기록한다.
- 출력은 기본 제어 직후, flight 직후, IntegratedControlHook 직후, 최종 제한 후의 방향/속도, 실제 변위, blocked 사유, 화면 yaw/animation speed를 담는다. 같은 스텝의 서로 다른 phase 값을 한 필드로 덮어쓰지 않는다.
- 단위는 Native unit/step와 cm/s, simulation seconds를 명시한다. 변환은 기존 helper를 사용한다. 사망/이주/restore/brain 교체 시 경계 이벤트와 새 generation을 기록한다.
- 기존 Inspector의 `local_forage_cell`, `motion_reason`, `contact_blocked_steps` 등을 재사용한다. `MotionReason` 하나로 모든 보정의 순서를 복원할 수 없으므로 전후 출력도 기록한다.

## 5. Mass Debugger 사용 절차와 현재 코드의 제한

1. PIE에서 `Tools > Debug > Mass Debugger`를 연다. Environment Picker에서 검사할 Server/Standalone World를 명시적으로 선택한다. Client 검사는 별도 World에서 수행한다.
2. Processors 탭에서 `EcoPolicyV3IntegratedStepProcessor`와 `EcoPolicyV3IntegratedVisualProcessor`를 찾고 Show Fragment Access를 연다. DeerQuery/WolfQuery의 required/optional/excluded 조건, 읽기/쓰기를 확인한다.
3. Entities 탭에서 대상 개체를 찾고 Select Fragments로 BehaviorState, IntegratedV3, Body, Control, Perception 등을 선택한다. StableAgentId가 표시되지 않으면 기존 Inspector가 제공하는 ID↔EntityHandle 매핑을 보강해 찾는다.
4. 사슴 PolicyV3Tag·늑대 ThreatV3Tag·Alive·CustomMovement 구성을 확인한다. GRAZE/FLEE 전환 때 Entity 구성은 유지되고 행동 필드가 변하는지 본다. 사망 시 Alive 제거는 별도 lifecycle 사건으로 기록한다.
5. Client에서는 Proxy/표현 구성을 확인하고 권위 Native 상태가 생성되지 않는지 검증한다. 하나의 World 결과를 다른 World의 권위 상태로 해석하지 않는다.

공식 문서상 필드 표시는 UPROPERTY에 한정된다. 현재 `Rep`, `Decision`, `Body`, `Control`의 Native 내부 멤버는 대부분 리플렉션 밖이므로 화면에 값이 안 보일 수 있다. 기존 `BehaviorId`, `PendingRequest`, `GaitId`, `Seq` 등 요약을 먼저 확인하고, 부족한 필드는 기존 Inspector snapshot 또는 최소한의 읽기 전용 debug mirror로 제공한다. Native 구조 전체에 UPROPERTY를 붙이거나 디버그 필드를 저장/복제 계약에 자동 편입하지 않는다.

쓰기 중단점은 `CreateEntityIterator` 계열을 사용하는 순회에 제한된다. 현재 통합 경로는 view/포인터를 모아 Native 함수에서 쓰므로 모든 Native write가 Mass Debugger 중단점에 잡힌다고 가정하지 않는다. `DecideHerb`, `Control`, `ApplyMove`, hook에는 IDE 조건부 중단점을 사용하고 선택 ID/row 매핑을 같은 phase에서 검증한다. 중단점 실행은 반응 지연·성능 측정에서 제외한다. [Epic Mass Debugger Overview](https://dev.epicgames.com/documentation/unreal-engine/mass-debugger-overview?lang=en-US).

## 6. 재현 시나리오와 보고

| 시나리오 | 확인할 값 |
|---|---|
| 위협 없는 풍부한 먹이 | GRAZE 유지, 정지/걷기 전환, 섭취량, 방향 변화 |
| 먹이 셀 경계·고갈 | bFoodOk, 셀/목표 전환과 gait 왕복의 상관관계 |
| 통제된 늑대 접근 | 최초 지각→panic 결정→Commit→첫 이동, 놀람 단계 각각의 지연 |
| 위협 해제·재접근 | 안전 조건·도주 유지/종료, 평상시 복귀 방향 |
| 낮은 FPS·catch-up | 실제 경과와 simulation time 차이, 한 frame step 수, 표시 보간 |
| 다른 Client 카메라 | 서버 실제 변위와 수신/표현 지연·animation 차이 |

동일 seed/config로 최소3회, 성능은30초 예열+180초 기록을 기존05와 맞춘다. 먼저 소수 개체의 원인을 찾고 이후 현재 지원 규모로 부하를 늘린다. 행동 자연스러움은 선택 개체10~30초 상세 기록으로 확인한다.

보고에는 재판단 횟수, 실제 행동 전환 횟수, 같은 행동 안의 gait 전환 횟수, 방향 변화, 각 phase 시간, 추적 drop 수를 분리한다. 기존 로그의 '먹기 개체·스텝 수'를 행동 전환 횟수로 사용하지 않는다. 첫 결과는 원인 후보별 지지/반증과 다음 변경 한 가지를 명시한다.

## 7. 완료 조건과 다음 구현

첫 구현은 phase 계측과 선택 개체 기록, Inspector 매핑까지다. 계측 OFF/ON에서 동일 입력의 관측·정책·행동·RNG·자원 결과가 같고, 선택 개체 수/버퍼 상한을 준수해야 한다. overhead는 OFF/집계/상세 세 모드로 보고하며 허용 예산은 baseline 측정 후 정한다. 기능 동작 변경은07의 진입 조건을 따른다.

C++ 변경 시 AGENTS의 직접 Editor UBT 규칙과 실행 중 엔진/UBT 중복 금지를 따른다. 관련 기존 자동화와 선택 기록의 phase/ID 일치·restore 무효화 회귀를 검증한다. 실제 실행 기록에는 사용한 명령·파일·실패·미검증 범위를 적는다.

다음 요청 예시: “06 문서의 계측과 선택 개체 진단을 구현하고 현재1102의 평상시 GRAZE와 위협 접근을 비교해 원인을 기록해 줘. 기존 판단·이동 수식은 계측 결과 확보까지 유지해 줘.”

공식 웹 페이지는 기본 버전이 바뀔 수 있다. 2026-11-12 조회 시 Trace Developer Guide는5.9, Mass Debugger/Timing 안내는5.8로 표시됐다. 실제 API/UI는 설치5.8.3과 대조한다.
