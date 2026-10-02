# M3 — 권위 생태 시뮬레이션: 구현 현황과 검증

> 기준: 2026-10-02 작업 트리의 UE 5.8 C++와 `Config/DefaultGame.ini`. 이 문서는 M3.1~M3.3의 계획·구현·에디터 절차·검증 기록을 통합한다. 아래의 **구현**, **기존 실행 증거**, **미검증 기대값**은 서로 다른 상태다. `Presentation/` 자료는 변경하지 않았다.

## 1. 목표와 현재 판정

M3의 최종 목표는 Server/Standalone World에서 지역의 Food와 개체의 생존·이동·개체군이 서로 영향을 주는 폐루프다. 현재 연결된 경로는 **지역별 Mass 초기 생성 → 낮밤 추가 생성 → 정기 먹이 소비·Food 손실 → Food 고갈 → 인접 지역으로 같은 Entity 이주 → Population 재집계와 읽기 전용 요약 복제**다. 이 경로는 M3.1~M3.3의 관찰용 시나리오이며, M3 전체 완료를 뜻하지 않는다.

| 구분 | 현재 상태 | 근거와 남은 확인 |
| --- | --- | --- |
| M3.1 지역·시계·생성 | 코드 구현 | 기존 UE 5.8 Editor 빌드 성공 기록. 현재 맵 설정으로 초기 생성, 반복 웨이브, Client Box 표시 재확인 필요 |
| M3.2 소비·이벤트·자원 | 코드 구현 | 기존 빌드 기록과 수동 Food 고갈 로그. 소비·자동 이벤트의 수치와 동시 시각 재현 필요 |
| M3.3 이주·집계·요약 | 코드 구현 | 기존 Standalone 로그에서 A의 ID 1~8이 B에 도착하고 총 Population 16 유지. 당시 `Waves=0`이었으므로 현재 독립 스위치 구성에서 웨이브와 이주 동시 실행, Host/Client, 장시간 실행은 미확인 |
| M3 최종 폐루프 | 미완료 | 섭식 결과의 Energy/HP 반영, 기아·사망, 자원 재생, 위협/정책과의 피드백이 이 관찰 경로에 연결되지 않음 |

기존 Standalone 로그와 빌드는 과거 실행의 증거다. 이 문서의 아래 시나리오는 **재현 절차와 기대 결과**이며, 이번 문서 정리만으로 PIE 실행이 완료된 것은 아니다.

## 2. 소유권과 처리 순서

| 계층 | 현재 책임 | 대표 코드 |
| --- | --- | --- |
| World | `AEcologyRegion`의 RegionId, Bounds, 도착점, 명시적 인접 관계 및 초기 Food 편집값. `UEcoWorldClockSubsystem`의 서버 낮밤 시계 | [`EcologyRegion.*`](../../../Source/AdaptiveEcosystem/World/EcologyRegion.h), [`EcoWorldClockSubsystem.*`](../../../Source/AdaptiveEcosystem/World/EcoWorldClockSubsystem.h), [`EcologyWorldSubsystem.*`](../../../Source/AdaptiveEcosystem/World/EcologyWorldSubsystem.h) |
| Ecology | 권위 `FRegionEcologyState`의 Food/Capacity·PredationHistory·Population, 생성 예약과 상한, 자원 이벤트·소비 배분·장부 | [`EcologySimulationSubsystem.*`](../../../Source/AdaptiveEcosystem/Ecology/EcologySimulationSubsystem.h), [`EcologyResourceSimulation.cpp`](../../../Source/AdaptiveEcosystem/Ecology/EcologyResourceSimulation.cpp), [`EcoSpawnSchedule.cpp`](../../../Source/AdaptiveEcosystem/Ecology/EcoSpawnSchedule.cpp) |
| Mass | `StableAgentId`를 가진 논리 개체의 생성·Feeding·Travel·Region 변경. Alive Entity 쿼리로 Population 계산 | [`EcoMassNetworkBootstrap.*`](../../../Source/AdaptiveEcosystem/Mass/EcoMassNetworkBootstrap.h), [`EcoMassFeeding.cpp`](../../../Source/AdaptiveEcosystem/Mass/EcoMassFeeding.cpp), [`EcoMassMigration.cpp`](../../../Source/AdaptiveEcosystem/Mass/EcoMassMigration.cpp) |
| 조정 | 권위 World에서 초기 검증, 예약 시각 처리, 단계별 Mass/Ecology 호출, 집계와 완료 요약 발행 | [`EcoMassLifecycleSubsystem.cpp`](../../../Source/AdaptiveEcosystem/Mass/EcoMassLifecycleSubsystem.cpp) |
| Network·표시 | `AEcoGameState`가 동일 Step/Revision의 시간·지역 요약을 복제. 기존 Mass Bubble이 관련 Box의 Transform/Region을 전달. Debug는 읽기 전용 | [`EcoGameState.h`](../../../Source/AdaptiveEcosystem/Network/EcoGameState.h), [`EcologyNetworkTypes.h`](../../../Source/AdaptiveEcosystem/Network/EcologyNetworkTypes.h), [`EcoMigrationDebugSubsystem.cpp`](../../../Source/AdaptiveEcosystem/Debug/EcoMigrationDebugSubsystem.cpp) |

