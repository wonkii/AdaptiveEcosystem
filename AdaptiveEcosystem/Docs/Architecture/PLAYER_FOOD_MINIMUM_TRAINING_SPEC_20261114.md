# 플레이어 제공 먹이: 최소 학습 기반 개발 명세

> **대체된 지면형 명세 — 현재 구현 지시로 사용하지 않는다.** 2026-11-14 후속 요청은 손에 든 먹이 감지·키 급여·플레이어별 호감도다. [활성 명세](PLAYER_HELD_FOOD_AFFINITY_SPEC_20261114.md)와 [활성 계획/재개문](PLAYER_HELD_FOOD_AFFINITY_PLAN_20261114.md)을 따른다. 아래는 이전 검토 이력이다.

작성 2026-11-14 / 상태 **개발 전 명세**. 이 문서는 구현 또는 학습 성공 기록이 아니다.

기준: `jwj-1024`, HEAD `ef44df15` + 기존 미커밋 작업 트리. 시작할 때 실제 Source/diff를 다시 확인한다. 근거 조사와 장기 확장은 [전체 계획](PLAYER_FOOD_PROVISION_RL_PLAN_20261114.md), 작업 순서와 다음 채팅 시작문은 [최소 구현 계획](PLAYER_FOOD_MINIMUM_IMPLEMENTATION_PLAN_20261114.md)을 따른다.

## 1. 목표와 개발 중단선

**게임 담당의 완료 지점은 플레이어의 유한한 먹이가 지면에 놓이고, 사슴에게 지각 가능한 정보가 제공되며, 선택된 섭취 의도가 실제 Native 접촉·소비·영양 갱신으로 실행되고, 그 세계 규칙을 Python에서 비교 실행할 수 있는 상태다.**

실제 학습 시작에는 이 기반에 더해 RL 담당의 환경 연결·관측/행동 표현·reward·reset/termination 설정이 필요하다. 따라서 두 gate를 별도로 둔다.

| Gate | 책임 | 통과 의미 |
|---|---|---|
| `G_GAME: handoff_ready` | 게임/통합 담당 | 실행 가능한 먹이 규칙, UE 작은 장면, C++/Python 규칙 비교와 인계 패키지 완성 |
| `G_TRAIN: training_ready` | RL 담당 + 실행 경계 공동 검토 | 선택한 Python 환경에서 정책이 먹이 선택에 영향을 줄 수 있고 관측/행동/보상/reset이 연결되어 짧은 rollout 검사를 통과 |

이번 후속 개발의 기본 종착점은 **G_GAME**다. G_TRAIN은 RL 담당과 연결하는 작업이며, 현재 R7e가 먹이에 접근하는 것을 성공 조건으로 삼지 않는다. 학습 실행·새 모델·호감도는 G_GAME의 선행 조건이 아니다.

## 2. 최소 범위

| 지금 구현 | 이후로 미룸 |
|---|---|
| 1종 먹이의 정수 수량, 명시 초기 지급 | 범용 아이템 인벤토리·상점·채집·제작 |
| 버튼 1개로 전방의 유효 지면에 1개 놓기 | 손먹이·물리 투척·회수·교환 |
| 서버 소유량/배치 검증, 실제 수량·source 거래 | 예측 배치 보정·화려한 애니메이션 |
| 단일 지역, 1명 플레이어·1마리 사슴의 실행 장면 | 다지역 먹이 이전·MMO 영속 계정 inventory |
| 소량 source 배열, 2마리 경합 테스트 | 접근 슬롯 예약·혼잡 최적화·Smart Objects 도입 |
| 현재 보이는 먹이의 증거와 선택/소비 실행 접점 | 먹이 기억·후각·제공자 신뢰·플레이어별 관계 |
| 잔량/수량의 간단한 표시와 DTO | 대형 가방 UI·자산 제작·전용 먹기 애니메이션 |
| 세션 reset, ON 저장 명시 거절, OFF 회귀 | ON 세계 저장/로드·재접속 복원·장기 streaming acceptance |
| 순수 규칙 leaf, Python 참조 규칙, 재생 사례 | 기존 Python 학습 환경/보상/모델의 무단 수정 |

