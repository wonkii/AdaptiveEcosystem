# 손에 든 먹이·키 급여·지역 사슴 평판 개발 명세

작성: 2026-11-14 / 상태: **구현 전 활성 명세**. 사용자 후속 요청에 따라 지면 배치형 계획을 대체한다. 실행 계획은 [구현·RL 인계 계획](PLAYER_HELD_FOOD_AFFINITY_PLAN_20261114.md)이다.

**최신 결정: 지역×종마다 모든 플레이어에 대한 공통 Reputation을 하나 저장한다.** 개인 사슴별·플레이어별 Affinity안과 이전 v2의 PlayerKey별 평판은 대체됐다. 한 플레이어의 급여/인식된 공격이 같은 지역·종의 모든 플레이어에 대한 판단에 영향을 준다. 같은 지역에서 처음 보는 사슴도 현재 평판을 읽는다. 규칙판은 `held_food_regional_species_human_reputation_v3`이며 아래 내용은 이전 개인 관계안을 수정한 현재 계약이다. 실제 전달 순서는 [Python 담당자 단계별 인수인계](../RL_Policy/PLAYER_INTERACTION_RL_HANDOFF_20261114.md)를 따른다.

감사 기준은 `jwj-1024`, HEAD `ef44df15` + 기존 dirty 작업 트리다. 다음 구현 때 실제 HEAD/Source/추가 지침을 다시 확인한다. 현재 코드·에셋·모델을 수정하거나 검증 완료했다는 기록이 아니다.

## 1. 이번 제품 목표

**먹이를 손에 든 플레이어를 사슴이 지각하고, 실제 급여가 성립하면 그 지역 사슴들의 전체 플레이어에 대한 공통 평판이 올라간다. 공격자를 인식한 실제 무기 피해는 피해 개체 소속 지역·종의 평판을 내린다. 행동 당사자 또는 아무 행동도 하지 않은 다른 플레이어를 처음 보는 같은 지역의 사슴도 공통 평판을 읽고 친숙하게 접근하는 행동을 학습한다.**

기능을 네 부분으로 분리한다.

1. **소유·장착:** 가방의 수량과 손에 든 상태, 무기/먹이 전환.
2. **감각:** 사슴이 실제로 볼 수 있는 사람·손의 먹이·현재 행동 정보.
3. **세계 규칙:** 유효 급여/피해의 결과, 지역×종별 인간 공통 평판 갱신. 개별 위험 기억과 분리.
4. **정책:** 이 관측과 관계를 근거로 행동을 선택. 관측 schema·action·reward·가중치는 RL 담당 소유.

평판은 게임의 사건 기반 공통 상태다. 평판을 갱신하는 것과 PPO를 온라인 재학습하는 것은 별개다. 숫자가 바뀌는 것만으로 기존 R7e의 행동이 달라진다고 설명하지 않는다. '친숙하게 접근'은 위험하지 않은 상황에서의 제품 목표이며, 항상 접근하거나 모든 사슴이 동시에 모이도록 강제하지 않는다.

## 2. 이전 계획에서 변경되는 범위

| 이전 지면형 | 이번 활성 계약 |
|---|---|
| 지면 source 생성/잔량/TTL | 플레이어가 소유하며 장착한 먹이 상태와 짧은 급여 요청 |
| 놓은 먹이 좌표로 접근 | 현재 지각한 플레이어/급여 위치를 근거로 접근·거리 유지 |
| 같은 source에 다수 사슴의 섭취 경합 | 1회 요청은 특정 사슴 1마리, 동일 사슴/플레이어 중복 거래 방지 |
| 호감도는 후속 | 지역×종의 전체 플레이어 대상 평판과 급여/피해 연결이 이번 필수 범위 |
| 플레이어별 관계 | 현재 지각한 플레이어 후보 모두에게 자기 지역·종의 같은 평판 제공. PlayerKey는 거래/감사에 유지 |
| Python에 먹이 규칙만 인계 | 장착/감각/급여/피해 인과·지역 평판/시간 순서까지 인계 |

지면 놓기/떨어진 먹이/투척, 범용 인벤토리, 상점·채집, 개체 간 소문 전파 시뮬레이션, 복잡한 신뢰/성격 체계, 계정 영속성·저장 왕복은 이번 최소 판에서 제외한다. 지역 공통 평판은 포함한다. 기존 개체별 Fear/UnknownFear 등 생존 기억은 유지한다.

## 3. 실제 Source 감사

| 파일 | 확인한 사실 / 변경 원칙 |
|---|---|
| [EcoEquipmentComponent](../../Source/AdaptiveEcosystem/Player/EcoEquipmentComponent.cpp) / [header](../../Source/AdaptiveEcosystem/Player/EcoEquipmentComponent.h) | 현재 무기 전용 Equip/Unequip, socket attach·복제. Equip은 양수 BaseDamage 등을 요구하므로 먹이를 가짜 무기로 넣지 않음 |
| [EcoPlayerState.h](../../Source/AdaptiveEcosystem/Player/EcoPlayerState.h) | PlayerState의 ASC/체력/AvatarGeneration. inventory/세션 PlayerKey와 Pawn 수명을 분리 |
| [EcoAbilityInputConfig.h](../../Source/AdaptiveEcosystem/Player/EcoAbilityInputConfig.h) | 기존 Enhanced Input→GAS binding 사용. 키는 기존 IMC 충돌 확인 후 배치 |
| [EcoWeaponFireComponent.cpp](../../Source/AdaptiveEcosystem/Player/EcoWeaponFireComponent.cpp) | 서버 조준/무기/생존 검증 후 QueueWeaponContact. 발사 성공 자체는 피해 확정이 아님 |
| [EcoCreatureCombat.cpp](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureCombat.cpp) | 총기와 combo가 최종 ProcessComboDamage→ApplyPlayerDamage를 거쳐 Before−RemainingHP로 실제 피해를 계산 |
| [EcoCreaturePlayerOutcomes.cpp](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreaturePlayerOutcomes.cpp) | 직접 strike도 ApplyPlayerDamage에 연결. Native 피해에서 감각 기반 Known과 Unknown 기억을 구분 |
| [EcoCombatTypes.h](../../Source/AdaptiveEcosystem/Core/EcoCombatTypes.h) / [Lifecycle](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureLifecycle.cpp) | Queued/Committed/DeathCommitted 등이 같은 공격의 다른 상태로 발행됨. Outcome.StepId는 현재 공격 stage이며 NativeStep으로 사용하면 안 됨 |
| [EcoLifeHistoryV3.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoLifeHistoryV3.h) | FMemory는 개체당 Familiarity/Fear 등. 지역 Reputation으로 기존 위험 기억을 덮어쓰지 않음 |
| [IntegratedProcessors](../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp) | ObservePlayers는 가장 가까운 보이는 사람의 좌표/속도 등을 읽어 기존 기억에 반영. 모든 사람에게 구분된 ID/held food 관측이 이미 있는 것은 아님 |
| [EcoWorldV3.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h) / [EcoRepertoire.h](../../Source/AdaptiveEcosystem/AI/Policy/EcoRepertoire.h) | 이전 step에서 준비한 요청을 실행, Native 단일 이동과 C4 에너지/비축/섭식, 1unit=200cm·step=8/60초 |
| [EcologySpatialFood.cpp](../../Source/AdaptiveEcosystem/Ecology/EcologySpatialFood.cpp) | 자연 FoodEaten과 자연 자원 수지 결합. 플레이어 급여를 자연 풀 소비로 기록하지 않음 |
| [EcoMassFragments.h](../../Source/AdaptiveEcosystem/Mass/EcoMassFragments.h), [EcoPopulationPressure.cpp](../../Source/AdaptiveEcosystem/Ecology/EcoPopulationPressure.cpp) | Identity.SpeciesId와 Region.CurrentRegionId, 기존 이주 receipt를 이용. 플레이어가 서 있는 region이나 렌더 위치를 평판 지역으로 대신 사용하지 않음 |