서버의 같은 처리 시각에는 **환경 Food 손실 → 추가 생성 → 소비 요청·비례 배분 → 자원 완료 snapshot → 이주 판단·도착 확정 → Alive Population 재집계 → 필요 시 완료 요약 발행** 순서를 따른다. 시각은 최대 0.25초 폭으로 나누고 프레임당 밀린 단계는 최대 8회 처리한다. 생태 상태와 Mass 논리 상태는 Client가 수정하지 않는다. Subsystem 자체는 복제 전송 주체가 아니다.

### M3.1: 지역·시계·생성

- 현재 관찰 경로는 설정된 최소 지역 수 이상, **지역당 Bootstrap 하나**를 요구한다. RegionId 중복, 자기 자신/미등록 지역 인접 ID, Bounds 밖 도착점·스폰 슬롯, 필수 Fragment 누락 또는 이동 Trait 충돌은 시작을 중단한다. 초기 상태 검증 뒤 Ecology 등록·Mass 초기 생성·집계가 끝나야 서버 시계가 시작된다.
- 각 Bootstrap의 `InitialAgentCount`로 처음 생성하고, `SpawnSchedule`의 낮밤 간격·수량으로 정규 웨이브를 예약한다. Phase 끝 시각은 포함하지 않아 전환 시각에 이중 생성하지 않는다. `Food=0`은 **추가 웨이브**만 막는다. 지역·전체 상한과 이미 예약된 수를 적용한다.
- 새 Entity는 신규 `StableAgentId`, Species/Region ID와 runtime index, Transform, `Resident` Travel 상태, 생성 시각과 첫 소비 예정 시각을 받는다. 지역별 Species/Region ID는 영속 식별, runtime index는 해당 World의 Mass hot path 식별이다.

### M3.2: 소비·Food 이벤트

- Authority·Alive·`Resident` 개체만 예정 시각에 Food를 요청한다. `Traveling`·`WaitingForFood`는 소비하지 않는다. Mass가 개체별 요청을 수집하고 Ecology가 지역별로 부족분을 비례 배분한다. Food 5에서 10개체가 2씩 요청하면 합계 5, 각 0.5를 지급한다.
- 지급 결과는 Feeding Fragment의 `LastGrantedAmount`와 `TotalGrantedAmount`에 기록한다. **현재 이 지급량은 Energy/HP를 회복시키지 않는다.** 0 지급이어도 다음 예약 시각으로 진행하며, 중복·오래된 요청은 거절한다.
- 낮·밤 자동 Food 이벤트는 설정한 Phase fraction에서 지역 Food를 최대 `FoodLoss`만큼 한 번 줄인다. `Debug.Starvation.<RegionId>` 또는 `Debug.Starvation <RegionId>`는 권위 World에서 해당 지역 Food를 0으로 만드는 이벤트만 예약한다. 이 명령은 시계·스폰 웨이브·소비 스위치를 바꾸지 않는다.
- 완료 자원 장부는 `Before - EventLoss - Consumed - Rounding = After`를 검사하고 `After`를 0 이상 Capacity 이하로 유지한다. M3 초기화는 `FoodRegenerationRate=0`으로 설정한다. 일반 Ecology 구조에 재생 필드가 있어도 **현재 M3 실행 경로에서는 자동 재생하지 않는다.**

### M3.3: 이주·집계·표시