평면·정적 지면의 작은 owned fixture부터 검증한다. 기본 맵은 OFF이고 현재 사용자 맵/에셋은 보존한다. 실제 먹이 섭취와 기존 자연 풀의 공존은 필수 검증이며, 자연 풀 패치 S2~S6의 완성을 기다리지 않는다.

## 3. 확인한 Source와 설계 제약

| 현재 Source | 확인 내용 / 구현 시 적용 |
|---|---|
| [EcoPlayerState.h](../../Source/AdaptiveEcosystem/Player/EcoPlayerState.h) | ASC/체력/AvatarGeneration 존재. 먹이 수량은 Pawn 교체와 분리하여 PlayerState 소유 component에 둔다 |
| [EcoAbilityInputConfig.h](../../Source/AdaptiveEcosystem/Player/EcoAbilityInputConfig.h) | 기존 입력→GAS binding 재사용. 새 플레이어/게임모드 금지 |
| [EcoWorldV3.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h) | A: 이전 끝에서 준비한 요청 commit, B: 제어, C1: 이동, C4: 섭식/대사, E~F: 관측, H: 다음 요청 준비 |
| [EcoRepertoire.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoRepertoire.h) | 1unit=200cm, step=8/60초. EatWant/EffectiveEatMult/EnergyAfterIntake의 현재 식을 재사용 |
| [IntegratedProcessors](../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp) | HerbControl hook은 정책/제어 후, 위치 적분 전. 기존 Journey/Social/Life 보정과 우선순위 충돌을 감사해야 함 |
| [EcologySpatialFood.cpp](../../Source/AdaptiveEcosystem/Ecology/EcologySpatialFood.cpp) | 자연 FoodEaten 차이를 자연 잔량 보존 거래에 사용. 제공 먹이로 그 값을 증가시키지 않음 |
| [EcoCreatureLifecycle.cpp](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureLifecycle.cpp) | LastIntake 기반 표현 소비자 존재. 자연/제공/합계 역할을 명확히 갱신 |
| [Python world.py](../../../herbivore_rl/env_v2/world.py), [r8_world.py](../../../herbivore_rl/env_v2/r8_world.py) | 여러 baseline 존재. RL 담당이 학습 기준을 선택하며 게임 담당이 임의 대체하지 않음 |

R7e/V1/내장 R1b의 관측·행동·mask·추론·weights와 SocialControl=0을 보존한다. 새로운 먹이 기능은 별도 opt-in world rule이다. 런타임 Python 추론, 별도 위치 writer, 별도 BP 영양 writer를 추가하지 않는다.

## 4. 초기 개발 fixture 설정

아래 값은 **개발 재현용 시작값**이다. 기존 게임/RL 수치를 변경하는 지시나 최종 밸런스가 아니다. 구현 후 export한 하나의 설정을 C++/Python이 읽고, 값을 바꾸면 config hash와 fixture 기대값을 함께 재검증한다.

| 설정 | fixture 시작값 |
|---|---|
| Enabled | 기존 세계 false, 명시 fixture만 true |
| FoodType | `PlayerFeed.Basic` 1종, 현재 사슴 backend만 허용 |
| InitialItems | 세션/episode당 10개. 리스폰마다 재지급 금지 |
| FoodUnitsPerItem | 0.25 Native food units |
| NutritionMultiplier | 1.0; 환산은 현재 RepCfg.FoodEnergyPerUnit. 기존 값 변경 없음 |
| PlaceRangeCm | 200cm 이내. 서버가 전방 후보의 실제 지면 확정 |
| PlaceCooldownSteps | 8 Native steps |
| LifetimeSteps | 450 Native steps = 60 simulation seconds |
| MaxSourcesPerPlayer / Region | 8 / 16. 정리 완료한 source는 활성 수에 포함하지 않음 |
| ContactRadiusCm / HeightToleranceCm | 100cm / 50cm. 실제 이동 후 지면 기준 위치와 비교 |
| SenseRangeCm | 1600cm 상한, 기존 사슴 시야·날씨 조건과 함께 더 제한적인 범위 적용 |
| CandidateLimit | 4. 거리 오름차순, 동률 SourceId 오름차순 |
| 기존 섭취율·야간·비축·대사·안전 규칙 | 활성 baseline 설정 그대로 export. 먹이 전용으로 재정의하지 않음 |

