# 플레이어 손 먹이와 지역·종 공통 평판 — Python 담당자 인수인계

게임 규칙판은 `held_food_regional_species_human_reputation_v3`다. 이 문서는 2026-11-14 Unreal 구현과 빌드 결과를 기준으로 한다. 손 먹이 장착·감지·급여, 피해에 따른 지역 평판, 결과 조회를 C++에 연결했다. UE 5.8 UBT의 `AdaptiveEcosystemEditor Win64 Development` 빌드는 성공했다. 에디터 설정 및 실제 PIE·멀티 재현, Python 환경 연결과 양쪽 결과 비교가 남아 있다. 현재 상태는 `training_ready=false`다. [에디터 설정과 수동 검증](../Player/HAND_FOOD_EDITOR_SETUP_20261114.md)

## 1. 게임에서 구현한 흐름

| 단계 | 구현 |
|---|---|
| 소유 | `AEcoPlayerState`에서 먹이 종류와 개수를 관리한다. 시작 시 서버에서 지급하고 현재 수량을 소유자에게 복제한다. 리스폰 시 같은 PlayerState의 수량을 유지한다. |
| 장착·표시 | Pawn의 `UEcoHandFoodComponent`가 서버에서 장착을 검증하고, 복제된 장착 상태로 오른손 Socket에 먹이 메시를 표시한다. 테스트 설정에서는 숫자 1을 `Equip Hand Food`에 연결하고 기본 Sphere를 쓴다. 장착만으로 수량과 평판은 변하지 않는다. |
| 급여 접수 | 별도 입력의 `Request Hand Feed`가 서버에 요청한다. 서버가 살아 있는 Native 초식 개체 중 해당 먹이를 허용하는 가장 가까운 대상을 거리·시야·방향으로 고른다. 동거리면 작은 `StableAgentId`를 선택한다. 대상과 현재 지역, Avatar/Offer 세대, epoch를 기록한다. |
| 한입 소비 | Native 이동·포획 판정 뒤 C4 섭식에서 현재 손과 입의 거리·높이·차폐, 행동, 한입을 받아들일 수 있는 섭식량을 다시 확인한다. 성공하면 플레이어 먹이 한 개를 차감하고 받은 개체의 에너지·비축 계산에 한입을 전달한다. |
| 피해 | `ApplyPlayerDamage`에서 실제 HP 감소를 확인한다. 피해 개체의 공격자 시야 판정인 `Known`이 참인 사건을 피해 개체 소속 지역·종 평판에 반영한다. 주변 개체만 공격자를 봤다면 기존 위협 기록은 바뀔 수 있지만 공통 평판은 그대로다. |
| 공통 평판 | `UEcologySimulationSubsystem`에서 `(WorldEpoch, RegionId, SpeciesId)`로 관리한다. 같은 지역·종 개체는 보이는 플레이어가 누구든 같은 점수를 읽는다. 다른 지역과 종은 별도다. |

플레이어가 먹이를 쥐고 있으면 무기 표시와 사용을 중단한다. 식량을 수납하거나 마지막 한 개가 소비되면 손이 비워진다. 실제 오른손 Socket은 표현에 사용한다. 시야와 급여 접촉은 서버 Pawn 좌표의 `HandOffsetCm`과 사슴의 현재 Native 몸·머리 방향에서 계산한 입 좌표를 사용한다. 시뮬레이션에서는 애니메이션이나 Sphere 충돌을 재현할 필요가 없다. [장착·요청 C++](../../Source/AdaptiveEcosystem/Player/EcoHandFoodComponent.cpp) · [급여 C++](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureHandFeed.cpp) · [피해 C++](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreaturePlayerOutcomes.cpp)

## 2. 환경에서 재현할 규칙

### 입력, 시간, 섭식