- `Migration.Enabled`가 켜진 경우 Food가 부족한 `Resident`는 명시적 인접 지역 중 Food가 있는 곳을 고른다. `FEcoTravelFragment::State`의 `Resident`/`Traveling`/`WaitingForFood`가 상태 기준이다. 이동 중에는 마지막으로 확정된 Region의 Population에 남는다.
- 엔진 Mass Movement가 위치를 이동한다. 목표 반경 및 목적지 Bounds 안에 도달하고 목적지 Food가 남아 있을 때 **기존 Entity와 StableAgentId 그대로** Region을 변경한다. 목적지가 고갈되거나 후보가 없으면 대기한다. 도착한 개체의 다음 소비는 실제 관측 도착 시각 + 소비 간격이다.
- 이주는 전체 Alive 수를 늘리지 않는다. 지역 상한은 신규 생성에 적용되며 이주 도착을 막지 않는다. Client의 Bubble Box 수는 relevancy에 따른 부분집합이므로 전체 Population으로 해석하지 않는다. Host의 ID/화살표와 양쪽 지역 라벨은 표시용이다.

## 3. 현재 설정과 편집 위치

설정은 **Project Settings → Adaptive Ecosystem**에서 확인하고 PIE를 재시작해 적용한다. 아래 `현재 ini`는 이 작업 트리의 [`DefaultGame.ini`](../../../Config/DefaultGame.ini) 값이다. 맵에 저장된 Bootstrap/Region 값은 에디터 Details에서 별도로 확인한다. 사용자의 현재 ini·레벨·에셋은 이 문서 정리에서 수정하지 않는다.

| 설정 | C++ 기본값 | 현재 ini |
| --- | --- | --- |
| 낮/밤 길이 | 60초 / 60초 | **10초 / 10초** |
| Feeding | 켬, 첫 소비 20초, 이후 10초, 요청 1 | **켬, 첫 소비 5초, 이후 5초, 요청 1** |
| 낮 Food 이벤트 | 끔, `Forest_A`, fraction 25/60, 손실 40 | 끔, 같은 값 |
| 밤 Food 이벤트 | 켬, `Forest_B`, fraction 0.25, 손실 40 | **끔**, 같은 대상·fraction·손실 |
| 정규 Spawn Waves / Migration / Debug 표시 | 켬 / 켬 / 켬 | 켬 / 켬 / 켬 |
| Migration 판단·속도·도착 반경·분산 | 1초 / 400 cm/s / 30 cm / 300 cm | 같은 값 |
| 최소 지역 / 전체 신규 생성 상한 | 2 / 128 | 별도 override 없음 |
| Bootstrap 초기 수·낮 간격/수·밤 간격/수·지역 생성 상한 | 8 · 30초/4 · 20초/6 · 64 | **맵 에셋 값에 따름** |

10초 Phase에서 **Bootstrap이 C++ 기본 간격 30초/20초 그대로라면 추가 웨이브가 없다.** 반복 생성을 보려면 각 Bootstrap을 낮 5초/4개, 밤 4초/6개 등 Phase보다 짧은 간격으로 설정해야 한다. 자동 낮/밤 이벤트도 현재 ini에서는 모두 꺼져 있으므로 명시적으로 켜야 한다.

## 4. 에디터 재현 절차와 기대값

### 공통 준비

1. Fragment/EntityConfig를 변경한 빌드를 쓰는 경우 에디터를 완전히 종료하고 다시 연다. 기존 M2 Box `EntityConfig`의 `Eco Network Agent` Trait를 사용한다. 별도 Creature Actor나 추가 이동 Trait는 넣지 않는다.
2. `AEcologyRegion` 두 개를 `Forest_A`, `Forest_B`로 두고 서로의 `AdjacentRegionIds`를 등록한다. 인접은 거리로 자동 판단하지 않는다. 각 지역에 Bootstrap 하나를 연결하고 `InitialAgentCount=8`, `bAutoInitialize=true`를 확인한다. 초기 Food≤Capacity, 스폰 슬롯·도착점이 Bounds 내부여야 한다.
3. 장애물 없는 예시는 A 중심 `(0,0,100)`, B 중심 `(4000,0,100)`, 각 Box Extent `(1500,1500,1000)`, Arrival Offset `(0,0,0)`이다. 실제 바닥 높이에 맞춰 Z를 조정한다. 현재 이동은 3D 직선이며 NavMesh/장애물 회피/지면 추적을 하지 않는다.
4. Standalone PIE의 권위 게임 뷰포트에서 명령을 실행한다. `mass.debug.DrawAllEntities 1`은 기존 Transform Entity를 그리는 명령이며 **생성 명령이 아니다**. 로그의 World·Epoch·Step·Due/Observed를 함께 확인한다.