누락/NaN/음수/불일치 단위·과도한 상한은 시작/요청 시 거절한다. 이 설정만으로 완전한 세계 복원은 불가능하므로 baseline/world flags/geometry를 manifest에 포함한다.

## 5. 모듈과 필수 인터페이스

다음 이름은 **신규 구현 예정 명칭**이다. 실제 구현에서 기존 코드를 더 작은 변경으로 확장할 수 있으면 명칭보다 소유권을 우선한다.

| 모듈 | 책임 |
|---|---|
| `UEcoPlayerFoodInventoryComponent` | PlayerState의 서버 수량, grant/placement 거래·revision, owner-only 수량 표시 |
| `UEcoPlayerFoodProvisionComponent` | 플레이어 소유 요청 endpoint, 현재 Avatar 검증, 서버 지면 확인, pending 큐 |
| `EcoPlayerFoodRules` 엔진 비의존 leaf | source 수명, placement/consume 규칙, deterministic 배분, 잔량·receipt. UObject 참조 없음 |
| 기존 Native context + 실행 adapter | leaf 상태 소유, 증거 snapshot, 의도 수명, 기존 이동/C4에 연결 |
| 기존 Ecology 서비스의 제공 먹이 하위 장부 | 공급·소비·폐기의 감사. 자연 FoodAmount/Capacity 의미 유지 |
| 지역 Actor의 표현 DTO + 최소 표시 | source ID별 위치/잔량/종료 revision. 논리 tick/영양 처리 없음 |

필수 동작은 `Reset`, `TryPlace`, `ExpireAtStep`, `BuildEvidence`, `SubmitFeedIntent`, `ResolveIntake`, `CaptureState`다. Blueprint/네트워크 endpoint와 순수 규칙 함수는 분리한다. 먹이마다 AIController/BT/독립 Mass 개체는 만들지 않는다.

## 6. 요청·state·증거·결과 계약

### 6.1 플레이어 요청

`PlaceRequest = {RequestSequence, AvatarGeneration, FoodTypeId, AimHint}`

- PlayerKey는 서버의 owning connection으로 확정한다. Amount/영양/사슴 ID는 받지 않는다. 1요청=1개다.
- 중복 키 `(SessionEpoch, PlayerKey, RequestSequence)`는 기존 결과를 반환한다. 수량을 다시 쓰지 않는다.
- 서버 소유권, 현재 Pawn/생존, 수량/pending 예약, 쿨다운, 큐/source 예산, 실제 지면/차폐/높이·거리·region을 검증한다. Native 적용 경계에서 다시 유효성을 확인한다.
- inventory 차감과 source 등록은 한 거래다. 실패한 요청은 둘 다 0. source capacity 확보 전 inventory를 확정 차감하지 않는다.
- 같은 step의 placement는 서버 접수 sequence 순서로 정렬하며 replay에 기록한다. client가 임의 step으로 과거를 수정하지 못한다.
- 성공/거절 모두 `RequestSequence, ResultCode, AppliedStep, InventoryRevision, SourceKey?`로 응답한다. 일반적인 재고/지면 거절로 세션을 끊지 않는다.

### 6.2 권위 source

```text
SourceKey = (WorldEpoch, SourceId, Generation)
SourceState = {Key, RegionId, FoodTypeId, PositionCm, GroundRevision,
               InitialFood, RemainingFood, CreatedStep, ExpireStep,
               ProviderKey, AmountRevision, Status}
Status = Available | Depleted | Expired | Removed
```

SourceId는 epoch 안에서 단조 증가하고 재사용하지 않는 방식을 우선한다. AmountRevision은 일부 섭취로 증가하지만 source identity가 바뀌지는 않는다. 다른 사슴이 일부 먹었다는 이유만으로 모든 의도를 stale identity로 거절하지 않는다. 최종 단계에서 현재 잔량으로 재배분한다.

ProviderKey는 감사용이다. 최소 버전은 제공자 인식·호감도를 policy 입력으로 제공하지 않는다. player가 떠나도 이미 놓인 먹이는 만료까지 유지한다. Actor pointer를 provenance로 저장하지 않는다.

### 6.3 사슴이 읽는 FoodEvidence