이번 재확인에서 `ApplyPlayerDamage`는 피해자의 `Known` 외에 주변 목격을 포함하는 `Recognized`와 `Context->KnownHumanDanger.Record`를 이미 사용한다. 신규 Reputation은 기존 위협 상태를 대체하지 않는다. 기존 `KnownHumanDanger`를 signed 평판으로 재해석하거나 두 경로에서 Score를 중복 차감하지 않는다. 이번 평판의 최초 채택 기준은 피해자의 `Known`이다. 목격자만 인식한 사건까지 포함하려면 별도 규칙판으로 합의하고 Python 사례를 함께 변경한다. H0에서 기존 위협 소비자/반사와 신규 정책 adapter 우선순위를 확인한다.

현재 작업 트리에서 Native/무기/맵/문서에 기존 수정이 있다. 이번 코드와 겹친다는 이유로 reset/checkout/전체 덮어쓰기를 하지 않는다. Blueprint 전체 입력/장비 배치 감사는 구현 때 수행한다.

## 4. 플레이 흐름과 UI

1. 세션 시작 시 테스트 먹이 10개를 서버에서 한 번 지급한다.
2. 숫자 **1번 키**로 손 상태를 Food로 전환한다. 오른손 Socket에 기본 Sphere가 보이며 HUD에 남은 개수가 표시된다. 이미 Food 상태에서 1번을 다시 누르면 장착 상태를 유지한다. 수납/무기 전환은 별도 기존 장비 입력을 사용한다.
3. 사슴이 사람과 손의 먹이를 감지할 수 있다. 보기만 해서는 호감도/먹이량이 바뀌지 않는다.
4. 플레이어는 가까운 사슴을 겨냥한다. UI는 서버가 유효한 대상으로 인정한 사슴에만 급여 가능/불가 사유를 보여 준다.
5. 상호작용 키를 누르면 한입 급여 요청. 가까움·살아 있음·먹이 장착·시야·섭식 가능 상태 등을 서버가 재검증한다.
6. 실제 Native 섭취가 성공하면 먹이 1개 차감, 받은 사슴 영양만 증가, **그 지역·종→전체 플레이어**의 평판 증가. 다른 사슴의 영양은 늘지 않는다.
7. 총 등 무기로 인식된 실제 피해를 주면 피해 사슴 소속 지역·종의 인간 공통 평판 감소. A가 급여하고 B가 공격하면 같은 지역·종의 한 점수에 순서대로 반영한다.
8. 같은 지역의 다른 사슴은 플레이어를 처음 시각 식별해도 그 지역 평판을 읽는다. 다른 지역 사슴이나 늑대의 평판은 바뀌지 않는다.

장착은 `IA_EquipFood`를 숫자열 1번 키에 연결한다. 키 충돌이 있으면 owned fixture의 Mapping Context 우선순위/충돌 binding을 명시적으로 조정하고 실제 동작을 검증한다. 급여는 별도 `IA_InteractFeed`로 정의하며 물리 키는 구현 전 기존 IMC 감사 후 확정한다. 기존 F 직접 공격을 덮어쓰지 않는다. 1번 장착과 급여는 별개 입력이다. 키를 오래 누른 Triggered 이벤트가 여러 거래를 생성하지 않도록 Started 1회/명시 cooldown을 사용한다.

최소 판에서는 별도의 '수락' PPO action을 필수로 추가하지 않는다. 사슴이 현재 가까이 있고 안전한 섭식 상태이면 급여가 가능하며, 떠나거나 도주/수면 중이면 불가능하다. 학습은 우선 그 상태에 접근·머무를지 피할지를 선택한다. 명시적인 수락/거절 action이 필요하면 RL 담당과 새 계약을 만든다.

## 5. 손 상태와 물리적 급여 위치

### 5.1 소유·장착 분리

```text
Inventory(PlayerKey): FoodTypeId, WholeItemCount, InventoryRevision
HeldState(PlayerKey): AvatarGeneration, HandMode(None | Weapon | Food),
                     FoodTypeId?, HasEdibleItem, HandRevision, OfferGeneration
```

수량은 PlayerState 소유 서버 component, 손 상태는 현재 Pawn/장비 계층의 권위 상태다. 정확한 수량은 소유자에게만 복제하고, 주변에는 장착 종류/먹이가 있는지/필요한 연출만 전송한다. 손에 들기만 할 때 수량을 차감하지 않는다. 수량이0이면 HasEdibleItem=false이며 먹이 mesh/급여를 비활성화한다.

무기와 먹이는 **동시에 활성 손 도구가 되지 않는다.** 기존 무기 설정/상태를 보존한 채 입력·조준·공격 경로를 suspend하고 외형을 수납하거나, 상태 보존이 검증된 장비 전환을 구현한다. 현재 Unequip은 무기 Actor를 Destroy하므로 무작정 호출해 무기 상태가 사라지게 하지 않는다. 먹이 mode에서 총/근접 공격의 서버 실행도 거절한다. 외형만 숨기고 무기가 계속 발사되는 상태는 실패다.