### A. 웨이브와 상한만 분리

Feeding·낮밤 Food Event·Migration을 끄고, Spawn Waves를 켠다. A/B Food를 충분히 주고 낮/밤 10초, 초기 8, 낮 5초마다 4, 밤 4초마다 6, 지역 상한 64·전체 상한 128로 맞춘다. 낮은 +5초, 밤은 +4/+8초에 웨이브가 있다.

| 낮 시작 서버 시각 | A | B | 전체 |
| --- | ---: | ---: | ---: |
| 0초 | 8 | 8 | 16 |
| 20초 | 24 | 24 | 48 |
| 40초 | 40 | 40 | 80 |
| 60초 | 56 | 56 | 112 |
| 80초 | 64 | 64 | 128 |

이는 설정을 위와 같이 **덮어쓴 경우의 미검증 기대값**이다. `[Eco Spawn]`, `[Eco Spawn Skipped]`, `[Eco Daily Total]`을 확인한다. A Food만 0으로 재시작하면 A의 초기 8개는 남고 추가 웨이브만 스킵되어야 한다.

### B. 소비·이벤트만 분리

Migration·Spawn Waves를 끄고 각 경우마다 PIE를 다시 시작한다.

| 경우 | 명시적 설정 | 기대 결과 |
| --- | --- | --- |
| 소비 고갈 | 자동 이벤트 끔, Feeding 첫 20초/간격 10초/양 1, A Food=24·초기 8 | A Food가 20/30/40초에 16/8/0, 50초 지급 0 |
| 부족 배분 | 자동 이벤트 끔, A Food=5·초기 10·요청량 2 | 첫 요청 총 지급 5, 개체별 0.5 |
| 이벤트 고갈 | Feeding 끔, 양쪽 이벤트 **켬**, A/B Food=40·손실 40, 낮밤 10초, 기본 fraction | A 약 4.167초, B 약 12.5초에 Food=0. 각 Phase당 1회 |
| 같은 시각 충돌 | A Food=40, 낮 이벤트 fraction=0.5·손실 40, 첫 소비 5초, 낮 웨이브 5초 | 5초 손실 → A 웨이브 스킵 → 기존 개체 소비 지급 0 |

`[Eco Food Event]`의 `ActualLoss`와 `[Eco Food]`의 `Before/EventLoss/Consumed/Rounding/After`를 비교한다. 기존 문서의 20초/10초 소비 표는 **시험용 override**이며 현재 ini의 5초/5초 기본 실행 예상이 아니다.

### C. Starvation과 웨이브·이주 동시 실행

Spawn Waves·Migration·Debug 표시를 켜고, Bootstrap 낮 5초/4·밤 4초/6, 초기 A/B 각각 8로 둔다. A/B Food는 40/400 등 양수로 시작한다. 원인 분리를 위해 Feeding과 자동 Food 이벤트를 끄고 첫 웨이브 전에 권위 뷰포트에서 `Debug.Starvation.Forest_A`를 실행한다.

`[Eco M3.2] ... Waves=1`과 `[Eco M3.3] Ready` 후 `[Eco Debug] Starvation queued` → `[Eco Food Event] Source=Debug.Starvation` → `[Eco Food] ... Region=Forest_A ... After=0 ... Depleted=1`을 확인한다. A의 다음 웨이브는 `[Eco Spawn Skipped]`, B의 웨이브는 `[Eco Spawn] ... Region=Forest_B Initial=0`이어야 한다. 기존 A ID는 Traveling으로 이동해 `Arrived=1 From=Forest_A Region=Forest_B`에서 **같은 ID**로 도착해야 한다. 전체 Population 증가분은 B의 성공한 신규 생성 수와 같아야 한다. B의 이주 도착과 B의 신규 출생은 다른 사건이다.

별도 PIE에서는 A가 이동 중일 때 B에도 Starvation을 실행해 `WaitingForFood`, ID/전체 Population 보존을 확인한다. 또 Feeding을 켜 도착 ID의 `NextFeed=Observed+Interval`과 B에서의 다음 소비를 확인한다. 두 지역 모두 고갈된 상태에는 자동 재생이 없어 신규 웨이브가 계속 스킵된다.

