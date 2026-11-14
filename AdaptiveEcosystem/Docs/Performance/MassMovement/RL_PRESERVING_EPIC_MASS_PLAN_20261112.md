# RL 판단을 유지하는 Epic Mass 구조 적용 계획·개발 명세

작성·소스 확인: **2026-11-12**. 기준 branch `jwj-1024`, HEAD `ef44df1597589416b16fceaa496341541a4400a3` 및 당시 작업 트리. 설치 엔진 `UE 5.8.3`, CL `58210709`. **계획 문서이며 코드·에셋·기본값을 변경하거나 새로운 빌드·PIE·성능 측정을 수행하지 않았다.**

사용자 요구는 **강화학습이 개체의 행동을 판단하는 로직을 유지하면서 실행 단위·스레드·주변 탐색·조향·갱신 주기·표현을 개선**하는 것이다. 이 문서는 여섯 비교 지점의 통합 작업 계약이다. 기존 [기능별 명세](README.md)와 [동족 탐색 P0~P3](../BOUNDED_NEIGHBOR_SEARCH_SPEC_20261110.md)를 연결하며, 정확 탐색 P0/P1을 첫 구현으로 유지한다. 기존 문서의 근사 관측·느린 응집 제안은 아래 보존 경계를 통과한 별도 실험에만 해당한다.

## 1. 보존할 것과 개선할 것

### 1.1 RL 보존 계약

| 반드시 보존 | 개발 규칙 |
|---|---|
| 두뇌·추론 | 기본 R7e 24관측/5행동·mask5 및 기존 시작 두뇌 선택/호환성 검사. R1b fallback, V1 7관측/4출력은 각각 별도 계약. 모델 파일·가중치·추론 수식·argmax/동점 처리를 변경하지 않는다 |
| 정책 입력 | 관측 순서·정규화·clamp·시야/반경·자기 제외·동족 수/도주 비율의 의미와 생산 시점을 보존한다. 배열 크기가 같다는 것만으로 호환이라고 판단하지 않는다 |
| 행동 결정 | `Prepare`/mask/요청 읽기 조건·사건·잠금·길 선택과 `Commit` 순서 유지. 기존 `request`/`flight_override`/`executed` 구분 유지. 새 StateTree/BT/FSM으로 행동을 대신 고르지 않는다 |
| 기존 세계 규칙 | 기존 도주 반사·여행·Social opt-in·life·안전/은신처 제약 보존. 성능 변경을 이유로 새 행동 override를 추가하지 않는다 |
| 실행 권위 | Server/Standalone만 논리 실행. 각 개체의 위치·대사·섭식 writer는 기존 Native 하나. M4 거래/인구/lifecycle, Client 수신 표현, SocialControl 기본값·맵별 opt-in 보존 |
| 시간·난수 | Native `kStepSeconds=8/60`초, 현재 단계별 RNG draw 및 원본 row 누산 순서 유지. 카메라 거리에 따라 RL 판단 시점이나 생태 시간을 바꾸지 않는다 |

모델을 그대로 둔 채 회피 방향을 바꾸면 다음 위치와 관측, 이후 RL 선택은 달라질 수 있다. 따라서 **판단 알고리즘 보존과 전체 궤적 동일성은 구분**한다.

- **A: 결과 보존 최적화** — 데이터 접근/후보 순회/함수 추출/독립 작업 병렬화. 동일 빌드·입력·seed에서 기존 결과와 정확 비교를 통과해야 한다.
- **B: 실행 동작 개선** — 예측 회피, 제한 이동 이웃. RL 입력 계산과 선택 로직은 보존하지만 세계 궤적이 바뀌는 실험 profile이다. 먼저 shadow로 차이를 기록하고, 이동 품질·생태 결과·RL 실행 계약을 확인한 후 opt-in한다.
- **C: 현재 구현 범위 밖** — 정책 동족 관측을 K개 표본으로 대체, 정책 판단/관측 주기 축소, 카메라 기반 Native 생태 step 생략, 재학습·보상 변경. 필요하면 RL 담당과 별도 계약을 만든다.

새 학습을 이 계획의 선행 조건으로 강제하지 않는다. A와 표현 개선은 기존 두뇌로 진행한다. B는 기존 두뇌의 실행 환경 호환성을 평가하며, 재학습 필요 여부는 RL 담당의 판단이다. Python mirror가 없는 ON 세계를 기존 학습 세계와 동등하다고 기록하지 않는다.

## 2. 현재 구현과 Epic 참고 범위 재확인

| 비교 지점 | 실제 Source에서 확인한 상태 | 적용 방향 |
|---|---|---|
| 실행 단위 | 이미 Mass Fragment에 개체별 상태를 보관한다. 통합 Processor가 view를 모아 지역별 `EcoWorldV3::Step` 실행 | ECS 재도입이 아닌, 기존 단계의 읽기/쓰기·중간 결과·계측 경계 명시 |
| 스레드 | 통합 Step/표현 동기화 Processor는 GameThread 요구. hook에 Subsystem·지형·여행·생명주기 접근 포함 | 순수 계산을 먼저 추출하고 소유권과 의존성이 입증된 작업만 worker로 이동 |
| 주변 개체 | `PerceiveKin`이 KinCount/FleeCount/KinAlarm/ToCentroid/Separation을 함께 계산. 보이는 동족과 분리의 시야 조건은 다름 | 정책 지각과 회피 후보를 별도 계약으로 분리. 정책 지각은 정확 결과 유지 |
| 조향 | `Control`과 도주 반사·IntegratedControlHook·약화/날씨 등이 실행 방향/속도에 관여 | 기존 목표 조향 뒤에 독립 회피 출력을 연결하고 최종 Native 적분 유지 |
| 갱신 주기 | Native에는 요청을 읽는 조건이 있고 관측 계산은 별도로 수행. `UpdateSimulationLOD`의 `DecisionIntervalMultiplier` 소비자는 V1 `EcoPolicyProcessor` | 기존 V1 LOD를 v3에 이미 적용된 기능으로 설명하지 않음. v3 판단/감각/이동 주기는 유지, 우선 표현·진단 및 동일 snapshot 재사용 최적화 |
| 표현 | `SyncVisuals`는 거리, 기존 Actor 유지 가중치, 전투 우선순위, Actor/Add 예산, 클래스별 풀 재사용 지원 | 기존 풀 소유자를 유지하며 단계별 표현을 도입. 새 풀/Actor writer를 겹치지 않음 |