상태 전환은 성공 여부를 확인한 뒤 commit한다. 전환 중 실패하면 이전 유효 상태 유지. 먹이 수납/무기 교체/사망/Avatar 교체 시 OfferGeneration 증가와 pending 급여 취소. 단순 count 변화는 hand identity 변경이 아니며 InventoryRevision만 올린다.

### 5.2 기본 Sphere와 오른손 Socket

표현은 엔진 기본 Sphere Static Mesh로 시작한다. 물리 시뮬레이션·충돌·overlap 기반 자동 급여는 사용하지 않는다. 크기·색·상대 transform은 설정으로 노출하고 실제 손에 맞춘다. 먹이 0개/수납/사망 시 숨기며, 접속한 다른 Client도 현재 장착 상태를 재구성한다.

`FoodGripSocket` 설정으로 오른손 부착 지점을 지정한다. 현재 무기는 `socket_Gun`과 `hand_r` authoring 경로를 사용한다. 먹이는 실제 Skeleton을 확인한 뒤 오른손의 전용 `socket_Food_R`을 우선 사용한다. 이는 신규 제안 이름이며 현재 존재하는 socket으로 간주하지 않는다. 공유 Skeleton 전체를 변경하기보다 owned mesh의 Mesh Socket/owned asset을 우선한다. socket 누락은 명시 실패로 처리하고 원점 부착으로 성공을 위장하지 않는다.

### 5.3 headless 서버에서도 동일한 논리 위치

시각 mesh는 손 socket에 붙이되, 논리 급여 anchor는 서버 Pawn transform + 설정된 local offset으로 계산한다. 사슴의 접촉점도 Native body 위치/heading/체격과 검증된 local offset으로 계산한다. 현재 개체 mesh의 렌더 bone tick/화면 LOD가 먹기 판정을 결정하지 않는다.

해당 anchor와 손/입 표현의 허용 오차를 fixture에서 확인한다. 거리가 맞더라도 벽·높이·지형을 통과해 먹지 못하도록 anchor 간 접촉/LOS와 body 간 최대 거리도 확인한다. client가 보낸 손/입 좌표는 권위 값이 아니다. 처음에는 단순 정지 포즈/mesh로 충분하며 복잡한 먹기 AnimNotify를 논리 trigger로 쓰지 않는다.

## 6. 급여 거래

### 6.1 요청과 대상

```text
FeedRequest = {RequestSequence, WorldEpoch, AvatarGeneration, OfferGeneration,
               TargetAgentStableId, TargetGeneration, AimHint}
FeedResult = {RequestKey, AppliedNativeStep?, AuthoritySequence,
              ResultCode, ActualFood, InventoryRevision,
              ReputationKey, ReputationDelta, ReputationRevision}
```

대상 stable ID는 client의 선택 hint이며 서버가 직접 Entity/생존/region/거리/방향/LOS를 확인한다. TargetId 자체를 신뢰하지 않는다. 표현 relevance의 기존 identity 검증을 참고하되 **CanAttack를 곧 CanFeed로 사용하거나 무기 HitProxy 존재를 급여의 필수 조건으로 만들지 않는다.** 서버의 근접 상호작용 후보 질의를 별도로 제공하거나 일반화한다.

1. 소유 connection/현재 Avatar, hand mode Food·먹이 존재, target identity, queue budget, cooldown 검증.
2. 가까운 후보로 선정된 대상과 현재 body/anchor 기하를 검사. 기한이 짧은 요청을 생성한다. 이때 수량/호감도 변화0.
3. 다음 적절한 Native 경계에서 같은 상태를 재검증하고 C4까지 유효한 요청을 전달한다. 멀리 있는 사슴을 키 하나로 강제 호출하거나 이동시키지 않는다.
4. C4에서 실제 이동 후 접촉·LOS, 현재 대상의 섭식 실행 상태, 포획/사망/여행/도주 충돌, 기존 EatWant를 재검증한다.
5. 자격을 만족하면 정해진 한입량을 소비한다. inventory debit 예약·섭취·영양·급여 receipt·지역 평판 결과를 같은 권위 거래로 확정하며 완료 결과는 commit 뒤에만 게시한다. EventRegionId/SpeciesId는 이때 받은 개체의 권위 소속으로 고정한다.

UObject 수량 변경은 Mass parallel 처리 중 직접 수행하지 않는다. GT에서 inventory revision/수량을 예약하여 plain-data token으로 넘기고, 지정된 실행 경계에서 원자적으로 확정한다. 서버 저장/관측은 pending 상태를 완료 거래로 노출하지 않는다. 실패 token은 반환하며 물리/관계 결과0이다.

### 6.2 최소 판은 한입 단위

먹이1개는 개발 설정의 작은 한입량이다. 기존 EatWant가 한입보다 작으면 `InsufficientAppetite`로 거절한다. 양의 영양 결과 없이 개수만 차감하거나, 포만 상태를 무시하고 Energy를 올리지 않는다. 부분 아이템/남은 음식/여러 프레임 씹기 시스템은 최소 판에서 제외한다.

유효 급여는 개체당 Native step당1회, 플레이어당 동시에 pending1개다. 동일 사슴에 여러 플레이어가 같은 step에 요청하면 서버 접수 AuthoritySequence가 가장 빠른 **유효 요청**만 commit한다. 탈락 요청은 차감/호감도0이고 임의 다음 target으로 이동하지 않는다. Source 정렬과 성공 순서를 replay에 포함한다.

성공한 사슴은 그 step 자연 풀 수요에서 제외한다. 실패하면 기존 자연 섭식 경로를 유지한다. 이 출처 선택은 C4 배분 전에 결정한다. 단일 energy/reserve 계산에 들어가는 nutrition은 기존 자연식 또는 제공 한입식 중 하나다.

### 6.3 수지와 기록

```text
ItemsEnd = ItemsStart + InitialGrants - SuccessfulFeeds
PlayerFoodSupplied = SuccessfulFeeds * FoodUnitsPerBite
sum(HandFeedIntake) = PlayerFoodSupplied
NaturalEnd = NaturalStart - NaturalIntake + NaturalGrowth - NaturalLoss
```