- 시뮬레이션 한 스텝은 `8/60초`, Native 공간 단위 하나는 `200cm`다. C4는 현재 스텝의 이동 뒤 위치와 현재 행동을 사용한다. 급여 후 생성되는 시각 증거는 다음 정책 판단의 입력 후보로 사용한다. 피해는 기존 전투 처리 경계에서 확정되므로 재현 시 사건이 실제 적용된 순서를 기록한다.
- 한 플레이어는 동시에 요청 하나를 보류할 수 있다. 접수 시 선택한 대상은 이후 다른 개체로 바꾸지 않는다. 요청 만료, 손 상태/Avatar 세대/epoch 변경, 대상 지역 변경 및 피해로 인한 취소는 소비를 발생시키지 않는다. 동일 개체·Native 스텝에 여러 요청이 들어오면 서버 접수 순으로 평가하고 첫 성공만 소비한다. 코드에는 실시간 RPC 빈도 제한도 있으므로 Python의 스텝 입력과 Unreal 입력 타이밍을 비교할 때 구분한다.
- C4에서 급여 가능한 실제 행동은 `Graze`다. 포획 대상, 여행 중, 경계 반사 활성, 웅크림, 현재 포식자 위협이 있으면 실패한다. 현재 스텝의 `EatWant`보다 한입량이 크면 `InsufficientAppetite`다. 손 급여가 성공한 개체는 그 스텝의 자연 먹이 배분에 참여하지 않는다. 실패했으면 기존 풀 섭식 절차를 따른다.
- 손 급여 영양은 기존 에너지·비축 계산에 전달한다. 자연 풀 셀 잔량, `FoodEaten`, `LastIntake`에는 자연식만 기록한다. 손으로 먹은 양은 `LastHandFeedIntake`와 급여 결과에서 확인한다. 영양이 들어간 개체는 직접 먹은 사슴 한 마리다.

[Native C4](../../Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h) · [실행 조건과 시각 증거](../../Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp)

### 공통 평판

| 사건 | 현재 계산 |
|---|---|
| 유효 지역·종의 첫 조회 | 종 프로필의 `InitialScore`, revision `0` |
| 성공한 한입 | 잠금 종료 스텝부터 `FeedGain`을 더한다. 잠금 중에도 한입을 먹을 수 있고 점수 변화는 0이다. |
| 공격자를 인식한 확정 피해 | `DamageScale × min(1, 실제 HP 감소 / 최대 HP)`를 뺀다. 잠금 종료를 `현재 NativeStep + PositiveLockoutSteps`로 설정한다. |
| 실패 급여, 빗나간 공격, 공격자 미인식 | 공통 점수와 revision 변화 0 |

성공 급여·채택된 피해를 처리할 때 `Score`를 종 프로필의 범위로 제한하고 `Revision`을 한 번 올린다. 잠금이나 상·하한 때문에 점수가 그대로여도 채택된 사건이면 revision은 증가한다. 사망 통지나 관측 조회는 별도 평판 사건을 만들지 않는다. 시간 감쇠는 없다. 첫 조우·리스폰·플레이어 교체는 평판을 초기화하지 않는다. 새 월드 또는 epoch는 초기점으로 시작한다. 개체가 새로운 지역 소속으로 인계되면 새 지역·종 값을 읽는다. 평판은 세션 상태이며 설정을 활성화한 월드의 기존 생태계 체크포인트 저장·복원은 거절한다. [평판 장부와 수식](../../Source/AdaptiveEcosystem/Ecology/EcoHumanReputation.cpp)

### 설정값

| 위치 | C++ 초기값과 에디터에서 확인할 값 |
|---|---|
| PlayerState / 먹이 Data Asset | 시작 개수 `10`, 한입 `0.04` 먹이 단위. 수동 재현에서 걷는 개체를 대상으로 삼을 때는 `0.01`부터 조정할 수 있다. 실제 장면의 값을 기록한다. |
| Pawn 컴포넌트 | `HandOffsetCm=(45,25,0)`, 조준 반각 `45°`, 요청 만료 `8` 스텝, 성공 뒤 쿨다운 `8` 스텝 |
| Spawner의 종 프로필 | `AcceptedFoodIds`, 입 오프셋 `(30,0,75)` cm, 눈 높이 `75cm`, 몸 거리 `200cm`, 손·입 거리 `100cm`, 높이차 `50cm`, 손 먹이 감지 상한 `1600cm` |
| 같은 종 프로필의 평판 | 초기 `0`, 범위 `[-100,100]`, 급여 `+5`, 피해 계수 `60`, 양의 변화 잠금 `75` 스텝 |