Epic의 [MassEntity](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine?application_version=5.8)는 데이터 Fragment와 처리 Query/Processor를 분리한다. [Mass Avoidance](https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine)는 가까운 장애물의 분리·예측 회피를 force로 계산한다. [MassGameplay](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-gameplay-in-unreal-engine?lang=en-US)는 Simulation/Representation/Replication LOD를 구분한다. 이 원리를 적용하며 Epic 샘플의 StateTree 행동 판단이나 ZoneGraph 경로를 생태계에 그대로 이식하지 않는다.

웹 기본 페이지는 조회 시 일부 **5.9**로 열렸다. 프로젝트 적용 API는 설치 **5.8.3** 소스를 기준으로 확인한다. 공식 설명의 High Actor/Low Actor/ISM/None은 선택 가능한 표현 유형이지 반드시 그 순서로 전부 통과해야 하는 전환은 아니다. 설치 엔진 enum에는 `SkinnedMeshInstance`도 있으나 사슴에 적용 가능한 애니메이션·패키징 경로는 이번에 검증하지 않았다. 네 단계는 이번 계획의 기본 후보이며 엔진 전체 기능 목록이 아니다.

### 2.1 구현자가 열어야 할 소스

아래 행 번호는 작성 시점 탐색용이며 변경 후에는 심볼을 기준으로 찾는다. 파일 헤더의 과거 R1b/MVP 설명보다 실제 Source와 최신 활성 계약을 우선한다.

| 파일·심볼 | 확인 위치/의미 |
|---|---|
| [통합 Processor](../../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp) | `IntegratedControlHook` 약471행, GT 요구848행, FamilyRows/BodyBuckets 구성1211행, Native Step1236행, 표현 Processor1430행 이후 |
| [Native 세계](../../../Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h) | `PerceiveKin` 호출2879행, `DecideHerb`2955행, 제어 입력3094행, Step A3930행대/H4659행대 |
| [관측](../../../Source/AdaptiveEcosystem/AI/Policy/EcoObservationV3.h), [공간 인덱스](../../../Source/AdaptiveEcosystem/AI/Policy/EcoSpatialIndexV3.h), [실행기](../../../Source/AdaptiveEcosystem/AI/Policy/EcoRepertoire.h) | 동족 집계의 원본 row 순서, 후보 fallback, 행동별 제어·단위 변환 |
| [v3 Fragment](../../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3Fragments.h), [통합 Fragment](../../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedFragments.h) | 이미 존재하는 Rep/Decision/Body/Perception/Control와 통합 상태 재사용 |
| [표현 선택](../../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreaturePresentation.cpp), [표현 Actor](../../../Source/AdaptiveEcosystem/Creature/Representation/EcoCreatureRepresentationActor.cpp) | `SyncVisuals`, binding·보간·외형·pool lifecycle |
| [기존 LOD](../../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureScheduling.cpp), [V1 소비자](../../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyProcessor.cpp) | `DecisionIntervalMultiplier` 쓰기/읽기. v3 스케줄과 혼동 금지 |
| [사냥 proxy](../../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureCombat.cpp), [Client Bubble](../../../Source/AdaptiveEcosystem/Network/Mass/EcoMassClientBubble.cpp) | 권위 피격/전투 relevance와 표현 선택/수신 데이터의 연결 확인 |
| [정체성·장부 Fragment](../../../Source/AdaptiveEcosystem/Mass/EcoMassFragments.h), [표현 DTO](../../../Source/AdaptiveEcosystem/Core/EcoRepresentationTypes.h) | StableAgentId·Region 및 read-only 표현 경계 |
| [기존 벤치마크](../../../Tools/EcoSpatialBenchmark/EcoSpatialBenchmark.cpp) | Unreal 외 순수 계산 비교. 실제 월드 trace 비용의 대체 근거로 쓰지 않음 |

설치 엔진 참고 파일은 `C:/Program Files/Epic Games/UE_5.8/Engine/` 아래 `Plugins/AI/MassAI/Source/MassNavigation/Private/MassSteeringProcessors.cpp`, `MassAvoidanceProcessors.cpp`, `Plugins/Runtime/MassGameplay/Source/MassMovement/Private/MassMovementProcessors.cpp`, `MassRepresentation/Public/MassRepresentationTypes.h`다. 확인한 회피 후보 상수24/collider6은 버전별 구현값이며 프로젝트 권장 기본값 또는 정확한 최근접6 보장이 아니다.

## 3. 실행 단위 — 단계별 입출력 명세