### D. 네트워크와 안정성

Listen Server + Client에서 `AEcoGameState` 또는 그 Blueprint를 사용하고 명령은 Host에서만 실행한다. 양쪽의 완료 요약(시각·Food·Population·Traveling·Waiting)과 관련 Box의 Transform/Region을 비교한다. Host Mass ID/화살표는 자동 복제되지 않는다. 이어 Late Join, Bubble 범위 이탈·재진입, PIE 재시작, 30/60 FPS와 긴 프레임, 128개체 15분을 확인한다. 심한 hitch에서는 엔진 이동 적분의 프레임당 0.1초 제한으로 실제 도착이 늦어질 수 있다.

## 5. 검증 기록과 남은 작업

- **기존 기록:** UE 5.8 `AdaptiveEcosystemEditor Win64 Development` 빌드 성공. 과거 Standalone 로그에서 A Food 50→0, ID 1~8의 B 도착, Day 3 A/B=0/16과 전체 16을 관찰했다. 이 로그는 당시 추가 Spawn Waves가 꺼진 실행이다.
- **2026-10-02 문서 정리 검증:** C++·설정·레벨 에셋을 수정하지 않았다. UE 5.8 `AdaptiveEcosystemEditor Win64 Development` UnrealBuildTool 결과는 `Succeeded`이며 컴파일 대상은 `Target is up to date`였다. `git diff --check`와 문서 링크 검사를 통과했다. 위 A~D PIE 시나리오는 새로 실행하지 않았다.
- **다음 구현:** 지급 Food를 Vitals의 Energy/HP와 연결하고 기아·사망·Population 감소까지 검증한다. Food 재생과 플레이어/포식 위협, V1 Observation/Utility 및 PPO 입력과의 폐루프 결합도 필요하다. 현재 Vitals Fragment와 별도 정책/포식 코드의 존재만으로 M3 경로와 통합됐다고 판단하지 않는다.
- **추가 검증:** 소비·자동 이벤트 단독 수치, Spawn Waves와 이주 동시 PIE, 이동 중 양쪽 고갈, 도착 후 소비, Host/Client와 Late Join, 장시간·긴 프레임. 기존에 수동 확인하지 않은 항목은 계속 미검증으로 둔다.

### 이주 이력을 다음 생성에 반영하는 후속 설계

현재 `FEcoSpawnRequest`에는 출생 프로필 필드가 없고, 이주 완료 시 `MigrationOutcome`이나 유출·유입 특성 이력이 Ecology로 전달되지 않는다. **이주 경험에 따른 다음 개체의 특성 변화는 미구현**이다. 현재 B의 다음 웨이브는 시각·Food·Population/상한으로만 결정된다.

후속 구현 시 이주자 본체는 같은 ID로 목적지에 남기고, 다른 지역으로 **실제 도착한 경우에만** 출발지 유출·도착지 유입 이력을 한 번씩 기록한다. Ecology가 `(RegionId, SpeciesId)`별 제한된 이력과 현재 환경으로 `BirthProfile`/`ProfileRevision`을 확정해 다음 **적격** 정규 웨이브의 신규 ID에만 적용한다. 기존 주민·이주자 특성은 소급 변경하지 않는다. Food=0이거나 상한에 걸린 웨이브는 그대로 스킵하고, 이력은 이후 적격 웨이브까지 보관한다. 플레이어 사냥 압력·부모 유전·정책 계약 변경은 별도 설계와 Python/C++ 파리티 검증이 필요하다.

핵심 불변식은 `전체 Alive = 모든 Region Population 합`, `이주 자체의 전체 Alive 변화 = 0`, `신규 생성 ID ≠ 이주 ID`, `Food=0인 지역의 신규 생성 = 0`이다. 이주 도착이 식량·생성 규칙을 무시하는 강제 출생으로 이어져서는 안 된다.

## 관련 계약

- [PPO/Mass 아키텍처](../../Architecture/PPO_MASS_ECOSYSTEM_ARCHITECTURE.md)
- [Policy V1 계약](../../RL_Policy/POLICY_CONTRACT_V1.md)
- [Mass Processor 순서](../../Mass/MASS_PROCESSOR_ORDER.md)
- [Debug 명령](../../Debug/DEBUG_COMMANDS.md)