FoodEaten/LastIntake의 기존 자연 소비 의미를 보존하고 HandFeedIntake/TotalIntake를 분리한다. 자연 뜯김 흔적은 Natural만, 먹는 표현은 Total을 읽는다. 자연 FoodAmount/Capacity/Regenerated에 제공량을 합치지 않는다. 기존 Ecology 서비스에 hand-feed receipt/감사를 추가하며 Subsystem에 RPC를 만들지 않는다.

## 7. 지역×종별 인간 공통 평판 규칙

### 7.1 저장과 조회

```text
ReputationKey = (WorldEpoch, RegionId, SpeciesId)
Reputation = {Score, LastPositiveSequence, LastRecognizedDamageTime,
              LastEventSequence, Revision}
```

PlayerKey는 서버가 발급한 세션 안정 ID이며 Pawn/Actor pointer/현재 검색 순서가 아니다. PlayerState를 통한 같은 플레이어 리스폰에서는 유지한다. 기존 권위 Ecology 서비스 안에 희소 평판 테이블을 소유하며 새로운 복제 Subsystem을 만들지 않는다. 행이 없는 유효 key는 중립0이다. index/표현 재생성/행 정렬로 key를 바꾸지 않는다.

각 지역·종당 평판 행은 하나다. 플레이어 접속/이탈/리스폰으로 행을 생성·삭제하지 않는다. PlayerKey는 inventory·요청 소유권·사건 원인·후보 handle에만 사용하고 평판 key에는 넣지 않는다. 기존 지원 지역·종 수에 맞춰 테이블을 구성하며 유효하지 않은 소속/미지원 종/서비스 실패를 중립0으로 위장하지 않는다. 개체마다 평판을 복사하거나 개체 수에 비례해 점수를 더하지 않는다.

**공유 의미:** 이 평판은 지역 공동 인식을 추상화한 게임 규칙이다. 사건을 보지 못한 사슴/새로 태어난 사슴도 자기 지역·종의 score를 조회할 수 있다. 실제 herd gossip/경보를 통해 모두 사건을 목격했다는 의미가 아니다. 공유되는 것은 Score뿐이며 공격 위치·현재 플레이어 좌표·개인 직접 기억을 만들지 않는다. 높은 평판 때문에 보이지 않는 플레이어를 추적하지 않는다.

**지역 결정:** 조회는 개체의 권위 CurrentRegionId와 SpeciesId를 사용한다. 이벤트는 commit 시 받은/피해 개체의 소속 지역·종을 고정하여 후속 이동/죽음에도 같은 key에 한 번 적용한다. 공격자가 다른 지역에 서 있어도 피해 지역 평판을 사용한다. 서로 다른 RegionId에 점수를 복사하지 않는다. 현재 활성 종은 사슴 SpeciesId 하나다. 외형이 비슷한 종을 자동 그룹화하지 않는다. 신규 종은 아래 종별 설정과 backend 검증을 통과한 뒤 활성화한다.

**이주·출생:** 이주는 기존 논리 소속 인계가 확정된 이후 목적지 평판을 읽는다. 이주 계획/TargetRegionId만으로 선반영하지 않는다. 여행 중 급여는 기존 자격에 따라 거절한다. 이동 전후 CurrentRegionId/Native context가 불일치하면 평판 조회를 무효화하고 진단하며 임의 가까운 region을 추정하지 않는다. 새 사슴은 개인 평판0을 생성하는 대신 출생 지역의 테이블을 조회한다. 사슴 사망/마지막 사슴 제거도 지역 평판을 지우지 않는다.

**수명·성능:** 렌더 unload/지역 context 재생성/플레이어 이탈은 테이블 삭제 조건이 아니다. world reset 때만 초기화한다. 현재 판의 영구 저장은 미지원이다. 평판 mutation은 권위 event당1회, 관측은 개체의 지역·종 key에 대한 lookup과 현재 지각 후보K에 대한 bounded 처리으로 한다. 평판 변경 때 전체 개체별 값을 쓰는 fan-out 작업은 하지 않는다. revision을 포함한 읽기 snapshot으로 같은 관측 경계의 일관성을 보장한다.

평판은 commit 후 다음 허용된 관측 snapshot부터 공유되고, 개체 행동은 기존 판단/실행 주기에 따라 달라진다. 모든 개체를 사건 발생 프레임에 즉시 interrupt하거나 모으지 않는다. 저장/외부 계약은 안정 RegionId/SpeciesId를 쓰고 hot-path lookup은 검증된 runtime index/handle로 압축할 수 있다.

### 7.2 종별 확장과 플레이어 공유

기존 종 설정의 확장 지점에 `SpeciesPlayerInteractionProfile`을 제안한다. 실제 자료형/파일명은 구현 H0에서 기존 종 설정과 중복되지 않게 확정한다.

| 설정 | 책임 |
|---|---|
| SpeciesId / bEnableHumanReputation | 종을 안정 ID로 구분, 미등록 종은 기능 미지원으로 처리 |
| bCanReceiveHandFood / AcceptedFoodTypeIds | 이 종의 급여 지원·식성, 사슴용 먹이를 모든 종에 자동 허용하지 않음 |
| InitialScore / ScoreMin / ScoreMax | 종별 중립값·범위. 이번 사슴 fixture는 0, -100, 100 |
| FeedReputationGain / DamageReputationScale / PositiveLockoutSteps | 종별 세계 규칙 계수, 같은 종은 지역별로 독립된 runtime score 보유 |
| 실행·감각 adapter | 해당 종의 기존 backend에 연결. unsupported 조합은 진단/거절 |

설정 asset에는 규칙만 저장하고 지역별 현재 Score를 저장하지 않는다. 최소 구현은 사슴 실행만 지원하되 테이블/이벤트/API를 사슴 이름이나 enum 하나에 하드코딩하지 않는다. 별도 SpeciesId의 순수 규칙 사례로 종 격리를 검증하며, 이는 그 종의 실제 AI 구현 완료가 아니다.

한 지역의 사슴이 A와 B를 동시에 지각하면 두 후보에 제공되는 평판 score/revision은 같다. A만 먹이를 들거나 B만 총을 겨누는 등 현재 지각과 개체별 위험 기억은 달라서 선택은 달라질 수 있다. 신규 접속자와 나중에 지역에 들어온 플레이어도 같은 공통 점수를 적용받는다. 이는 인간 집단에 대한 일반화라는 게임 규칙이며 다른 플레이어에게 실제 피해/개인 Fear를 복제하는 기능이 아니다.

### 7.3 점수 변화: 게임 규칙, reward 아님

Score 범위는 [-100,100], 유효 미등록 key의 기본값0을 fixture 기준으로 둔다.