**목표:** 기존 정책/Native 단계의 의미를 유지하면서 비용과 의존성을 분리한다. Fragment를 무조건 더 만들거나 Processor 수를 늘리는 것을 성과로 삼지 않는다.

중요한 현재 시점 계약: Step A는 **지난 스텝 끝에 준비한 요청**을 Commit하고, Step H에서 다음 요청을 준비한다. 아래 분리로 정책 판단을 현재 이동 직전으로 옮겨 한 스텝의 의미를 바꾸면 안 된다. 첫 초기화/지역 인계의 관측 갱신도 보존한다.

| 논리 단계(제안 이름) | 읽기 | 쓰기·단일 소유자 | 초기 실행 위치 |
|---|---|---|---|
| Gather/Prepare | Mass 상태, 환경·지형·Social/여행 입력 | 해당 경계의 작업 snapshot, ID→row, 입력 revision | GT |
| Commit/Control | 기존 pending request, 세계 규칙, 기존 제어 입력 | 기존 Rep/Decision/Control, 선택 provenance | 우선 GT, Native 내부 순서 유지 |
| LocalAvoidance | 선택된 preferred movement, 이동 이웃/경계 | 별도 회피 결과 scratch; Body/행동/관측 쓰기 금지 | shadow; 이후 순수 작업 후보 |
| NativeExecute | 최종 제어와 현재 세계 상태 | 기존 Body·섭식·대사·포획 결과 | GT 및 기존 단일 Native writer |
| Geometry/NextDecision | 기존 시점의 이동 후 세계, 모델 | 기존 Perception·다음 Decision | 순수 지각을 우선 추출; 정책은 기존 함수 호출 유지 |
| Publish/Commit | 완료 결과·실제 위치 | 기존 summary/feedback/장부/표현 전달 | 기존 GT 완료 경계 |

이 표는 실제 Step의 중간 세부 단계(늑대, 자원, 출생/사망 등)를 생략한 인터페이스 지도다. 표 순서로 Native 전체를 재작성하지 않는다. 함수 추출 전 현재 A~H와 hook의 호출·관측 시점을 기록하고 그대로 옮긴다.

구현 순서:

1. 기존 scope를 재사용·보강해 Gather/Build/Perception/Policy/Control/Trace/NativeExecute/Commit 시간을 분리한다. `EcoSpatialV3::FDiagnostics`도 확장 가능 여부를 먼저 본다.
2. 지역/개체 상태는 기존 Fragment에 남기고 순수 입력/출력 helper를 추출한다. 참조 `FReferenceWorld`와 통합 경로가 같은 함수를 사용하도록 한다. native loop와 Mass loop에 같은 알고리즘을 두 벌 만들지 않는다.
3. 초기에는 하나의 Processor 안에서 같은 순서로 호출한다. 동등성 통과 후 Mass 스케줄에 노출할 필요가 있는 단계만 별도 Processor로 옮긴다.
4. Query에 실제 ReadOnly/ReadWrite, tag 제외, Subsystem 의존성을 선언한다. snapshot을 통한 간접 이웃 접근도 의존성 표에 포함한다. 소비자마다 읽기/쓰기 owner를 하나로 정한다.
5. 고정된 phase 사이의 scratch는 컨텍스트 소유 버퍼로 둔다. 여러 Processor가 지속적으로 읽을 개체 결과만 Entity Fragment 후보로 삼는다. 개체별 정책 출력은 Shared Fragment에 넣지 않는다. LOD 변경 때마다 정책 archetype을 교체하지 않는다.

**완료 기준:** 단계별 trace와 읽기/쓰기 표, 기존 결과 차이0, 개체별 최종 적분1회, 새 단계 때문에 늘어난 copy/alloc/dispatch 비용 공개. 구조 분리만으로 성능이 좋아졌다고 보고하지 않는다. [03 상세](03_REGION_EXECUTION.md)를 병행한다.

## 4. 스레드 — GT snapshot / 순수 작업 / GT 반영

**목표:** 현재 GT flag를 제거하는 방식이 아니라, GT 권위를 유지하면서 독립적인 계산의 critical path를 줄인다.

| 분류 | 후보/처리 |
|---|---|
| 먼저 worker 후보 | 고정 snapshot의 정확 후보 순회·순수 지각·회피 산술. 기존 로컬/공유 scratch 사용 여부를 먼저 감사 |
| GT 유지 | UObject/World/Subsystem 접근, 실제 지면 trace, Social 예약 변경, 자원/포획 경쟁·생명주기·지역 이전, Mass 구조 변경, 최종 commit |
| 별도 입증 필요 | 정책 병렬 호출, 지역 전체 Step 병렬 실행, 지역 간 FamilyRows/BodyBuckets 공유. 읽기 전용 Net 외 brain 전환·통계·RNG·앞 지역 갱신을 뒤 지역이 보는 순서도 검사 |

작업 token은 `WorldEpoch + RegionId/ContextGeneration + StepSequence + Phase + InputRevision + BrainRevision`으로 구성한다(이름은 제안). snapshot은 값으로 소유하며 worker가 Mass chunk 포인터·Actor·Subsystem·stack adapter를 캡처하지 않는다. 개체 결과는 StableAgentId와 동일 generation의 row를 함께 검증한다.