맵에서 Native v3와 Ecosystem Life를 켜고 `PlayerInteractionProfiles`에 종 항목을 추가한다. 기존 사슴의 일반적인 `SpeciesId`는 `Herbivore`이며 재현 맵의 실제 Mass 설정을 확인한다. 학습·비교에서는 기본 상수보다 실제 실행 설정을 우선한다. [설정 자료형](../../Source/AdaptiveEcosystem/Core/EcoPlayerInteractionTypes.h) · [재현 맵 설정](../Player/HAND_FOOD_EDITOR_SETUP_20261114.md)

## 3. 관측과 진단 계약

C++에서 개체별 `FEcoIntegratedV3Fragment`에 `InteractionEvidenceStep`, `VisibleInteractionCount`, `VisibleInteractions`를 보관한다. 등록된 살아 있는 플레이어 후보를 대상으로 한 뒤 개체 기준 가까운 가시 후보를 최대 2개 남긴다. 동거리는 `PlayerId` 순이다. 시야각, 날씨 등을 적용한 기존 가시 거리와 프로필 거리의 최솟값, 식생 차폐, 월드 정적 충돌을 검사한다. 플레이어의 눈 위치를 볼 수 있어야 후보가 되고, 손 위치는 별도로 검사한다. 손 먹이는 위치를 가진 지각 대상이며, 급여 결과는 별도 사건이다.

| `FEcoVisiblePlayerInteraction` 필드 | 사용 의미 |
|---|---|
| `PlayerId`, `AvatarGeneration` | 보이는 상대를 목표와 연결하는 식별 정보. 정책이 학습하는 수치 입력과 구분한다. |
| `RelativePositionUnits`, `VelocityUnitsPerStep` | 개체 기준 상대 위치와 플레이어 속도. Native 단위/스텝. |
| `bHandKnown`, `VisibleFoodId`, `RelativeFoodPositionUnits` | 손을 관측했는지, 시야 안에 실제 들고 있는 먹이 종류·위치. 손이 가려졌거나 손 데이터가 없으면 `bHandKnown=false`. 손이 보이면서 먹이가 없으면 `true`와 빈 ID. |
| `bReputationKnown`, `Reputation.Score` | 현재 보이는 상대에게 적용할 지역·종 공통 점수와 유효 여부. 해당 지역·종의 가시 후보들은 동일 값. |
| `Reputation.Revision`, `Reputation.LockUntilStep` | 비교·진단용 상태. 정책 입력 사용은 관측 계약에서 따로 결정한다. |

`FEcoHandFeedReceipt`에는 `Result`, `RequestId`, `TargetId`, `NativeStep`, `WorldEpoch`, `RegionId`, `SpeciesId`, `ActualFood`, `ReputationDelta`, `Reputation`이 들어간다. 결과는 소유자에게 전달되고 서버 로그 `[EcoHandFood]`, `[EcoHumanReputation]`와 권위 Inspector를 함께 볼 수 있다. 현재 로그·Inspector는 관측 및 결과의 일부 필드를 담는다. 전체 시뮬레이션 입력을 로드할 replay/학습 export는 아직 없다. 숫자 비교 자료를 만들 때 설정과 요청·피해 적용 순서를 함께 남긴다. [C++ 자료형](../../Source/AdaptiveEcosystem/Core/EcoPlayerInteractionTypes.h) · [Inspector](../../Source/AdaptiveEcosystem/Creature/Debug/EcoCreatureInspection.cpp)

## 4. Python 담당자 작업 순서