```text
성공한 한입 급여: Score' = clamp(Score + FeedReputationGain, -100, 100)
인식된 실제 피해: Score' = clamp(Score - DamageReputationScale * clamp(AppliedDamage / MaxHPBefore, 0, 1), -100, 100)
```

FeedReputationGain과 DamageReputationScale은 versioned world config다. fixture 시작값은 +5와60, 예를 들어 MaxHP100에서 실제30 피해이면 -18이다. **사슴1마리에게 한입을 주면 지역 score가+5이며, 사슴100마리가 읽어도 +500이 아니다.** 한 사건의 목격/경보/재전송도 추가 점수를 만들지 않는다. 향후 부분 섭취를 도입하면 비례/중복 보상 계약을 새로 정한다.

- 장착·먹이 보여 주기·키 누르기·실패 급여·Queued 피해는 변화0.
- 공격자를 인식한 피해 후 `PositiveLockoutSteps` 동안 **같은 지역·종 key(모든 플레이어 공유)**의 급여가 성공해도 영양/수량만 반영하고 양의 Score 변화는0. 같은 지역의 다른 사슴에게 먹이를 주거나 다른 플레이어가 대신 급여하거나 무기↔먹이 전환을 해도 타이머를 우회하지 못한다. 시간은 world 공통 simulation time/step으로 비교하고 지역 local counter를 섞지 않는다.
- 최소 판은 passive 시간 회복/감쇠를 넣지 않는다. 수납·거리·로그아웃·representation 재생성으로 호감도를 초기화하지 않는다. 영구 저장은 아직 지원하지 않는다.
- 호감도가 높아도 기존 Fear/UnknownFear/도주 반사·포식자 위협은 유지한다. 급여가 Fear를 지우거나 모든 회피를 무효화하지 않는다.
- 최소 판에 `Score > X이면 무조건 접근` 규칙은 없다. 학습 정책은 regional_reputation을 입력받고 행동을 결정한다. 안전·각성·적당한 식욕 조건에서는 평판이 높은 사람에게 처음 만나는 사슴도 접근/머무름이 증가하는 것을 제품 acceptance로 검증한다. 위험/피로·경로 불가 상황까지 접근을 강제하지 않는다.

지역 평판이 같아도 개체별 배고픔·Fear·부상·여행·위협·위치는 다르므로 최종 행동이 항상 같지는 않다. 직접 피해 개체는 같은 지역의 처음 만나는 개체보다 오래 피할 수 있다. 이는 공통 평판 공유 실패가 아니며 기존 위험 기억을 점수로 상쇄하지 않는다. 이번 판은 별도의 개인 Affinity와 지역 Score를 가중 합산하는 혼합 모델을 추가하지 않는다.

## 8. 피해 결과와 관계 갱신 연결

호감도 하락은 **최종 피해 함수에서 실제 HP 감소를 확인한 뒤 한 번만** 생성한다. 총기/콤보/직접 strike의 공통 commit 지점에 내부 `PlayerHarmCommitted`를 마련한다. 기존 피해량/포획/체력 규칙은 그대로다.

```text
PlayerHarmCommitted = {EventId, WorldEpoch, AuthoritySequence,
    SourceSimulationTime, ObservedAtNativeStep,
    AttackerPlayerKey, AttackerAvatarGeneration, VictimKey,
    EventRegionId, EventSpeciesId, AppliedDamage, MaxHPBefore,
    AttackerRecognized, EvidenceKind}
```

기존 FEcoCombatDamageOutcome만으로는 안정 PlayerKey·지각 증거·Native 시점이 충분하지 않다. UI delegate의 OnDamageOutcome/Multicast에서 reputation을 변경하지 않는다. `StepId`를 simulation step으로 재사용하지 않고 새 필드로 구분한다.

원 사건 EventId는 authority에서 발급하며, 기존 attack 실행 ID/출처/generation/stage/target은 provenance로 남긴다. Committed와 나중 DeathCommitted는 같은 피해를 두 번 차감하지 않는다. Network 재수신/중복 callback도 중복0. 인식된 lethal 피해의 지역 score 변화는 사슴이 죽어도 유지하며, 죽은 Entity에 개인 점수를 재적용하지 않는다.

**공격자 인식(유지되는 초기 규칙):** 사용자의 이번 변경은 공유 대상을 모든 플레이어로 넓히는 것이다. 사건 채택 조건은 앞선 계약의 '피해 개체가 공격자를 인식한 피해만 점수에 반영'을 유지한다. 현재 ApplyPlayerDamage의 Known과 같은 지각 증거를 공통 결과로 전달한다. 미인식 피해는 기존 UnknownFear로 처리하고 지역 평판은 변경하지 않는다. 나중에 사람을 봤다고 소급 귀속하지 않는다. 이는 지역 평판의 공유 범위와 별개의 사건 채택 규칙이다. 보이지 않은 공격도 지역 점수로 처벌하는 별도 게임 규칙은 현재 판에 포함하지 않으며 변경 시 rules version과 Python mirror를 함께 바꾼다.

한 번 채택된 피해 사건의 지역 평판은 같은 지역 전체에 공통 적용된다. 개체마다 별도 '목격으로 점수 차감'을 실행하지 않는다. 기존 목격/무리 경보·Fear는 독립적으로 유지하며, 그 재전파로 reputation을 다시 차감하지 않는다.

## 9. 사슴 관측과 정책 실행 경계

### 9.1 PlayerInteractionEvidence

```text
Snapshot = {WorldEpoch, AgentKey, AuthorityRegionId, SpeciesId, EvidenceNativeStep,
  Players[K]: {valid, local_handle, identified,
    relative_position_native, relative_velocity_native_per_step,
    held_food_visible, visible_weapon_state?, feed_request_visible,
    reputation_known, regional_reputation, reputation_revision, validity_masks}}
```