1. 동일 스레드에서 snapshot→순수 함수→반영을 실행하여 원본과 비교한다. 위치/행동 갱신 경계가 다른 캐시는 재사용하지 않는다.
2. 작업별 출력 영역을 분리하고 barrier 뒤에만 읽는다. `FGrid::Query`는 호출자가 제공하는 `Out` 벡터에 후보를 쓰므로 해당 벡터와 다른 mutable scratch를 worker별로 분리한다. read-only Grid라는 이름만으로 호출 경로 전체의 동시 Query 안전성을 가정하지 않는다.
3. dispatch·barrier는 매 기존 논리 경계 안에서 끝낸다. 최초 버전은 다음 frame으로 결과를 넘기지 않는다. task 완료 순서로 RNG/장부 순서를 바꾸지 않는다.
4. 기존 context 및 원본 row 순서로 결과를 반영한다. 새 StableAgentId 정렬을 도입할 때도 기존 순서와 결과 동일성을 입증한다. 정확 경로에서 float reduction을 재배치하지 않는다.
5. 취소/오래된 token 결과는 반영하지 않는다. 작업 종료가 확인된 후 기존 동기 경로로 계산한다. 입력이 그대로면 같은 snapshot을 사용하고, revision이 바뀌었으면 현재 유효한 경계의 입력을 다시 수집한다. worker와 fallback이 같은 출력에 동시에 쓰거나 이미 소비한 RNG/receipt를 재실행하지 않게 한다. 종료 중인 World에는 fallback을 실행하지 않는다.
6. 저장·restore·brain 교체·birth/death/reorder·이주·월드 종료는 작업 drain/취소 및 generation 갱신 경계를 거친다. 파생 캐시를 저장 데이터의 진실값으로 추가하지 않는다.

life의 FamilyRows/BodyBuckets는 현재 지역 루프마다 전체 Deer를 읽는다. ID mapping처럼 구조가 같을 때 재사용 가능한 것부터 줄인다. 위치 bucket을 전지역에서 한 번 freeze하면 앞 지역 이동을 뒤 지역이 읽는 현재 의미가 달라질 수 있으므로 먼저 읽기 시점을 대조한다.

**완료 기준:** worker1/2/4·완료 순서 변화에도 A의 비교 차이0, stale commit/중복 receipt0, GT/worker/barrier/copy 시간을 포함한 전체 Step 측정. 지역 병렬화를 못 해도 순수 지각 분리만 독립 채택할 수 있다.

## 5. 주변 개체 — 정확 정책 지각과 이동 회피 질의 분리

**목표:** Epic의 근거리 장애물 질의 원리를 이동에 적용하면서 정책이 보던 동족 집합은 유지한다. 현재 `FKinInput`에 HerdId가 없으므로 기존 동족 관측을 같은 Persistent Herd로 제한하는 것도 의미 변경이다.

| 질의 계약 | 입력/출력 | 변경 허용 |
|---|---|---|
| PolicyKinExact | 기존 Self/Pos/Head/see·sep 반경/행동 snapshot → 기존 KinCount/FleeCount/KinAlarm/ToCentroid/Separation | 정확 후보 수집·정렬·재사용만 변경. 원본 row 누산/시야/분모 보존 |
| MovementNeighbors | 같은 경계의 위치·실행 속도·몸 반경·생존/층 정보 → 회피용 ID/row 목록·거리·예산 진단 | 제한 후보 사용 가능. 기존 정책 집계에 반환하지 않음 |
| Threat/Predation | 현재 감지·포획 입력과 판정 | 위 두 질의 최적화로 반경/후보/판정 주기를 줄이지 않음 |

초기에는 PolicyKinExact의 네 결과를 그대로 계산한다. 이동 결과를 추가하면서 기존 Separation 계산까지 남아 있으면 전체 비용이 증가할 수 있다. 제거/생략은 소비자 감사를 통해 해당 profile에서 사용하지 않음을 입증한 산술에만 적용한다.

구현 단계:

1. 기존 [P1](../BOUNDED_NEIGHBOR_SEARCH_SPEC_20261110.md)의 FullScan/Grid25/수집+sort/k-way merge/bitset 비교. 후보0과 fallback을 구분하고 기존 동점/누산 순서를 유지한다. 밀집 질의의 후보25% fallback을 무조건 제거하지 않는다.
2. 같은 snapshot·셀 범위 후보를 bounded cache로 재사용한다. 메모리/entry 상한과 eviction을 두고 N개의 N길이 목록 누적을 방지한다.
3. 이동 질의에는 방문 셀 C, 검사 항목 B, 반환 K를 각각 제한한다. **최초 실험은 기존 01의 B={24,48,96}, K={6,12,24}**를 사용하고 C는 셀 분포 기준선으로 고정한다. 이 값은 공식 권장값·production 기본값이 아니다. 동족 P3의 관측 근사 수치와 섞지 않는다.
4. self/dead/다른 층 등 제외 항목도 검사 B에 센다. 셀/동점 순서를 결정적으로 정하고 selection에 세계 RNG를 사용하지 않는다. 빌드 때 전체 정렬하는 비용도 포함한다.
5. saturation 결과를 명시한다. 정상 밀집 질의를 무조건 전체 스캔으로 되돌려 budget을 무력화하지 않는다. 비정상 token은 신규 회피를 적용하지 않고 정해진 기존 경로를 사용하며 그 비용을 기록한다.

근사 질의는 정확한 최근접 K 또는 충돌 방지를 보장하지 않는다. 정책 관측이 여전히 전체 동족을 필요로 하면 전체 시스템이 O(N)으로 바뀌었다고 주장할 수 없다. 이웃의 검색 범위에 현재 지원하지 않는 cross-region 인지/겹친 동굴층을 추가하지 않는다.

