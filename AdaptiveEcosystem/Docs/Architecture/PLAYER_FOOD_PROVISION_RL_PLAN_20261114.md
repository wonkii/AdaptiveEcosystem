# 플레이어 먹이 소유·제공 시스템과 Python 학습 인계 계획

> **지면형 조사/계획 이력.** 2026-11-14 후속 요청으로 [손에 든 먹이·급여·개인 호감도 명세](PLAYER_HELD_FOOD_AFFINITY_SPEC_20261114.md)와 [구현·Python 인계 계획](PLAYER_HELD_FOOD_AFFINITY_PLAN_20261114.md)이 현재 기준이다. 아래 지면 배치 권장·최소 명세 우선 안내는 이전 결정이며 지금은 적용하지 않는다.

작성: 2026-11-14. **상태: Source 감사와 외부 자료 조사에 기반한 제안. 구현·학습·UBT/PIE 검증 기록이 아니다.**

후속 사용자 요청의 **최소 학습 기반 개발**은 [최소 명세](PLAYER_FOOD_MINIMUM_TRAINING_SPEC_20261114.md)와 [단계별 구현·재개 계획](PLAYER_FOOD_MINIMUM_IMPLEMENTATION_PLAN_20261114.md)을 우선한다. 아래 내용은 전체 방향이며, 최소 판은 ON 저장/로드·호감도·정책 학습을 제외하고 G_GAME 인계 지점까지 진행한다. 실제 Native의 이전-step 판단을 보존하는 구체적인 시간 순서도 최소 명세를 따른다.

확인 기준: `jwj-1024`, HEAD `ef44df15` 및 현재 작업 트리. `EcoWorldV3.h`, `EcoPolicyV3IntegratedProcessors.cpp/.h`, 플레이어 무기 코드, 개체 Inspector, 맵 external actor, PROJECT_CONTEXT/PROGRESS 등에 기존 미커밋 변경이 있다. 구현 착수 시 이 변경과 최신 HEAD를 다시 대조하며 덮어쓰지 않는다.

## 1. 결정 요약과 이번 범위

**게임 담당은 먹이가 존재하고 제공·감지·섭취되는 세계 규칙과 검증 가능한 결과를 제공한다. RL 담당은 그 세계에서 사슴이 무엇을 보고 어떤 행동을 선택하며 무엇을 보상받는지 결정한다.**

첫 제품 범위는 다음과 같다.

1. 플레이어가 먹이 한 종류를 수량으로 소유한다.
2. 기존 입력 경로로 가까운 유효 지면에 먹이 1개를 놓는다.
3. 서버가 소유량을 차감하고 유한 잔량·수명·출처가 있는 제공 먹이를 생성한다.
4. 사슴이 감지할 수 있는 정보와 실제 접근 후 섭취할 수 있는 실행 인터페이스를 제공한다.
5. 실제 섭취만 잔량과 Native 에너지/비축에 반영하고 결과를 남긴다.
6. 같은 규칙을 Python에서 재현할 수 있도록 명세·설정·입력 재생·기대 결과를 전달한다.

첫 버전은 **지면에 놓기**를 권장한다. 손으로 먹이기에는 움직이는 손/입 접촉·애니메이션·취소·접근 목표 추적 계약이 추가된다. 물리 투척도 비행/충돌/최종 착지 재현이 필요하므로 뒤로 미룬다. 클릭한 사슴에게 즉시 영양을 넣는 방식은 접근·위험·섭취 선택을 학습할 기회를 없애므로 채택하지 않는다.

호감도 수치, 신뢰 증가, 강제 접근/따라오기, 길들이기, 상점·제작·채집 전체, 새 정책·reward·가중치 변경은 이 단계의 기능에 포함하지 않는다. 테스트용 초기 지급은 명시적인 `InitialGrant` 사건으로 기록한다. 실제 채집으로 확장할 때는 자연 자원 차감→인벤토리 증가 거래를 별도로 구현한다.

## 2. 현재 프로젝트에서 확인한 사실