- K=2부터 시작. 현재 보이는 플레이어를 거리순/동률 안정 ID순 정렬하되 `local_handle`은 해당 episode의 player mapping으로 유지한다. 행 번호를 곧 개인 identity로 저장하지 않는다.
- 플레이어를 식별하려면 기존 감각 범위/FOV·static LOS/식생 차폐를 통과해야 한다. 세션 내 직접 시각 재식별을 지원하는 명시 게임 규칙으로 시작한다. 변장/얼굴 인식/현실적 생체 인식 구현을 주장하지 않는다.
- 손의 먹이는 사람 본체가 보인다는 사실 외에 먹이 anchor의 시야·차폐·활성 Food mode와 HasEdibleItem 조건을 확인한다. 가방 수량을 읽지 않는다.
- `hand_state_known` validity mask를 둔다. 손이 가려져 알 수 없음(mask0)과 실제 빈손/무기임을 봄(mask1, held_food_visible=false)을 구분한다. 서버가 아는 장착 상태로 가려진 손의 mask를 채우지 않는다.
- `feed_request_visible`은 대상 사슴에게 보여 주는 서버의 짧은 제공 gesture/상태와 지각 조건이 있을 때만 제공한다. 숨은 RPC 요청 여부를 그대로 관측에 넣지 않는다. 이 시각 표현을 구현하지 않은 판에서는 해당 필드를 비활성/마스크0으로 둔다.
- 평판은 사슴 자신의 직접 기억이 아니라 현재 지역의 공통 상태다. 플레이어를 식별한 뒤 `ReadReputation(CurrentRegionId, SpeciesId)`로 읽는다. 사람이 사라지면 실시간 위치·장착 상태 후보 갱신을 중단하며 지역 평판 테이블은 유지한다. 개인의 과거 신원 기억은 공통 점수 조회의 필수 조건이 아니며, 현재 대상을 플레이어로 지각하면 같은 값을 적용한다.
- **처음 보는 사람도 기존 지역 평판이 있으면 그 값**을 받는다. 유효 key가 미등록이면 reputation_known=true/score0, player 미식별·소속 불일치·서비스 오류이면 reputation_known=false/payload0이다. 처음 만남을 항상0으로 초기화하면 실패다.
- 같은 observer snapshot의 모든 후보는 고정된 평판 revision 집합을 읽는다. 지역명/SpeciesId는 lookup/감사에 필요하지만 encoder에 문자열/숫자 ID 그대로 넣을 권한은 아니다. `LastRecognizedDamageTime`은 거래 잠금용 장부이며 새 사슴의 직접 피해 기억으로 제공하지 않는다.
- PlayerKey/StableAgentId/RNG는 신경망에 숫자 그대로 넣지 않는다. 실제 normalization·vector shape·embedding·mask는 RL 계약이다. 친절한 역할을 특정 ID/slot0에 고정해 학습 shortcut을 만들지 않는다.

### 9.2 행동 연결

기존 R7e에 손 먹이/지역·종별 공통 평판 입력은 없다. 입력을 제공할 API를 만든 것과 현재 모델이 사용한다는 사실을 구분한다. RL 담당은 기존 5행동의 목표/조향에 PlayerTarget을 연결할지 별도 schema를 쓸지 결정한다.

게임 측 실행 접점은 현재 evidence의 `local_handle`을 대상으로 하는 `PlayerInteractionIntent`다. 필요한 의미는 접근/거리 유지/해제 등이며 action 번호·정책 분해는 이 명세에서 확정하지 않는다. 의도는 자기 식별/증거 시각·Avatar/OfferGeneration을 검증하고 이전 관측의 위치만 사용한다. 움직이는 플레이어의 숨은 live 좌표를 계속 추적하지 않는다. frame마다 순간이동이나 두 번째 movement writer를 추가하지 않는다.

급여 키는 이 의도를 강제 생성하지 않는다. 현재 가까운 eligible 사슴에게만 게임 거래를 요청한다. 기본 R7e에서 실제 급여도 이 조건을 통과하면 가능하다. 학습 전 scripted intent는 테스트에 한정하고 Origin=Fixture로 기록한다.

기존 Life 호기심/후퇴, 여행, SocialControl, Native flight override와의 충돌을 adapter priority로 명시한다. 위험/생존 writer를 제거하지 않는다. 새 policy와 기존 개인 경험 rule 중 실제 제어 소유자를 RL 담당과 버전으로 정하며 'rules가 만든 접근'을 학습된 호감 행동으로 보고하지 않는다.

G_TRAIN 전에는 같은 세계/관측에서 후보 정책의 다른 선택이 실제 목표/접근/회피 결과에 영향을 주는지 확인한다. 기존 경험 rule이 모든 선택을 덮어쓰는 상태에서는 지역 평판 기반 행동 학습이 가능하다고 선언하지 않는다. 필요한 중재 변경은 별도 opt-in 계약으로 합의하고 raw request→각 adapter 보정→executed를 trace에 남긴다.

## 10. 시간과 사건 순서

현재 Native step A는 앞서 준비한 요청을 실행하고 E~F/H는 다음 관측/요청을 준비한다. 이를 유지한다.

1. 권위 경계에서 장착/키 요청의 서버 접수 sequence와 적용 tick을 확정한다. 인벤토리·target token 검증.
2. Native A/B는 이전 관측/준비된 판단을 사용한다. 같은 step 새로 든 먹이를 이미 보고 접근했다고 만들지 않는다.
3. 기존 Native C1 이동 후 C4에서 유효 급여 token을 재검증하고 한입 섭취와 출처별 거래를 수행한다.
4. 성공 급여 결과를 권위 사건 sequence에 따라 reputation에 적용한다. 그 뒤 생성되는 관측에 반영한다.
5. 피해는 현재 resource 경계의 기존 ProcessPlayerStrikes/ProcessCombatDamage에서 확정한다. 그 순서를 바꾸거나 Native C4에 피해를 다시 적용하지 않는다. 피해 결과는 실제 발생 sequence로 reputation에 반영/queue하고 다음 허용 snapshot에 제공한다.

NativeStep과 M4 resource step은 같은 카운터가 아니다. 모든 사건은 SourceSimulationTime, AuthoritySequence, 관계에 반영된 NativeSnapshotStep을 기록한다. 동일 시각이라도 실제 commit 순서를 replay한다. 이미 완료된 급여를 나중 피해 때문에 소급 취소하거나, pending 피해를 이미 발생한 것으로 호감도에 반영하지 않는다. 이미 확정된 피해/사망/장비 전환은 후속 pending 급여를 취소한다.

명시적인 단일 authority dispatcher/안전 경계에서 두 사건 스트림의 reputation mutation 순서를 보장한다. Mass 처리 중 GT callback으로 reputation을 동시에 수정하지 않는다. source 발생·queue 수신·snapshot 관측 시점을 섞지 않는다.

## 11. fixture 설정과 수명

아래는 검증용 시작값이며 최종 밸런스나 학습 계수 확정이 아니다. 하나의 config/hash로 C++/Python에 전달한다.