**완료 기준:** A 관측/로짓/요청·제어 비교 차이0, B/C/K 초과0, 동일 위치1,024개 및 birth/death/reorder/migration/restore의 stale row0, query/build/cache 메모리와 전체 Step 결과 보고. [01 상세](01_NEIGHBOR_AVOIDANCE.md)를 따른다.

## 6. 조향 — RL이 정한 실행 의도와 예측 회피 분리

**목표:** 행동 선택을 바꾸지 않고, 이미 이동하기로 한 개체의 국소 진행을 개선한다. Epic 회피의 force를 그대로 Native의 변위에 더하지 않는다.

제안 인터페이스는 아래와 같다. 이름은 아직 존재하는 API가 아니다.

| 데이터 | 필수 필드·제약 |
|---|---|
| PreferredMotion | token, StableAgentId, 선택된 실행 행동/출처, 기존 목표·방향, 희망 속도, 정지 여부, 허용 속도/방향 제약. 행동을 재선택하는 필드 없음 |
| AvoidanceResult | token, 보정 방향/속도, saturation·거절 사유, 예측 최저 간격. Body/HP/Energy/Raw Request를 직접 쓰지 않음 |
| ActualOutcome | 기존 Native의 실제 위치/변위·blocked·도착 결과. 기존 feedback 경로로만 전달 |

단위는 Native `1unit=200cm`, 제어 Velocity는 **Native 단위/step**, UE 속도는 cm/s다. 변환은 기존 `UnitsPerStepToCmPerSec`/역함수를 사용한다. 예상 충돌 시간은 초 또는 step 중 하나로 고정하고 타입/필드에 단위를 명시한다.

구현 순서:

1. `Control → Flight 보정 → IntegratedControlHook → 약화/날씨 → 실제 이동`의 전체 writer를 기록한다. 함수 분리만 수행한 OFF 버전은 기존 수식/순서를 그대로 둔다.
2. shadow에서 preferred velocity와 가까운 이동체의 상대 위치/속도·몸 반경으로 미래 근접을 계산한다. 정면 교차/벽 접근/이동-정지 조합의 보정량과 현재 결과 차이를 표시한다. 이 단계에서는 기존 결과만 실행한다.
3. opt-in profile에서는 **기존 goal/여행·Social·life 제어가 만들어진 뒤, 최종 속도 제한·지형 검증 전에** 회피 단계를 넣는 안을 검증한다. 선행 hook 내부에서 이미 적용한 체형 배수 등은 다시 곱하지 않는다.
4. 기존 Separation 유지+예측 회피 추가, 또는 기존 Separation을 이동 분리로 대체하는 안을 서로 다른 profile로 비교한다. 후자의 경우 기존 분리를 한 번만 비활성화하며 이중 가산하지 않는다. B profile로 명시한다.
5. FREEZE/SLEEP/도착 정지·웅크림은 위치 보정을 강제하지 않는다. 정지 개체는 다른 개체의 장애물 후보가 될 수 있다. 회피가 새 먹이/은신처/지역 목표나 행동을 선택하지 않는다. 지면·보호영역·슬롯 안전 방향은 기존 검증을 통과해야 한다.
6. 충족하지 못한 결과는 해당 회피만 거절하고 기존 제어로 돌아간다. 최종 Native 이동 검증은 계속 수행한다. 실제 도착만 slot/여행 feedback으로 전달한다.

엔진 `UMassMovingAvoidanceProcessor` 직접 재사용은 adapter 검증 항목이다. 필요한 Force/MoveTarget/Velocity/Radius/NavigationEdges와 장애물 grid를 누가 채우는지, 어느 좌표/시점인지, 엔진 적분을 어떻게 배제하는지 먼저 확정한다. 표면 tag/Fragment만 추가해 자동 연결하지 않는다. 최초 구현은 엔진 비의존 순수 회피 helper로 기존 backend를 보존하고, 엔진 직접 재사용은 같은 fixture의 비용·유지보수 비교 후 결정한다. `FMassCustomMovementTag` 제외 계약을 유지하고 Native와 `UMassApplyMovementProcessor`를 동시에 실행하지 않는다.

**완료 기준:** 정면/교차/좁은 통로/정지 군집/위협 도주/은신처 실패/이주 도착 fixture. 보정 원인 trace, 단일 적분, 목표·정지 제약 위반0. 품질 및 생태 gate는 §9. 회피 품질 개선과 CPU 절감은 각각 측정한다. [02 상세](02_STEERING_INTEGRATION.md)를 따른다.

## 7. 갱신 주기 — 작업별 빈도와 즉시 갱신 조건

**목표:** Epic Simulation LOD의 작업 분류·빈도 관리 원리를 적용하되, 이번 범위에서는 RL 판단/관측과 생태 step 주기를 바꾸지 않는다.