```text
FoodEvidenceSnapshot = {WorldEpoch, AgentKey, EvidenceStep, CandidateCount,
    Candidates[4]: {valid, SourceKey, RelativePositionNative, visible_now}}
```

- 현 시야·거리·static LOS·식생 차폐를 통과한 Available source만 후보가 된다. 정확한 숨은 잔량/가방 수량/제공자 ID는 노출하지 않는다.
- 기본 센서는 현재 보이는 먹이의 위치와 존재만 전달한다. 정확한 잔량 관측·기억·후각은 후속이다.
- 현재 살아 있는 개체의 heading 기준 **좌표축을 명시**한다. 첫 판은 world XY 축의 상대 native units, Z는 cm 높이 차를 Native로 변환한 값으로 통일한다. 정책 정규화는 RL 소유다.
- 빈 슬롯은 valid=false, payload=0. 원점의 실제 먹이와 구분한다. ID는 routing용이며 신경망에 연속 숫자로 넣지 않는다.
- 순수 leaf는 외부 geometry adapter가 전달한 LOS/지면 증거를 검증/사용한다. Python도 같은 입력을 재생할 수 있어야 한다. 평면 slice만 검증했으면 3D 전체 parity로 표기하지 않는다.

### 6.4 실행 의도: policy action이 아닌 환경 접점

```text
FeedIntent = {WorldEpoch, AgentKey, SourceKey, EvidenceStep,
              ApplyStep, Mode: None | ApproachAndEat, Origin: Fixture | PolicyAdapter}
```

- `None`은 제공 먹이를 선택하지 않는다는 뜻이며 기존 자연 행동을 유지한다.
- `ApproachAndEat`는 선택한 source에 접근해 조건이 맞으면 섭취하려는 **실행 명령의 의미**다. 이것을 새 PPO action 번호로 추가하는 결정은 RL 담당 업무다.
- 명령은 정확히 ApplyStep 한 step에만 유효하다. stale/future/다른 epoch·agent generation·증거에 없는 source는 거절한다. 지속 접근은 현재 유효 증거와 명령으로 갱신한다.
- 정책을 읽지 않는 step에는 이미 선택된 목표를 실행 adapter가 재검증하여 같은 의도를 갱신할 수 있다. 이것은 새 목표 선택/재추론이 아니며 기존 policy 판단 간격을 단축하지 않는다. 증거에서 사라지거나 상위 행동이 바뀌면 취소한다. 이 유지 규칙도 Python adapter에 동일하게 반영한다.
- 처음에는 test-only scripted adapter가 이 접점을 사용한다. production 경로는 승인된 PolicyAdapter가 없으면 None이며, 주변 먹이를 발견했다고 자동으로 목표를 선택하지 않는다.
- 제공 먹이 실행은 현재 실행 상태가 Graze이고, 도주/피난 강제 상태·active travel·Social 이동·sleep 등이 없어야 한다. 기존 Life 경계/호기심 보정과 동시에 서로 다른 방향을 쓰지 않도록 선택/거절 이유를 기록한다. 기존 상태의 우선순위를 제거해 fixture를 통과시키지 않는다.
- 접근 제어는 기존 Native controller/hook에서만 생성하고 위치는 C1에서 적분한다. 몸이 접촉 범위에 오면 기존 허용된 정지/섭식 control을 사용한다. 새로운 자동 행동 선택기는 만들지 않는다.

**RL 연결 전제:** 현재 5행동과 `FeedIntent` 사이의 mapping은 아직 없다. RL 담당이 관측/목표 선택/행동 표현을 정하고 대응 adapter를 작성해야 G_TRAIN이 된다. 기존 Raw Request/예약 action 슬롯을 몰래 바꾸지 않는다. FeedIntent로 만든 접근을 기존 모델이 학습한 결과로 기록하지 않는다.

### 6.5 결과

`IntakeOutcome = {EventId, Step, AgentKey, SourceKey, RequestedFood, GrantedFood, SourceBefore, SourceAfter, NutritionContribution, ResultCode}`

물리적 섭취량과 영양 기여만 담고 reward를 계산하지 않는다. `NutritionContribution`은 먹이에서 들어온 양이며 대사·비축·clamp 이후 `EnergyAfter−EnergyBefore`와 다르다. 그 전후 값은 diagnostic trace에 별도로 남긴다. 0 섭취를 성공 보상 사건으로 쓰지 않는다.