| 설정 | 시작값 / 규칙 |
|---|---|
| 기능 | 기본OFF, owned fixture만ON |
| 규모 | 독립 region2개, 플레이어2명, region당 사슴2마리 이상. 평판행1/지역·종, 관측 후보K2. 큰 맵 신규 제작 대신 작은 owned fixture 사용 |
| 초기 소유량 | 세션/episode당10개. 리스폰 재지급0 |
| 한입량 | 0.04 Native food units. 기존 FoodEnergyPerUnit·EatWant/MaxEnergy/Reserve 설정 보존 |
| 급여 기하 | body 거리200cm 상한 + 논리 anchor 거리100cm·높이 차50cm 상한, LOS 필수 |
| 감지 | 최대1600cm와 현재 시야/날씨 중 더 좁은 범위, 손 anchor 차폐 별도 |
| 급여 cooldown / 요청 기한 | 성공 후8 Native steps / 접수 후8 steps. 초과 요청 거절, 무한 대기 없음 |
| 지역 평판 | [-100,100], 유효 미등록 key0, 급여+5, 인식 피해−60×실제피해/MaxHP |
| 양의 변화 잠금 | 해당 지역·종에서 인식된 어느 플레이어의 피해든 발생한 후75 Native steps=10simulation초 |
| passive reputation decay | 없음. 기존 Fear 감쇠는 변경하지 않음 |

pending 요청 retry는 같은 request key를 사용하며 완료/만료 뒤 새 transaction으로 살아나지 않는다. 바쁜 상태/거리/식욕 부족의 UI는 명확히 표시하고 한 번의 키 입력이 여러 급여가 되지 않게 한다.

reset은 epoch를 바꾸고 관계/held 상태/pending/사건 dedup/로컬 handle을 초기화한 후 InitialGrant와 최초 evidence를 만든다. source array나 지면 먹이 TTL은 존재하지 않는다. PlayerState 리스폰은 지역 공통 평판/개인 수량 유지, Avatar와 pending만 교체한다.

최소 판은 ON 세계의 기존 checkpoint 저장/복원 및 '복원 가능' 전체학습 export를 명시 거절하고 별도 replay export만 지원한다. inventory/관계/장착을 누락한 채 저장 성공으로 표시하지 않는다. OFF 저장은 보존한다. 향후 joint persistence는 지역·종 평판과 플레이어별 inventory/계정 ID 매핑을 별도로 설계한다.

## 12. Python 담당자에게 다시 넘겨야 하는 것

먹이 좌표/잔량만 넘기는 이전 패키지는 이번 목표에 부족하다.

| 인계 항목 | 구체적 내용 | 정책 입력 여부 |
|---|---|---|
| held-food world rules | inventory/hand 전환·급여 거리·한입·쿨다운·취소/0수량 | 내부 세계 규칙 |
| 감각 규칙과 evidence | player/hand 시야·LOS, 현재 보이는 food/weapon/gesture, 상대 위치/속도 | RL이 인코딩할 허용 입력 |
| 평판 규칙 | RegionId×SpeciesId, 증가/감소식·clamp·지역잠금·reset/identity·출생/이주 | 자기 지역에서 식별한 상대의 score만 입력 |
| damage bridge | 확정 실제 HP 감소·Known/Unknown·event dedup·source/native 시간 대응 | 진단 정답과 감각 사건 분리 |
| 동작 접점 | policy→player target/control, 기존 생존/여행 우선순위와 실제 실행 | RL이 mapping 결정 |
| scripted player | Food/Weapon 전환, 키 요청, 이동·공격의 타이밍/결과 | 환경 actor. player 정책 자체는 학습 대상 아님 |
| 상태/결과 replay | 초기 state, geometry evidence, event order, intake/HP/reputation before/after | parity/평가용. 통째로 observation 금지 |
| config·manifest | 실제 SHA+dirty hash, world/model/schema, 단위, 규칙/hash, 지원 범위 | 재현 근거 |

순수 C++ leaf와 Python 참조에는 **급여·관계 갱신·사건 순서**가 포함되어야 한다. Python 학습 env 연결은 RL 담당이 실제 baseline을 골라 진행한다. 기존 `herbivore_rl/env_v2/world.py` 및 별도 r8 경로를 임의 대체하지 않는다.

Reputation 증가량을 그대로 PPO reward로 쓰는 것은 결정되지 않았다. game Reputation은 지역 공통 상태이며, reward·normalize·mask·행동 차원은 RL 담당 업무다. 특히 한 급여 때문에 점수를 읽는 모든 사슴에게 동일한 급여 보상을 지급하지 않는다. 실제 영양은 받은 사슴에게만 들어간다. 지역 점수 읽기 자체는 새 reward 사건이 아니다.

## 13. 검증과 완료 기준