| 작업 | 이번 구현 규칙 | 무효화/긴급 처리 |
|---|---|---|
| RL Prepare/판단/Commit | 기존 조건·시점 유지 | 기존 사건·초기화·인계 경로 유지 |
| 정책 동족/위협/먹이/은신처 지각 | 기존 fresh snapshot·주기 유지 | 위치/heading/행동/환경 등 실제 입력이 바뀌면 재계산 |
| Native 이동·대사·섭식·포획 | 기존 고정 step 유지 | 카메라/표현 LOD로 건너뛰지 않음 |
| 이동 회피 | 최초 B profile은 매 Native step 계산 | 새 위협·목표/slot 변경·context 이전은 캐시 무효화 |
| ID mapping/공간 broad phase | 동일 구조/동일 위치 snapshot 내 재사용 | 구조 generation·위치 revision이 달라지면 폐기 |
| 표현 LOD 선정 | 제안 5/10/20Hz 비교, 실제 화면 보간과 분리 | relevance/죽음/teleport/전투 우선순위 변화는 즉시 dirty 처리 |
| 화면 보간/애니메이션/IK | 표현 등급별 예산, 받은 위치만 보간 | 새 binding·이주 discontinuity 시 과거 보간 초기화 |
| 진단/HUD 집계 | 저빈도 snapshot 재사용 | 원시 이벤트는 유실 없이 누적하고 화면만 느리게 갱신 |

위 표현 주기 수치는 실험 후보다. 이동을 부드럽게 보이는 interpolation tick까지 함께 낮추지 않는다. viewer가 없는 Dedicated Server에서도 생태계는 계속 실행한다.

Mass Signals를 사용한다면 payload 없는 wake-up 알림으로만 쓴다. 실제 Alarm 증거/수신 시각·TTL·Herd·lease는 기존 상태가 소유한다. signal을 새로운 RL 사건으로 주입하지 않는다. 동일 알림은 병합하며 처리 deadline/age를 계측한다. birth/death/목표 변경을 단순 거리 LOD queue 뒤에서 굶기지 않는다.

느린 응집(1/2/4 decision step)이나 감각 캐싱의 입력 동일성이 입증되지 않으면 C로 남긴다. 기존 문서의 후속 제안이 이를 자동 승인하지 않는다. 정책 주기를 그대로 유지한 상태에서 얻은 성능과 아직 줄이지 못한 지각 비용을 분리 보고한다.

**완료 기준:** 동일 입력에서 권위 궤적 동일, 카메라 이동/숨김으로 판단 시점 변동0, dirty 사건 누락0, 표현 갱신 지연 p95/최대 기록. [04 상세](04_LOD_AND_SIGNALS.md)를 따른다.

## 8. 표현 — High Actor / Low Actor / ISM / None

**목표:** 멀리 있는 사슴도 저비용으로 표시하고, 가까운 개체에 animation/IK 비용을 집중한다. 현재 거리·수 제한·풀링을 유지·확장한다.

| 후보 등급 | 표현 내용 | 요구 사항 |
|---|---|---|
| High Actor | 기존 Skeletal Actor, 필요한 animation/IK·외형·상태 UI | 전투/선택 우선, 기존 기능 동일 |
| Low Actor | 저비용 mesh/animation 설정, IK·보조 효과 예산 축소 | 동일 StableAgentId/체형·성숙·털색·행동 유지. 별도 class 필요 여부부터 비교 |
| ISM | 애니메이션 가능한 원거리 mesh 후보, per-instance 외형/활동 데이터 | locomotion·정지·섭식·죽음과 체형 변화의 아트/셰이더 지원 검증. 정적 mesh로 뛰는 사슴을 대체하고 완료 선언하지 않음 |
| None | 해당 viewer에게 렌더하지 않음 | 논리 생존·자원·감각·권위 사냥 proxy와 무관. 원거리 생태계 정지 금지 |

설정 명세(제안): `High/Low/ISM 거리`, `각 등급 cap`, `승격/강등 hysteresis`, `최소 유지 시간`, `frame당 전환/Add cap`, `pool cap`, `LOD 선정 주기`, `전투/선택 우선순위`. 처음에는 기존 거리·Actor 총예산을 기준선으로 두고 cap을 늘려 생긴 성능 저하를 LOD 성과와 혼합하지 않는다. 생산 거리/개수는 하드웨어 측정 후 profile에 고정한다.

구현 단계:

1. 기존 `SyncVisuals`를 선정과 표현 소비로 나눈다. 수신 Client sample·MovementDiscontinuityRevision·보간을 그대로 재사용한다. dedicated는 기존처럼 visual Actor를 생성하지 않는다.
2. High/Low Actor 전환부터 비교한다. 기존 `VisualPool`과 클래스별 반환 정책을 유지하고 active+pool 총예산을 준수한다. 반복 경계 이동은 hysteresis와 최소 유지 시간으로 제한한다.
3. owned 테스트 에셋으로 ISM/VAT 경로를 검증한다. 준비되지 않은 종/활동은 Low Actor 또는 명시된 fallback을 쓴다. instance index를 영속 ID로 쓰지 않는다. remove-swap·재배치 시 ID↔instance mapping을 갱신한다.
4. 한 viewer의 한 StableAgentId에 활성 표현은 하나. 새 표현 준비가 끝난 경계에서 기존 표현을 반환한다. spawn 실패 때 중복 binding/보이지 않는 영구 대기를 남기지 않는다. 성장/죽음/이주 및 늦은 네트워크 sample도 같은 규칙으로 처리한다.
5. `SetPresented`가 현재 Actor 목록에서 생성되는 점을 확장 감사한다. Low/ISM 개체의 전투 relevance 및 조준/선택을 누락하지 않는다. 권위 `HitProxy`는 별도 유지하며 render instance collision으로 대체하지 않는다. 필요한 경우 전투 대상의 High 승격을 기존 예산 안에서 요청한다.
6. Epic `UMassRepresentationSubsystem` 직접 사용 여부는 단일 소유자 기준으로 선택한다. 선택한 cohort는 기존 Actor 생성에서 제외하고 엔진 representation만 생성하게 한다. 기존 manager와 엔진 spawner가 같은 개체를 이중 소유하지 않는다. 초기에는 기존 소유자 확장으로 시작하고 엔진 adapter는 별도 단계로 비교한다.