기본 ResultCode: `Accepted`, `EmptyInventory`, `Cooldown`, `InvalidAvatar`, `InvalidPlacement`, `CapacityLimit`, `InvalidEvidence`, `StaleEpoch`, `Expired`, `Depleted`, `NotEligible`, `OutOfContact`, `NoDemand`. 요청 재전송은 새 EventId를 만들지 않는다. 진단용 거절은 개체에게 알려지지 않은 원격 세계 정답을 observation에 전달하지 않는다.

## 7. 시간 순서: 현재 Native 파이프라인 보존

Source의 현재 동작은 **step 끝 관측/다음 요청 준비 → 다음 step A에서 실행**이다. 전체 계획의 추상 순서를 구체화하여 다음과 같이 고정한다.

1. 실행 step t 시작: pending placement 반영, `t >= ExpireStep`인 source 만료. 기존 policy는 앞서 준비된 관측/요청을 그대로 사용한다.
2. A/B: 앞선 증거에 기반한 FeedIntent(t)를 검증하고 허용된 경우에만 control에 반영한다. t에 새로 놓인 source는 앞선 증거에 없으므로 t에 처음 선택할 수 없다.
3. C1~기존 포획 단계: 기존 이동/충돌·안전 처리. 별도 위치 갱신 없음.
4. C4: 실제 위치/높이/LOS, 생존/포획 확정 여부, 현재 source 상태, 섭취 자격을 재검증한다. 제공 먹이는 C4 전 이미 포획 확정된 대상에게 주지 않는다. 기존 자연 경로의 포획/섭식 순서는 별도 변경하지 않는다.
5. C4에서 출처별 잔량/영양을 한 번 반영하고 기존 비축·대사·생명주기를 진행한다.
6. E~F/H와 정렬되는 끝 snapshot에서 `EvidenceStep=t`인 먹이 증거를 생성한다. Fixture/향후 PolicyAdapter는 이 증거로 ApplyStep=t+1 의도를 준비한다. 기존 policy 판단 주기를 매 step으로 바꾸지는 않는다.
7. receipt/표현/trace 게시. reset에서는 초기 세계의 evidence를 먼저 만들어 최초 의도 입력을 준비한다.

새 schema를 가진 정책이 먹이 증거를 읽게 할 때는 H의 encoder/Decide 접점이 필요하다. 그 변경은 RL 담당과 공동 계약하며, 단순히 Step 반환 후 snapshot을 붙이고 같은 시점의 기존 inference가 이미 읽었다고 주장하지 않는다.

## 8. 수요·경합·장부

### 8.1 수요와 실제 섭취

```text
E_drained[i] = 기존 DrainAmount로 계산
want[i] = 기존 EatWant(E_drained[i], EffectiveEatMult(...), RepCfg)
W[source] = sum(유효 제공 먹이 수요)
T[source] = min(RemainingFood[source], W[source])
grant[i] = W[source] > 0 ? want[i] * T[source] / W[source] : 0
```

동일 source의 개체는 StableAgentId 순으로 합산하며 마지막 양의 수요 행에 반올림 잔차를 보정한다. 총 소비=T, 0≤grant≤want를 수치 tolerance 내 확인한다. SourceId/개체 순서를 바꿔도 같은 결과가 되도록 입력 정렬을 공통 규칙으로 제공한다.

선택 출처는 Natural 또는 Provision 1개다. 제공 목표를 선택했다면 자연 풀 수요에는 넣지 않는다. 제공 접촉 실패/소진은 이번 step 섭취0이며 자연 풀로 같은 step에 우회하지 않는다. None/부적격으로 제공 의도가 거절된 경우는 기존 자연 실행을 유지한다. 따라서 **의도 승인 후 소비 실패**와 **의도 자체 거절**을 결과에서 구분한다.

Energy/Reserve는 기존 C4 한 곳에서 갱신한다. Natural은 기존 NutritionMultiplier, Provision은 fixture 1.0을 사용한다. 기존 `FoodEaten`과 `LastIntake`는 자연 의미를 유지하고 `ProvisionIntake`/`TotalIntake`를 추가한다. 풀 뜯김 증거는 Natural만, 전체 먹기 표시는 Total을 읽는다. 기록 필드의 모든 소비자를 감사한다.