| ID | 사례 | 필수 결과 |
|---|---|---|
| H01 | 가방에만10개 / 손에 Food | 가방 상태 food 증거0, 시야 내 손 Food는 증거1. 단순 장착의 reputation 변화0 |
| H02 | 손만 가려짐/사람 비가시 | 먹이/지역 평판 관측 조건 준수, 원격 inventory·live좌표 누출0 |
| H03 | 성공 급여 | items−1, actual food .04, 받은 사슴만 C4 nutrition, 해당 Region×Species score+5 한 번, A·B 모두 같은 값 조회 |
| H04 | 멀리/벽/수면/도주/여행/포만/죽음 | 실패 공급/영양/reputation0, 기존 생존 동작 유지 |
| H05 | 중복 요청/마지막1개/2플레이어→1사슴 | 성공 횟수와 debit·intake·reputation 변화가1:1; loser 변화0 |
| H06 | pending 뒤 수납/무기/사망/리스폰 | 옛 Offer/Avatar 요청 취소, 수량/영양0, 이중 장착/공격0 |
| H07 | 총기/콤보/직접strike 실제피해 | 공통 실제 HP손실 기준1회 감소, Queued/Miss/Rejected/0피해 변화0 |
| H08 | Committed→DeathCommitted | 같은 hit 관계 차감1회, 제거된 Entity/다른 세대 재적용0 |
| H09 | A급여/B공격 | 같은 지역·종의 점수에 두 사건 반영. A·B 조회값 동일, 각자의 소유량/사건 identity는 분리 |
| H10 | 보이지 않는 공격자 | UnknownFear 경로 유지, 지역 공통 reputation 변화0, 소급 귀속0 |
| H11 | 공격 직후 먹이 전환/급여 | PositiveLockout 동안 성공 영양은 허용될 수 있어도 reputation양의 변화0, Fear 강제초기화0 |
| H12 | 움직이는 player/anchor·headless | 최근 evidence 목표만 사용, 실제 접촉/높이/LOS로 소비; 렌더 bone 비활성에도 같은 판정 |
| H13 | 자연 풀 위 급여 | 자연 장부/뜯김 흔적과 분리, 한 step 섭식 예산 중복0 |
| H14 | reset/리스폰/관계capacity/ON저장 | 정의한 초기화·보존·명시 실패, 악의적 slot churn으로 기억삭제0 |
| H15 | C++/Python replay | state/event/identity 정확 일치, 수치 허용오차 통과, 첫 divergence 출력 |
| H16 | OFF/Standalone/server+client2 | 기존 정책·자원·무기 OFF회귀 없음, owner inventory 분리와 지역·종의 공통 관계 결과 올바름, client 논리writer0 |
| H17 | R1 사슴D1에게 급여 후 R1 D2 첫 조우 | D2가 직접 경험 없이 동일 score/revision을 조회. 다른 사슴 영양/Fear/직접 기억을 D2에 복사하지 않음 |
| H18 | R2의 첫 사슴·R1의 다른 종 | 각각 미등록이면0, R1 사슴 score와 혼합0 |
| H19 | 출생/개체제거/지역표현 unload | 새 사슴은 지역값 조회, 마지막 사슴이 사라져도 평판행 유지 |
| H20 | 소속 인계/접경 공격 | 실제 membership commit 후 새 지역값 조회. 사건 지역은 victim/recipient의 commit 시 소속, 공격자 위치와 독립 |
| H21 | 1개체 vs 100개체 조회/동일 사건 재수신 | 지역 delta는 동일, 반복 조회는 revision/delta를 만들지 않음 |
| H22 | 같은 지역 다른 사슴에게 공격 직후 급여 | region/species-scoped PositiveLockout 유지. 다른 사슴/플레이어 선택으로 악평 잠금 우회0 |

산술 fixture 예: R1 D1에게 A가 급여1회면 Score(R1,Deer)=5. R1의 D2가 A 또는 B를 처음 봐도5, R2 D3는 어느 플레이어를 봐도0이다. 이어 R1 D1에 B가 인식된 실제30피해(MaxHP100)를 주면 같은 Score(R1,Deer)=5−18=−13이다. A·B 모두−13을 적용받는다. 죽음 알림/100마리 조회로 추가 차감하면 실패다. 별도 종의 기본값0은 그대로다. 산술 기대값은 실제 플레이 capture를 대신하지 않는다.

수치 비교 기본 허용오차 `1e-8 + 1e-9 * max(abs(a),abs(b))`; ID/거래개수/순서/인식 mask는 정확 일치. 모델 추론 tolerance는 기존 계약을 따른다. JSON int64는 문자열로 내보낸다.

**G_GAME(handoff_ready):** H01~H25의 해당 engine-free/UE 검증, 실제 runnable C++/Python 참조/비교/재생 패키지, 남은 RL 접점 명시까지 완료. 실제 다지역 이주 장면 미검증이면 H20의 membership/사건 replay 검증과 실제 runtime 범위를 구분하며 전체 이주 acceptance로 주장하지 않는다.

**G_TRAIN(training_ready):** RL 담당이 baseline·관측/행동 mapping·reward·reset/종료·scripted player를 연결하고 짧은 rollout/형상·유한값·재현 검사를 통과. 그 전에는 training_ready=false.

학습 평가는 'D1에게 급여하여 지역 점수 상승→D1과 다른 D2의 첫 조우'를 필수로 한다. 빈손인 동일 플레이어, 다른 지역, 다른 플레이어, 지역점수만 바꾼 대응 평가를 포함해 손 먹이 반응과 평판 반응을 분리한다. 같은 점수에서도 직접 피해/포식 위험이 있는 개체는 달리 행동할 수 있다. 'food를 보면 누구든 따라오기'나 직접 먹은 D1의 접근만으로 지역 평판 학습 성공이라 하지 않는다.

추가 필수 사례 H23~H25를 G_GAME 범위에 포함한다.

| ID | 사례 | 필수 결과 |
|---|---|---|
| H23 | 숫자1 장착/재입력/무기 전환/누락 socket/새 client relevance | 오른손 기본 Sphere, 중복 장착0, 수량 차감0, 상태와 표시 일치, 무기 동시 사용0 |
| H24 | A 공격 후 B 급여/새 접속 C/공격자 로그아웃 | 공통 악평 유지, 잠금은 B에게도 적용. C도 같은 score 조회 |
| H25 | 종 설정 및 비활성 종 | 독립 SpeciesId의 규칙 격리, 미지원 급여 거절, 설정만으로 미구현 종 AI 활성화0 |

## 14. 자료 근거와 이번 작업 범위

[Epic Lyra Inventory and Equipment](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-inventory-and-equipment-in-unreal-engine)의 소유 inventory와 사용 중 equipment 구분을 이 프로젝트의 손 상태 분리에 적용했다. 이는 Lyra 전체 도입이나 먹이를 무기 subclass로 처리하라는 의미가 아니다. 열람 문서는 UE5.9 표시이므로 실제 API는 프로젝트 UE5.8 소스로 검증한다.

[Gymnasium 환경 설계](https://gymnasium.farama.org/introduction/create_custom_env/)의 내부 상태/관측/info 구분에 따라 관계·감각 데이터와 서버 진단 정답을 나눴다. 위 임계값·호감도식·급여 거래는 이 프로젝트를 위한 제안이며 외부 문서가 검증한 밸런스가 아니다. 앞선 연구 근거는 [전체 조사 기록](PLAYER_FOOD_PROVISION_RL_PLAN_20261114.md)에 남긴다.

이번 변경은 문서만이다. Source·링크·변경 범위를 확인하며 새 C++/에셋/학습·UBT/PIE 실행은 없다. 다음 구현은 루트 AGENTS의 권위·단일 writer·RL 소유권·빌드 규칙을 따른다.

오른손 부착은 [Epic Skeletal Mesh Sockets](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine), 키의 논리 Action/Mapping Context 분리는 [Epic Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine)을 참고했다. 열람 페이지는 UE5.9 표시이며 구현 API·에셋은 설치 UE5.8과 대조한다. 지역·종 공통 평판 및 수치는 사용자 요구에 따른 프로젝트 설계다.