Representation LOD, authority Simulation, client별 Replication LOD는 각각 독립이다. 더 먼 ISM은 Client가 위치 데이터를 수신하고 있을 때만 의미가 있다. 현재 Bubble 범위를 넘기려면 기존 relevance/대역폭 계약을 별도로 검증하고, 새 로컬 생성으로 모르는 개체를 만들어 채우지 않는다. 여러 local viewer가 있으면 필요한 가장 높은 표현 등급과 전체 cap을 조합한다.

**완료 기준:** 같은 authority replay에서 표현 변경에 따른 논리 차이0. Client2의 다른 시점·late join·relevance 재진입·풀 재바인딩·성장/죽음/이주 중 전환·사냥/선택 검증. 중복 표현0, 잘못된 ID/털색 잔류0, active+pool cap 초과0. CPU animation/IK, GT 선정/spawn, GPU/draw calls, 메모리·network bytes를 각각 비교한다. ISM 에셋 변경 시 cook/package/streaming 확인도 포함한다.

## 9. 단계별 개발 순서와 채택 기준

### 9.1 작은 변경 단위로 진행

| 순서 | 산출물 | 진입/종료 기준 |
|---|---|---|
| M0 계측·소비자 감사 | 단계별 trace, writer/시간/RNG/질의 소비자 표, 재현 manifest | 실제 branch/diff·backend·맵 설정 기록. 현재 동작 baseline 고정 |
| M1 정확 탐색·중복 제거 | 기존 P1 후보 순회 비교, bounded cache, 안전한 ID mapping 재사용 | A 정확성 통과 + 비용 측정. 기존 P0/P1 우선순위 유지 |
| M2 실행 단계 분리 | 동일 순서의 pure helper와 입력/출력 계약 | same-thread 결과 차이0. 구조 변경만으로 전면 Processor 교체하지 않음 |
| M3 표현·주기 개선 | High/Low 및 LOD 선정 주기, 이어서 준비된 ISM | 권위 결과 동일·전투/네트워크/전환 검사 통과. M1 이후 독립 진행 가능 |
| M4 worker 분리 | 순수 지각 또는 회피 작업, barrier/token/취소 | M2 동등성 이후. worker 수 변화 결정성·critical path 이득 확인 |
| M5 회피 실험 | 제한 이웃 shadow → 예측 회피 shadow → owned opt-in | A와 B 결과 구분. RL 선택 로직 보존·품질/생태 계약 통과 후 채택 |

처음부터 여섯 변경을 함께 켜지 않는다. M4/M5의 실행 순서는 M0 병목 결과에 따라 바꿀 수 있다. 성능 근거가 없으면 M1/M3만 채택하고 다른 단계는 보류할 수 있다. 각 항목별 issue/PR에는 입력·출력·대상 파일·실험 flag·검증/rollback을 함께 적는다.

### 9.2 재현·성능·품질 gate

공통 fixture는 기존 [05 검증](05_VALIDATION.md)을 사용한다. 최소 지역당128/1,024, 지역1/4/8, 균일/한곳 밀집/16×64·4×256 무리/통로 교차/이주를 비교한다. 같은 하드웨어·바이너리·brain hash·설정·seed·실제 alive 수와 스텝 수를 기록하고, 3seed 이상·30초 warmup+180초 측정·실행 순서 교차를 시작 기준으로 삼는다. 출생/죽음으로 부하가 달라지는 자연 실행과 개체 수를 고정한 비용 fixture를 분리한다.

| gate | 기준(목표이며 아직 실측하지 않음) |
|---|---|
| A 정확성 | 기존 Golden/공간 비교 및 신규 밀집 fixture의 관측·로짓·mask·요청·행동·주요 상태·RNG·장부 차이0. struct padding 전체 memcmp 대신 정의된 필드/정규화된 snapshot 비교 |
| 공간 성능 | 기존 명세대로 clustered1,024 동족 처리 p95 20% 이상 감소, 전체 Native Step p95 회귀5% 이내. 실패하면 전략 재선택 |
| worker 채택 | 같은 부하 전체 Native 처리 critical path p95 10% 이상 감소를 초기 제안 gate로 사용. dispatch/copy/barrier 포함, worker CPU 합계·메모리도 보고 |
| 표현 채택 | 같은 화면 대상 수/품질 fixture에서 표현 CPU p95 또는 GPU p95 20% 이상 감소를 초기 제안 gate로 사용. 다른 쪽과 전체 frame p95 회귀5% 이내; 기존 Actor 제한 baseline과 동일 표시 수 baseline 모두 보고 |
| B 선택 로직 | 동일 고정 관측·내부 판단 상태를 재생했을 때 mask/로짓/request 동일. 자유 실행의 미래 관측/행동 동일은 요구하지 않으며 차이를 보고 |
| B 이동 품질 | 기존 05대로 겹침 pair-step 비율 증가≤1%p, 완료 가능한 경로 도착률 하락≤1%p, 경로시간 p95 증가≤10%. 별도로 개선 대상 밀집 fixture의 겹침 또는 정체 감소를 확인 |
| 권위·생명주기 | Client Native Step0, 단일 writer, stale 결과·중복 receipt0, 기존 자원/인구 수지 유지, 실제 위치 feedback |
| B 생태 결과 | 생존·포획·섭식·이주·체류시간 분포 비교. 허용 편차를 RL/생태 담당과 사전 수치화하기 전 기본 활성화하지 않음 |