| 확인 대상 | 현재 근거 | 설계에 주는 영향 |
|---|---|---|
| 플레이어 수명·GAS | [EcoPlayerState.h](../../Source/AdaptiveEcosystem/Player/EcoPlayerState.h), [cpp](../../Source/AdaptiveEcosystem/Player/EcoPlayerState.cpp): ASC, 체력, AvatarGeneration, 사망/리스폰 | 먹이 수량은 Pawn 재생성과 분리하고 기존 입력/능력 경로를 재사용 |
| 입력과 요청 | [EcoAbilityInputConfig.h](../../Source/AdaptiveEcosystem/Player/EcoAbilityInputConfig.h), [EcoWeaponFireComponent.cpp](../../Source/AdaptiveEcosystem/Player/EcoWeaponFireComponent.cpp) | 입력→능력→소유 Actor/component의 서버 요청→검증 패턴을 확장 |
| 먹이 인벤토리/제공 | 관련 C++/Python 이름과 Player 코드를 검색한 범위에서 전용 경로를 찾지 못함 | 기존 기능 완료로 가정하지 않음. Blueprint 에셋 전체 감사는 구현 착수 때 수행 |
| 사슴 섭식 | [EcoWorldV3.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h) C4: 이동 후 셀 수요를 비례 배분, Energy/Reserve 갱신 | 제공 먹이도 이 실행 경계로 연결. Actor overlap에서 별도 영양 writer 금지 |
| 자연 먹이 장부 | [EcologySpatialFood.cpp](../../Source/AdaptiveEcosystem/Ecology/EcologySpatialFood.cpp): Before−Consumed+Regenerated−EnvironmentLost=After | 제공 먹이를 재생량으로 위장하거나 자연 풀과 이중 집계하지 않음 |
| 섭식 기록 소비자 | [IntegratedProcessors](../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp): FoodEaten 차이로 M4 receipt, LastIntake로 BrowseEvidence; [Lifecycle](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureLifecycle.cpp): 섭식 표현 | 기존 필드에 제공 먹이를 무조건 합치면 자연 풀 수지/흔적이 달라짐. 출처별 기록 필요 |
| 자연 풀 패치 | [패치 명세](FOOD_PATCH_GRAZING_IMPLEMENTATION_SPEC_20261108.md), [S0/S1 기록](FOOD_PATCH_GRAZING_S0_S1_20261108.md) | 공통 배치/지면 단계는 있으나 패치별 잔량·예약·도착 소비는 완료 전제 불가 |
| Python | [env_v2/world.py](../../../herbivore_rl/env_v2/world.py), [vec_env.py](../../../herbivore_rl/env_v2/vec_env.py), [repertoire.py](../../../herbivore_rl/env_v2/repertoire.py) | v3 관련 코드도 env_v2에 존재. 새 env_v3/PettingZoo 프로젝트를 임의 생성하지 않음 |
| Python 별도 실험 | [r8_world.py](../../../herbivore_rl/env_v2/r8_world.py) 등 별도 경로 존재 | RL 담당이 실제 학습 기준을 지정해야 함. 파일 존재를 기본 Native 지원으로 해석하지 않음 |
| 단위와 기본 정책 | [EcoRepertoire.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoRepertoire.h), [RL 담당 안내](../RL_Policy/ECOSYSTEM_RL_AGENT_GUIDE_20261024.md) | Native 1unit=200cm, step=8/60초. 통합 R7e 24관측/5행동·mask5와 V1/내장 R1b를 보존 |

기존 FMemory의 익숙함·두려움 등은 본 작업에서 그대로 둔다. 먹이 제공자를 기록한다는 이유만으로 사슴의 관계 기억을 변경하지 않는다.

## 3. 공식 문서·연구에서 채택할 원칙

아래의 '프로젝트 적용'은 외부 문서가 이 프로젝트를 검증했다는 의미가 아니라, 자료를 근거로 한 설계 판단이다. Epic 기본 URL은 열람 시 UE5.9 문서로 표시됐고 일부 검색 결과는 5.8로 표시됐다. 여기서는 공통 설계 원칙만 참고했다. 실제 API는 구현 시 설치 UE5.8 소스/헤더와 대조한다.