### 8.2 보존식

```text
InventoryEnd = InventoryStart + GrantedItems - PlacedItems
ProvisionEnd = ProvisionStart + PlacedFood - IngestedFood - ExpiredFood - RemovedFood
NaturalEnd   = NaturalStart - NaturalIntake + NaturalGrowth - NaturalLoss
```

제공 먹이를 자연 재생량·허용 오차 조정에 넣지 않는다. Ecology 기존 서비스에 별도 제공 먹이 receipt를 추가하고 SourceKey/step/sequence를 검증한다. 같은 step의 모든 수지를 검증한 뒤 성공으로 게시한다. 검증 실패는 세션 진단 실패로 처리하고 조용히 넘기지 않는다.

## 9. reset·수명·저장·표현의 최소 요구

- episode reset은 WorldEpoch를 바꾸고 source/의도/pending/중복 키/receipt/sensor cache를 비운 뒤 설정한 초기 상태와 InitialGrant를 정확히 한 번 구성한다.
- 플레이어 리스폰은 inventory 유지, AvatarGeneration 교체, 옛 pending 요청 무효화다. 로그아웃 뒤 placed source는 TTL 유지. inventory 재접속 영속성은 지원하지 않는다.
- 일반 rendering/actor relevancy 변화는 source를 삭제하지 않는다. 실제 region teardown은 남은 food를 Removed로 정산한다.
- feature ON 상태에서는 기존 checkpoint 저장/불러오기와 기존 학습 snapshot의 '복원 가능' export를 명시 거절한다. 이 feature의 새 trace/export만 허용한다. OFF 기존 저장은 그대로 유지한다. joint save 구현은 G_GAME에 필요하지 않다.
- UI는 수량 텍스트와 단순 먹이 mesh/marker로 충분하다. 서버 결과만 논리 상태이고 client에서는 절대로 source 잔량/사슴 energy를 적분하지 않는다.
- 네트워크 endpoint와 owner-only 수량/주변 source DTO를 제공한다. G_GAME 인계 전 서버+client 1개에서 배치·수량·잔량의 기본 동기화를 확인한다. 대규모 동시접속·장기 패킷/streaming QA는 후속이다.

## 10. Python에 넘기는 최소 패키지

아래는 구현 단계에서 **실제로 생성할 산출물**이며 지금 존재한다고 가정하지 않는다.

| 파일/도구 | 요구 내용 |
|---|---|
| `contract.md` | 본 명세의 구현 확정판 + 실제 함수/필드/지원 world profile |
| `config.json` | 위 fixture 값과 활성 baseline의 단위·대사·섭취/감각 설정, version/hash |
| `rules_reference.py` | 엔진 비의존 제공 먹이 규칙만. 기존 학습 env/reward/model을 수정하지 않는 인계용 Python 참조 |
| C++ rule CLI | 같은 JSON 입력을 읽고 placement/evidence/섭취/잔량 결과 출력 |
| `cases.jsonl` | 입력/기대 출력/비교 항목과 출처. 수작업 산술 fixture와 UE 실제 capture를 구분 |
| `compare_rules.py` | C++↔Python 결과를 실행 비교하고 첫 차이·종료코드 보고 |
| `replay.jsonl` | 초기 상태, 적용 step, 플레이어 입력/pose, geometry 증거, scripted 의도, 실제 결과 |
| `manifest.json` | Source SHA+dirty diff hash, config/규칙/geometry/fixture hash, 단위, seed, coverage와 미지원 목록 |
| `README.md` | 새 clone에서의 실제 설치/실행 명령, 예상 출력, 실패 재현, G_GAME/G_TRAIN 상태 |

배치 제안: `AdaptiveEcosystem/Tools/EcoPlayerFoodEval/`에 CLI/참조/비교 도구, `Docs/RL_Policy/Examples/PlayerFood/`에 작은 계약/fixture/manifest, 실행 산출물은 프로젝트 `Saved/PlayerFood/`에 둔다. 학습 담당의 `herbivore_rl/` 환경 연결은 별도 변경이다. 위 디렉터리는 후속 작업에서 만들며 빈 파일로 전달 완료를 표시하지 않는다.