1. 팀에서 실제 쓰는 환경 branch/SHA·환경 경로·모델 버전을 고정한다. 게임 담당과 먼저 **움직이는 플레이어를 현재 정책의 행동으로 접근하거나 피할 수 있는지** 확인한다. 지금 C++에는 가시 후보 데이터가 있으며 이를 받는 새 정책 encoder와 행동 adapter는 합의가 필요하다.
2. 환경 월드별로 공통 평판 테이블과 플레이어별 수량·장착·요청을 구성한다. 정지, 이동, 들기/수납, 급여 요청, 실제 피해를 재현할 스크립트를 만든다. 개체 사망·리스폰과 월드 `reset`을 구분한다. 한 사건을 관측 개체 수만큼 처리하지 않는다.
3. 아래 사례에서 결과를 비교한다. 평판, revision, 먹이 차감, 섭식 출처, 피해 인식 마스크와 순서가 맞으면 기존 학습 환경의 `reset`/`step`에 연결한다.
4. 다음 관측·행동 계약에서 가시 손 먹이, 플레이어 상대 위치, 공통 평판과 유효 마스크의 인코딩을 정한다. 위험·배고픔·가시 여부와 함께 관측하고, 보상과 정책 행동의 매핑은 학습 담당이 정한다. 점수 자체는 월드 상태다. 한입의 영양이나 해당 보상을 관측한 사슴 모두에게 복제하지 않는다. 숨은 위치와 개체·지역 ID는 정책이 정답을 외우는 입력으로 사용하지 않는다.
5. 짧은 rollout에서 접근·거리 유지·회피 선택이 실행 차이를 만드는지 확인하고, 안전·배고픔이 같은 조건에서 먹이 유무와 지역 평판을 교차 평가한다. 빈손의 다른 플레이어 B를 처음 본 사슴의 반응 변화, 위협 상황에서의 안전, 다른 지역·종 격리를 함께 평가한다. 후보 모델의 C++ 연결과 실제 PIE 확인은 이후 단계다. [RL 담당 안내](ECOSYSTEM_RL_AGENT_GUIDE_20261024.md)

## 5. 비교할 최소 사례

다음은 기본 평판 설정의 기대값이다. 실제 게임에서 수집한 검증 기록과 구분한다.

| 입력 | 기대 결과 |
|---|---|
| 숲 사슴 D1에게 A의 한입 성공 | A 수량 `10→9`, D1에만 한입 반영, 숲 사슴 `0→5`, revision `1`. 초원 사슴과 숲의 다른 종은 초기값. |
| 숲 D2가 B를 처음 봄 | B가 직접 급여하지 않았어도 공통 점수 `5`. B의 손 상태는 실제 관측을 사용한다. |
| B가 숲 D1에게 인식된 `30/100 HP` 피해 | 숲 사슴 평판 `5→-13`, revision `2`, 새 종료 스텝 설정. A·B·신규 접속자 모두 같은 점수. 죽음 통지로 다시 차감하지 않는다. |
| 잠금 중 A 또는 B의 한입 성공 | 해당 플레이어 수량 `-1`, 받은 개체 영양 반영, 점수 `-13`, revision `3`. 잠금 종료 스텝부터 성공 시 다시 상승 가능. |
| 공격자 미인식 피해, 멀리서 급여, 포만, 수납 뒤 만료 | 해당 사건의 평판 변화 0. 실패 급여는 수량·섭식에 영향 0. 숨은 손은 `bHandKnown=false`. |
| 같은 스텝에 두 플레이어가 한 사슴에게 유효한 급여 요청 | 접수 순서상 첫 성공만 한입과 평판을 확정한다. |
| 대상 사슴이 지역 이동 또는 새 세계 시작 | 현재 소속 지역의 값 조회. 새 세계의 점수·수량은 각 초기 설정부터 시작한다. |

## 6. 팀에 전달할 파일과 기준

이 문서와 에디터 설정 안내, 기능 변경만 담은 commit 또는 patch, 재현 맵의 설정값과 작은 사건 로그를 넘긴다. 확인 시점은 `jwj-1024`의 기준 HEAD `ef44df15`와 로컬 변경을 포함한 작업 트리다. **현재 HEAD만 전달해서는 이 기능 소스를 재현할 수 없다.** 실제 전달 시 기능을 담은 SHA 또는 patch를 기록한다. Python에서 월드 규칙을 재현하는 단계는 계약과 필요한 변경만으로 시작할 수 있다. UE에서 모델을 검증할 단계에는 해당 기능과 의존 커밋을 함께 사용한다. [에디터 설정 및 수동 검증](../Player/HAND_FOOD_EDITOR_SETUP_20261114.md)