| 근거 | 자료가 설명하는 내용 | 프로젝트 적용 |
|---|---|---|
| [Epic: Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine) | 소유 inventory와 사용 중 equipment의 수명·표현·복제 범위를 분리 | 정적 먹이 정의, 소유 수량, 놓인 먹이의 런타임 상태를 분리. Lyra 전체 프레임워크 이식은 불필요 |
| [Epic: Remote Procedure Calls](https://dev.epicgames.com/documentation/unreal-engine/remote-procedure-calls-in-unreal-engine) | RPC 실행은 Actor/component 복제·소유 연결에 의존 | 플레이어 소유 경로에서 요청. 서버가 실제 소유량·지면·거리·생존을 검증. 지속 잔량은 복제 상태로 전송 |
| [Epic: Smart Objects Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/smart-objects-in-unreal-engine---overview) | 공간 질의와 예약 가능한 상호작용 정보를 제공하며 실행은 interactor가 담당 | 먹이 source는 위치·유효성·섭취 가능성을 제공하고 기존 Native가 실행. 기존 풀 명세처럼 플러그인 신규 도입은 하지 않음 |
| [Farama: Create a Custom Environment](https://gymnasium.farama.org/introduction/create_custom_env/) | 내부 상태, observation, 진단 info를 구분하고 seed 기반 reset/step을 구성 | 세계 정답과 사슴에게 허용된 센서를 분리. 기존 VecEnv API를 보존하며 이 원칙 적용 |
| [Juliani et al.: Unity: A General Platform for Intelligent Agents](https://arxiv.org/abs/1809.02627) | 구성 가능한 게임 시뮬레이션을 학습 환경으로 사용하는 연구 플랫폼 | 먹이 규칙과 시나리오를 설정/입력으로 분리. Unity 도입이나 런타임 Python 연결을 뜻하지 않음 |
| [Leibo et al.: Scalable Evaluation of Multi-Agent Reinforcement Learning with Melting Pot](https://arxiv.org/abs/2107.06857) | 다른 에이전트의 행동도 환경의 일부이며, 새 상호작용 상황에 대한 일반화 평가가 필요 | 먹이를 주는 사람만 등장하는 train 장면에 한정하지 않고 빈손·이탈·복수 사람·경쟁 개체를 별도 평가 |

## 4. 권장 구조와 책임

```text
플레이어 입력 / 테스트 시나리오
  → 플레이어 소유 요청 endpoint
  → 서버 검증 + 인벤토리/세계 먹이 생성 거래
  → 기존 지역 Native context의 제공 먹이 저장소
       ├─ 감지 조건을 통과한 FoodEvidence → RL encoder (RL 담당)
       ├─ 최종 승인된 섭취 의도 → 기존 Native 이동 → C4 섭식/대사
       └─ 출처별 결과 receipt → Ecology 장부 / 표현 DTO / 검증 trace

동일 world-rule 설정 + 입력 사건 + 초기 상태
  → Python mirror → 동일 결과 비교 → RL 학습/평가 → 기존 C++ 모델 로딩 경로
```

아래 클래스/함수명은 **제안명이며 현재 존재하는 API가 아니다.**

| 구성 | 소유자·역할 | 직접 하지 않는 일 |
|---|---|---|
| `UEcoFoodItemDefinition` | DataAsset: FoodTypeId, 개당 먹이량, 식용 대상, 제공 규칙, 외형 | 개별 플레이어 수량/개별 먹이 잔량 저장 |
| `UEcoPlayerFoodInventoryComponent` | 기존 PlayerState에 두는 서버 권위 수량·revision·지급 내역. 소유 클라이언트에만 잔량 공개 | Pawn 리스폰마다 초기 지급 반복 |
| `UEcoPlayerFoodProvisionComponent` 및 기존 능력 입력 | 현재 Pawn의 요청 endpoint, AvatarGeneration 확인, 서버 검증/거절 이유 | client가 영양량·제공자 ID·사슴 HP를 결정 |
| `FProvisionFoodStore` | 기존 Native context에 소유되는 엔진 비의존 데이터: SourceId, 위치, 잔량, 만료, 출처 | 자체 movement tick, 자연 풀 재생, 독립적 에너지 갱신 |
| 먹이 실행 adapter | 최종 선택된 대상/의도를 받아 Native 단계에 전달, 도착/생존/접촉 재검증 | 자동으로 모든 사슴을 유인하거나 Raw Request 덮어쓰기 |
| Ecology 확장 receipt | 기존 권위 서비스 내 제공 먹이 하위 장부 및 집계 | Subsystem RPC/직접 복제, 자연 FoodAmount 의미의 조용한 변경 |
| 표현 transport | 기존 지역 authority Actor의 제공 먹이 DTO 배열 + 클라이언트 표시 컴포넌트 | Mesh/Overlap/AnimNotify를 권위로 사용 |

먹이 한 개마다 AIController/BT/Mass Entity를 만들 필요는 없다. 첫 버전은 지역별 제한된 source 배열과 source ID 기반 표현을 권장한다. 표현 Actor/instance가 없어지거나 카메라가 멀어져도 논리 먹이는 유지한다. 지역 시뮬레이션이 실제로 종료될 때의 정리는 별도 사건이다.

## 5. 플레이어 소유·놓기 거래

### 5.1 기본 조작

- 입력은 기존 Enhanced Input/GAS binding에 연결한다. 키는 기존 맵 입력 충돌을 확인한 뒤 정한다.
- HUD에는 소유 수량, 로컬 배치 미리보기, 서버 거절 이유를 표시한다.
- 초기 버전은 1회 1개, 가까운 지면에 놓기, 회수/교환 없음. 유효하지 않은 지면이면 수량은 그대로다.
- 음식 정의는 한 종류로 시작한다. 식용 여부는 명시 설정하고 임의 몬스터 지원으로 확대하지 않는다.
- 테스트 지급량·거리·수명·요청 간격·동시 source 상한·섭취 반경은 하나의 versioned 설정에서 정한다. 확정 전 값은 gameplay 기본값으로 문서화하지 않는다.

### 5.2 요청과 원자적 반영

1. client는 `RequestSequence`, `AvatarGeneration`, 선택한 `FoodTypeId`, 배치 방향/후보를 요청한다. 제공자 ID·개당 영양·최종 위치·소비량은 서버가 결정한다.
2. 서버는 소유 connection, 현재 Pawn/세대, 생존, 수량, 쿨다운/큐 상한, 유한값, 거리, 차폐, 실제 지면·높이·경사, 활성 region, source 예산을 검사한다. 지역이 없으면 거절한다.
3. Native step 경계에 넘길 때 적용 step을 확정한다. 실행 시 소유량/Avatar/지면 revision 등을 다시 검사한다. 미리보기 성공이 배치 성공을 의미하지 않는다.
4. 같은 거래에서 인벤토리 1개 차감과 source 등록을 확정한다. 실패하면 둘 다 반영하지 않는다. 다중 요청이 같은 마지막 1개를 쓰지 못하도록 pending 수량 예약 또는 단일 commit 경로를 둔다.
5. `(SessionEpoch, PlayerKey, RequestSequence)`로 중복 요청을 제거한다. 동일 요청 재전송은 기존 결과를 반환하며 다시 차감/생성하지 않는다. 검증 실패와 일반적인 재고 부족을 구분한다.
6. `ProvisionResult`와 인벤토리 revision, source 상태를 전송한다. client 표시 갱신 순서가 뒤집혀도 revision으로 수렴시킨다.

리스폰은 인벤토리를 유지하되 이전 Avatar의 pending 요청을 취소한다. 접속 종료 후 이미 놓인 먹이는 만료까지 유지하고 provenance는 Actor pointer 없이 보존한다. 첫 버전의 `PlayerKey`는 세션 내 권위 ID로 충분하지만 영구 계정 관계 ID라고 부르지 않는다.

## 6. 먹이 자원·섭식 계약

### 6.1 자연 풀과의 경계

플레이어 음식은 자연 FoodGrid 셀의 재생 가능한 풀과 다른 **유한 공급 source**다. 같은 Native 실행 경계에서 관리하되 잔량/공급/섭취/폐기 하위 장부를 구분한다. 먹이를 놓았다고 자연 셀 Food/Capacity/Regenerated를 올리지 않는다. 따라서 기존 자연 FoodAmount를 읽는 이주/출생 판단에 새 공급량이 자동으로 들어가지는 않는다. 그 소비자 확장은 후속 계약이다.

```text
inventory_after = inventory_before + grants - placed_items
provision_after = provision_before + placed_food - ingested_food - expired_food - explicitly_removed_food
natural_after   = natural_before - natural_intake + natural_growth - natural_environment_loss
```

물리량의 단위를 맞춘 전체 감사에서는 `inventory_items × food_units_per_item`을 사용한다. 에너지에는 소화율·대사·clamp/비축 이동이 있으므로 먹이 질량 수지를 Energy 증가량과 단순 동일시하지 않는다.

`CommitSpatialFood`는 현재 자연 자원용으로 유지하고 같은 Ecology 서비스에 versioned 제공 먹이 receipt를 추가한다. 전체 step의 두 장부를 사전 검증한 뒤 게시하며 실패를 무시하지 않는다. 플레이어 공급을 `Regenerated`나 `RoundingAdjustment`에 넣어 기존 검사를 통과시키지 않는다.

### 6.2 C4 연결과 한 번의 영양 반영

- 소비 조건: 살아 있는 대상, 지원 식성, 유효 source/epoch, 실제 이동 후 위치의 접촉 반경/높이/LOS, 허용된 섭식 실행 상태, 유효한 접근 의도, source 잔량/기한.
- 제공 먹이는 개체당 한 source만 대상으로 삼는다. 첫 버전은 같은 source에 도달한 여러 개체의 유효 수요를 비례 배분하고, 임의 Actor tick의 선착순 차감을 피한다. 접근 슬롯 예약은 기본 필수가 아니며 실제 혼잡 검증 후 추가한다.
- 개체가 선택한 출처는 `Natural` 또는 `Provision(SourceId)`다. 한 step의 기존 섭취 예산을 두 출처에 각각 중복 적용하지 않는다. 제공 먹이가 소진되면 그 step에는 다른 풀로 몰래 보충하지 않고 다음 선택 기회에 갱신한다.
- source별 승인량을 계산하고 기존 C4에서 Energy/Reserve/대사를 한 번만 갱신한다. 물리 source 수지와 영양 환산량은 별도로 남긴다. 자연 서식지의 NutritionMultiplier가 제공 먹이에 우연히 적용되지 않게 한다.
- 초기 food 영양 환산은 기존 `FoodEnergyPerUnit`을 재사용하는 방향을 우선 검토한다. 실제 개당 공급량과 섭취율은 RL 담당과 고정한다. EatWant/배고픔 제한·포만 규칙을 우회하지 않는다.
- 죽음·포획·이주·취소와 섭취의 같은-step 순서는 반드시 명세/fixture에 포함한다. C4 진입 전 확정된 사망이나 포획 대상은 제공 먹이 자격에서 제외하는 제안을 우선하며, 기존 자연 경로의 순서를 동시에 바꾸지 않는다.

현재 `FoodEaten`/`LastIntake`는 자연 풀 소비 근거로 쓰인다. 첫 구현은 그 의미를 보존하고 `ProvisionIntake`와 `TotalIntake`를 추가하는 방향을 권장한다. BrowseEvidence/풀 훼손은 Natural만, 제공 먹이 이벤트는 Provision만, 먹는 표현은 Total을 읽게 한다. 모든 읽기 지점을 감사하고 기존 자연 장부가 Total을 읽지 않도록 검증한다.

### 6.3 상태·시간·수명

source 상태는 `Available → Depleted / Expired / Removed`다. CreatedStep부터 존재하고 `step >= ExpireStep`이면 해당 step 소비 전에 만료한다. 부분 섭취는 Available 상태의 잔량 감소다. 종료 source는 짧은 tombstone/receipt 유지 후 정리한다.

시간은 권위 simulation step, 길이는 계약상 cm 또는 명시된 native units다. 변환은 1unit=200cm, step=8/60초를 사용한다. 실시간 Render DeltaTime으로 먹이 감소를 계산하지 않는다. 만료 시간은 정수 step으로 양쪽에 같은 값을 전달한다.

## 7. Python 담당자에게 전달할 데이터 경계

다음은 **세계/센서 인터페이스 후보**이며 현재 R7e 벡터에 자동 추가되는 관측 슬롯이 아니다. 최종 차원·정규화·mask는 RL 담당이 결정한다.

| 데이터 | 최소 내용 | 사용 범위 |
|---|---|---|
| `ProvisionWorldConfig` | schema/world-rule version, 단위·step, 개당 먹이량, 반경·높이, 섭취 규칙, TTL, 시야 조건, 예산, source 선택/경합 순서 | C++/Python 공통 world 규칙 |
| `ProvisionSourceState` | WorldEpoch, SourceId, RegionId, Position, FoodType, Remaining, Created/ExpireStep, ProviderKey, Revision | 서버 정답/복원. 그대로 observation에 넣지 않음 |
| `FoodEvidence` | valid, 감지한 source의 상대 위치, 현재 보임/마지막 증거 시각, 감지된 존재/수량 범주, 공급 목격 여부·제공자 인식 여부 | 개체에게 허용된 센서 데이터 |
| `ProvisionIntent` | AgentStableId, SourceId, generation, 선택 시각, 실행 의도, 취소 사유 | policy/adapter→Native 실행. 새 action 번호를 여기서 결정하지 않음 |
| `ProvisionOutcome` | EventId, step, source/agent ID, actual food consumed, source before/after, 영양 기여, reject reason | 학습 결과·진단/평가. reward 계수는 포함하지 않음 |
| `StepTrace` | 초기 상태/외부 입력, request/executed/override, 실제 순서·수지·종료 원인 | differential/재현 검증 |

감지 규칙은 다음을 권장한다.

- 플레이어 가방 안 수량은 사슴에게 보이지 않는다. 먹이가 지면에 놓였거나 후속 버전에서 손에 보일 때만 별도 증거가 생긴다.
- 첫 버전은 기존 FOV/거리·static LOS/식생 차폐 원칙을 적용하고 새 후각을 도입하지 않는다. Unreal 지면/차폐 결과를 Python에 어떻게 재현하는지도 계약한다.
- 먹이를 봤다는 사실과 놓는 사람을 목격했다는 사실은 분리한다. `ProviderKey`가 서버 장부에 있어도 못 본 사슴은 제공자를 Unknown으로 받는다.
- 첫 버전 센서는 현재 보이는 source만 제공하는 구성이 가장 작다. 기억을 추가하면 마지막 관측 좌표/량과 TTL을 유지하며 숨은 잔량을 실시간 갱신하지 않는다.
- source 후보 상한 K, 정렬 기준(거리·동률 ID 등), 감지 주기, 결측 mask를 고정한다. 0 좌표/0 수량을 Unknown 표기로 겸용하지 않는다.
- 실제 SourceId/PlayerKey는 routing/로그용이다. 네트워크 입력에 숫자 ID를 그대로 넣지 않는다. 사람 구분 학습이 필요하면 episode-local 매핑·재식별/Unknown 규칙을 RL 담당이 별도로 정한다.

**중요한 학습 경계:** 기존 5행동에서 Graze를 고르는 것만으로 '어느 제공 먹이에 접근할지'가 자동으로 표현되지는 않는다. 기존 조향에 후보를 연결할지, 목표 선택을 새 정책에 둘지, 별도 상호작용 요청을 둘지 RL 담당과 먼저 확정해야 한다. 기존 FREEZE/예약 슬롯이나 다른 관측 슬롯을 임의 재사용하지 않는다. 기존 모델로 보이는 접근을 새 먹이 학습 성공으로 설명하지 않는다.

## 8. 실제 전달 패키지와 준비 상태

첫 전달물은 UObject 코드만이 아니라 다음 묶음이어야 한다. 아래 경로는 **향후 산출물 제안**이며 이번 계획 작성에서 생성한 파일이 아니다.

| 전달물 | 내용 / 완료 조건 |
|---|---|
| `player_food_contract_v1.md` | 생성·만료·감지·경합·섭취·거절·reset 순서, 필드 단위/결측, 지원 범위 |
| `player_food_world_v1.json` | 합의된 상수와 version/hash. DataAsset→검증 가능한 export. 양쪽 수동 복사 방지 |
| 엔진 비의존 C++ 규칙 leaf + CLI 평가기 | 동일 입력으로 source/수량/경합 결과 실행. Actor/네트워크 없이 재현 |
| `player_food_cases_v1.jsonl` | 정상·부분 섭취·동시 경합·만료 경계·중복·Unknown·차폐·포만 사례와 기대값 |
| 초기 상태 + 입력 사건 replay | seed 외에 제공자의 pose/행동/배치 성공, 지면·LOS 입력, 적용 step 포함 |
| Python mirror | RL 담당이 기존 env_v2 계열의 선택한 기준에 연결. 새 모듈 이름/패키지는 담당자와 결정 |
| `manifest.json` | commit+dirty diff hash, rules/config/fixture hash, 모델·encoder·mask, map/geometry, seed·step, 지원/제외 기능 |
| 비교 보고서 + 작은 재현 fixture | C++/Python 첫 불일치 위치, 수지/영양/감각 결과, 실패 장면 재생 방법 |

준비 상태를 구분한다.

- `spec_ready`: 인터페이스와 보류 사항을 문서화한 상태. 이번 산출물은 **설계 제안**에 해당한다.
- `world_rules_ready`: 실행 가능한 C++/Python slice와 동일 입력 비교를 통과. 이때도 기존 모델이 새 먹이를 선택한다는 뜻은 아니다.
- `policy_training_ready`: RL 담당이 관측/행동 표현·보상·reset/termination·세계 기준을 고정하고 학습 환경 검사를 통과한 상태.
- `runtime_ready`: Native로 돌아온 후보 정책, 저장/멀티/실제 지형까지 해당 범위의 검증 완료.

기존 [부분 snapshot 예제](../RL_Policy/Examples/README.md)는 training_ready=false다. 이를 새 먹이 학습 dataset이나 전체 복원 세계로 재명명하지 않는다. 세계 trace는 주로 parity 자료이며 PPO 학습 환경 자체를 대신하지 않는다.

## 9. step 순서와 Python parity

다음 순서를 새로운 opt-in 규칙으로 양쪽에 고정한다. 기존 Native 세부 단계와 맞추는 작업은 RL/실행 담당 공동 변경이다.

1. step 경계에서 검증된 플레이어 사건 반영: source 생성/명시 제거, 현재 step 만료 처리.
2. 개체가 알 수 있는 감각 snapshot 생성. policy의 기존 판단 주기를 임의 단축하지 않음.
3. RL/승인된 adapter가 의도 선택. request, 실제 executed, 생존/여행 등 override를 분리.
4. 기존 Native 이동·충돌/안전 경로 실행. source 위치로 순간이동/두 번째 이동 writer 없음.
5. 이동 후 접촉과 source revision을 재검증하고 자연/제공 중 선택 출처에 대한 섭취 수요 생성.
6. C4에서 source별 배분·잔량 commit, 한 번의 Energy/Reserve/대사 처리. C5/C6와 충돌 순서 fixture 검증.
7. 출처별 receipt와 표현 snapshot 게시. 완료되지 않은 제안/예약에 영양 결과를 발행하지 않음.

정수 ID·상태·선택/거절 결과는 정확 일치, double leaf 계산은 `abs_error <= 1e-8 + 1e-9 * max(abs(a), abs(b))`를 초기 비교 기준으로 제안한다. 이 값은 테스트에서 검토·고정하며 모델 추론 tolerance와 혼용하지 않는다. JSON에는 int64를 문자열로 저장한다.

같은 seed만으로 UE trace/NumPy/난수 소비 순서가 같아지지는 않는다. 1차는 동일 외부 사건·지면/LOS 입력을 재생하는 작은 slice를 비교하고, 2차로 fixture geometry를 맞춘다. 전체 life/다지역 궤적 parity 완료로 확대하지 않는다. Python vector 환경도 UE 시간 순서/개체 경합 규칙을 재현해야 하며 새 환경을 Gymnasium 단일 Env로 전면 교체할 필요는 없다.

## 10. 저장·복제·규모

- owner-only inventory revision과 주변 클라이언트용 source DTO를 분리한다. source의 지속 상태는 replicated property/배열로 제공하며 Multicast 연출만으로 복원하지 않는다. 저장소 내부 Subsystem은 transport가 아니다.
- 1차 규모는 지역당/플레이어당 source 상한을 설정하고 제한된 공간 후보 질의를 수행한다. 전체 개체×전체 source 전수 매 tick 탐색을 기본으로 잡지 않는다.
- World Partition 표현 unload는 논리 삭제가 아니다. 지역의 실제 simulation teardown은 `Removed(reason)`로 정산하고 잔량을 버리거나 환급하는 규칙을 명시한다. 첫 버전은 자동 환급하지 않는다.
- 기존 checkpoint에 live source/잔량/만료 기준/sequence와 필요한 공급 내역을 함께 싣는 판을 추가해야 한다. inventory와 world를 다른 시점으로 저장해 복제 공급이 생기지 않게 한다.
- joint 저장/복원을 지원하기 전에는 feature ON 세계를 기존 저장 포맷으로 성공한 것처럼 저장하지 않는다. 개발 단계에서는 명시적으로 거절한다. episode reset은 initial grant부터 다시 구성하며 옛 receipt/ID가 새 episode에 적용되지 않게 epoch를 바꾼다.
- MMO 계정 inventory, reconnect 영속성, cross-server 이주는 이 단계의 완료 조건이 아니다. 세션/fixture 지원 범위를 manifest에 적는다.

## 11. 구현 단계와 역할별 완료 조건

| 단계 | 게임/통합 담당 | RL 담당 | 완료 조건 |
|---|---|---|---|
| P0 계약 고정 | Source writer/consumer 감사, ground placement와 자원 소유 구조, 설정 후보 | 실제 학습 baseline·대상 행동 표현 선택 | 세계 규칙·단위·선택/섭취 접점 합의, 기존 모델/OFF baseline 확보 |
| P1 소유·제공 | 1종 수량/HUD, 서버 놓기 거래, source 수명/표현 | scripted player 입력 형태 검토 | 수량 차감=source 공급, 실패/재전송/리스폰 이중 공급0, 멀티 두 사용자 검증 |
| P2 감지·섭취 실행 | sensor evidence, source별 C4 연결, M4 하위 receipt, 결과 trace | 새 행동 interface와 mirror 연결점 검토 | fixture로 실제 접촉/부분 섭취/경합/Unknown/수지 검증, 강제 접근 기본OFF |
| P3 Python 인계 | config/leaf/fixtures/replay/manifest, UE 실제 지면 사례 | Python mirror/differential, 관측·mask·reward 설계 | 작은 slice의 동일 입력 결과 비교 + training-ready 판정 |
| P4 학습/후보 통합 | 선택된 후보 adapter·기존 Native 로더 연결, 표현 검증 | 학습/holdout·export/Golden | OFF 회귀 없음, 후보 schema 거절/fallback, 제공량·생존·안전 행동 평가 |
| P5 실제 게임 수락 | 저장 왕복, Dedicated+Client, owned fixture의 지면/streaming/cook | 실제 플레이 실패 사례 재평가 | 제공→접근/거절→실제 섭취가 같은 원인/결과로 재현 |

P1만 끝내면 '소유·놓기 기능'은 있지만 학습 가능한 전체 먹이 상호작용은 아직 아니다. RL 담당에게 먼저 줄 수 있는 최소 유효 묶음은 **P2 실행 계약 + P3 mirror용 자료**다. P4 학습은 RL 담당 업무다.

P2의 실행 검증에서는 test-only scripted intent를 사용할 수 있다. 이때도 실제 Native 이동/접촉/C4 소비를 거치며, production 강제 유인 규칙이나 학습 결과로 표시하지 않는다. 기존 맵은 기본OFF, owned 테스트 fixture에서만 ON으로 시작한다. 기본 R7e/V1/내장 R1b, SocialControl=0은 변경하지 않는다.

## 12. 검증 시나리오

| 사례 | 필수 확인 |
|---|---|
| 마지막 먹이 1개로 동시 요청/재전송 | 인벤토리 음수0, source 1개, 중복 receipt0 |
| 벽 뒤/허공/높이 불일치/지역 없음 | 요청 거절, 차감0, client preview와 authority 결과 구분 |
| 2마리가 같은 작은 먹이를 섭취 | 합계 소비≤잔량, 선언한 비례 배분, 순회 순서에 따른 승자 뒤바뀜 방지 |
| 자연 풀 위에 놓인 먹이 | source 선택별 예산1회, 자연 풀 차감/뜯김 흔적과 제공 먹이 차감 분리 |
| 배부름/뛰기/도주/죽음/여행 | 기존 자격·섭취 예산 준수, 금지 상태의 원격 섭취0 |
| 부분 섭취 후 만료/제거 | 남은 양만 폐기, consumed+remaining+wasted 보존 |
| 먹이는 보이나 제공자를 못 봄 | 먹이 감지 가능, 제공자 Unknown 유지 |
| 화면 밖/클라이언트 재접근/늦은 접속 | 잔량 수렴, client에서 논리 섭취/생성0 |
| 리스폰/연결 종료/저장 복원/reset | 공급 복제0, 옛 Avatar·epoch 요청 적용0, 수명 규칙 일치 |
| feature OFF | 기존 정책 Golden·자연 먹이 receipt·행동 baseline 유지 |

학습 평가는 별도다. 협조적 제공자만 학습한 모델이 빈손 사람에게도 무조건 접근하는지, 제공자가 물러나거나 이동할 때 어떤지, 다른 사람의 공격과 먹이가 함께 있을 때의 생존, 낯선 위치/seed, 경쟁 개체, 먹이 없는 환경의 기존 섭식 유지 등을 holdout에 둔다. 보상식은 RL 담당이 정하되 `placed`/`requested`와 `consumed`를 섞지 않도록 원시 결과를 제공한다. 이번 단계만으로 개인 호감도 학습 완료를 주장하지 않는다.

## 13. 개발 전 합의할 항목과 권장 기본 방향

| 항목 | 권장 방향 | 결정 주체 |
|---|---|---|
| 제공 방식 | 첫 버전 지면 놓기, 손먹이/투척 후속 | 게임 기획/구현 |
| 먹이 공급원 | 개발 지급은 명시 InitialGrant, 채집/경제 후속 | 게임 기획 |
| 숫자 설정 | 개당량·섭취율·TTL·거리·상한을 export 가능한 한 설정으로 고정 | 게임 + RL |
| 학습 기준 | 실제 사용하는 R7/R8 등 Python baseline을 manifest로 지정 | RL |
| 행동 표현 | 제공 source 선택을 현재 repertoire/새 요청 중 어디에 표현할지 명시 | RL + Native 실행 담당 |
| 관측·보상 | 감지 근거/Unknown을 바탕으로 새 version 설계, 기존 슬롯 의미 보존 | RL |
| 저장 | 게임 수락 전 joint snapshot; 미지원 개발판은 ON 저장 명시 거절 | 게임/저장 담당 |

다음 구현자는 [프로젝트 공통 맥락](../PROJECT_CONTEXT.md), [RL 담당 안내](../RL_Policy/ECOSYSTEM_RL_AGENT_GUIDE_20261024.md), [자연 풀 패치 명세](FOOD_PATCH_GRAZING_IMPLEMENTATION_SPEC_20261108.md)를 함께 읽는다. 이 문서는 단계 계획이므로 API 이름이나 미생성 산출물을 구현 완료로 인용하지 않는다.

이번 작성에서 수행한 검증은 현재 Source/계약/공식 자료 조사와 문서 링크·diff 확인이다. 런타임 코드·에셋·Python 학습 코드는 변경하지 않았고 UBT/PIE/학습을 실행하지 않았다. 후속 C++/빌드 입력 변경 시 루트 AGENTS.md의 직접 UBT 실행 및 중복 빌드 금지 규칙을 따른다.