G_GAME의 `handoff_ready=true`는 모든 준비된 산출물/검증에 대응해야 한다. `training_ready=false`는 RL 환경 연결 전까지 유지한다. Policy observation schema/hash를 게임 담당이 대신 정하지 않는다. 일반 full diagnostic snapshot은 신경망 입력이 아니다.

## 11. 필수 acceptance

| ID | 사례 | 통과 조건 |
|---|---|---|
| A01 | 지급10→1개 배치 | 수량9, source 0.25, 공급 receipt1, 자연 Food 변화0 |
| A02 | 재고0/벽/허공/예산 초과/옛 Avatar | 수량/source 변화0, 명시 거절 |
| A03 | 마지막1개로 중복/동시 요청 | 확정 공급1개, 음수0, 같은 요청 receipt 중복0 |
| A04 | source 생성 t | t의 앞선 evidence로 선택 불가, 끝 t evidence→최초 t+1 선택 가능 |
| A05 | 시야/차폐/미감지/4개 초과 | 허용 후보만 deterministic 정렬, 가방/제공자/숨은 잔량 누출0 |
| A06 | 이동·접촉 전후 | 접촉 전 소비0, 실제 도착 후 유효 수요만 소비, 두 번째 이동/energy writer0 |
| A07 | 2개체 부족량 경합 | 예: want .04/.02, source .03 → grant .02/.01, 합 .03. 순서 뒤집어도 결과 동일 |
| A08 | 자연 풀 위 제공 먹이 | 선택 출처 예산1회, 제공 소비가 자연 장부/뜯김 증거를 만들지 않음 |
| A09 | 만료 경계/부분 섭취 | t=ExpireStep 소비0, 잔량만 폐기. .25 중 .10 소비 후 만료하면 폐기 .15 |
| A10 | 포만/도주/수면/여행/포획/사망 | 자격·우선순위·기존 대사 준수, 원격/금지 섭취0 |
| A11 | reset/리스폰/ON 저장 | 옛 epoch 요청 영향0, 리스폰 재지급0, 미지원 저장 명시 거절 |
| A12 | Standalone + 서버/클라이언트1 | 서버 생성/소비만1회, 수량/잔량 수렴, client energy/source writer0 |
| A13 | C++/Python 같은 입력 | ID/상태/거절 정확 일치, 수치 허용 오차 이하, 첫 divergence 보고 |
| A14 | feature OFF | 기존 추론 Golden·관련 Native/자원·저장 baseline 회귀 없음 |

수치 비교 초기 기준: `abs(a-b) <= 1e-8 + 1e-9 * max(abs(a),abs(b))`. ID/순서/개수는 오차 없이 일치. JSON int64는 문자열. tolerance를 확대해 잘못된 순서/수지 문제를 숨기지 않는다.

A07/A09 숫자는 leaf 산술 입력 예이며 UE 플레이에서 실제 그런 want가 생성됐다는 증거가 아니다. UE end-to-end capture는 별도로 수집한다. 테스트 실패를 Golden 덮어쓰기로 해결하지 않는다.

## 12. RL 담당의 G_TRAIN 조건

G_GAME 패키지를 받은 RL 담당은 다음을 완료해야 한다.

1. 사용할 Python baseline과 world flags를 고정하고 참조 규칙을 실제 env reset/step에 연결한다.
2. FoodEvidence의 인코딩·정규화·mask, 행동→FeedIntent mapping을 결정한다. 접근 방향이 정책인지 실행 controller인지 명시한다.
3. scripted player의 배치/이탈 입력을 episode 안에 넣고 자연 먹이와 제공 먹이 결과를 구분한다.
4. reward, episode 종료/시간 제한, dead agent/reset 처리를 정한다. 소비 요청/배치와 실제 섭취를 구분한다.
5. 고정 seed reset 재현, finite obs/reward, action 유효성, 정상 terminal/reset과 짧은 rollout 검사를 통과한다.

이 작업이 끝나면 제한된 먹이 접근·섭취 학습을 시작할 수 있다. 제공자 개인에 대한 호감도 학습, 임의 종, 전체 life 세계 parity, production 정책 배포가 완료됐다는 뜻은 아니다.