worker/표현 수치는 이 문서의 공학적 제안이며 Epic의 성능 보장도 프로젝트의 확정 frame budget도 아니다. 실제 목표 플랫폼의 server tick/client frame budget을 M0 결과에 기록하고 채택 전에 확정한다. shadow는 원본+신규 계산으로 더 느릴 수 있으므로 최종 실행 profile 성능과 분리한다. native 벤치마크 개선을 곧바로 FPS 향상으로 환산하지 않는다.

### 9.3 구현 시 필수 검증·되돌리기

- C++/빌드 입력 변경 시 루트 AGENTS대로 실행 중 UBT/dotnet/Editor/LiveCoding/MSBuild/ShaderCompileWorker를 확인한다. 실행 중인 것을 중단하거나 중복 빌드하지 않는다. 필요한 쓰기 권한의 실행 환경에서 직접 `UnrealBuildTool.exe`로 `AdaptiveEcosystemEditor Win64 Development`를 완료까지 기다린다.
- 관련 Policy/Route/Flight/Spatial/Integrated/Life/Migration/Representation 자동화 이름을 실제 Source에서 찾아 실행한다. 기존 Golden의 기대값을 새 결과로 바꿔 A 비교를 통과시키지 않는다.
- owned fixture의 Standalone·Dedicated+Client2·late join·저장/복원·월드 종료를 확인한다. 원본 팀 레벨 authoring은 하지 않는다. 에셋/엔진 의존 변경은 Game/cook/package·streaming을 추가 검증한다.
- 정확 탐색/worker/회피/표현 옵션은 독립적으로 끌 수 있게 설계한다. 이름은 구현 시 확정하며 이 문서의 제안 이름을 실행 가능한 CVar로 안내하지 않는다. 기본은 기존 동작, 실험은 owned profile에서만 opt-in한다.
- rollback은 완료 경계에서 작업 drain→generation 갱신→기존 경로 복귀 또는 세션 재시작. 실행 의미를 바꾸는 profile/계수는 checkpoint 호환성에 포함한다. 현재 통합 저장 포맷 v5를 임의로 낮추거나 worker scratch/row를 영속화하지 않는다.
- 다음 보고는 구현/측정/미완료를 분리하고 실제 설정·manifest·trace·비교 결과를 남긴다. 본 문서 작성은 위 실행 검증을 새로 수행했다는 뜻이 아니다.

## 10. 다음 구현 요청에 사용할 문구

> PROJECT_CONTEXT, MassMovement/README 및 RL_PRESERVING_EPIC_MASS_PLAN_20261112를 읽고 실제 HEAD/diff를 확인한다. 첫 범위는 M0와 M1이다. 현재 지각/조향 소비자·GT hook·스텝 A/H 시점을 감사하고 기존 FullScan/Grid25 기준선과 정확 후보 순회 개선을 비교한다. R7e/V1/R1b 계약·RL 판단/관측 주기·Native 단일 writer·M4·기존 맵 설정을 보존한다. 새 제한 이웃/예측 회피는 아직 실행하지 않는다. 결과와 다음 단계 진입 조건을 별도 실행 기록으로 남긴다.

## 11. 공식 참고자료와 적용 경계

2026-11-12 본문 확인. 웹 문서와 설치 버전 차이는 §2에 기록했다. 아래 공식 설명을 근거로 한 **프로젝트 적용 설계·수치·검증 조건은 이 문서의 제안**이다.

| 공식 자료 | 실제 참고 내용 | 우리 적용 |
|---|---|---|
| [MassEntity Overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine?application_version=5.8) | Fragment·Query·Processor·deferred 구성 변경 | §3 단계 계약, §4 소유권. Processor 추가만으로 병렬 성능 보장 없음 |
| [Mass Avoidance Overview](https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine) | 이동/정지 회피, 공간 grid, force 출력, 예측 근접, navigation edges | §5/6. 정책 동족 지각의 대체 기능이 아니며 Experimental 표기와 설치 API 확인 필요 |
| [MassGameplay Overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-gameplay-in-unreal-engine?lang=en-US) | 표현 유형·pooling, 세 LOD 구분, 가변 갱신, payload 없는 signal | §7/8. RL 행동 선택을 StateTree로 교체할 필요 없음. ISM animation 지원 범위는 실제 에셋 검증 |
| [Query Executor](https://dev.epicgames.com/documentation/en-us/unreal-engine/simplified-mass-processor) | 선택적인 선언형 접근 API | 읽기/쓰기 명시 참고. 기존 Query API 유지 가능하며 전환 자체는 최적화 아님 |
| [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine) | trace 기반 비용 분석 | §9 전체 비용·critical path 평가. 실제 옵션은 설치5.8에서 확인 |

관련 활성 계약: [공통 맥락](../../PROJECT_CONTEXT.md), [V1](../../RL_Policy/POLICY_CONTRACT_V1.md), [v3](../../RL_Policy/POLICY_CONTRACT_V3.md), [R7e 기본 두뇌·live 계약](../../RL_Policy/LIVE_ADAPT_SPEC.md), [RL 담당 경계](../../RL_Policy/ECOSYSTEM_RL_AGENT_GUIDE_20261024.md), [Mass 실행 순서와 이력](../../Mass/MASS_PROCESSOR_ORDER.md). 과거 기본 R1b·미연결 설명은 현재 Source/최신 계약과 구분한다.
