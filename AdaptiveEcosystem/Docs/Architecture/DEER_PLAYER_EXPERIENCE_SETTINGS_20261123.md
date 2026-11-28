# 사슴의 플레이어 경험 직접 확인 설정

## 레벨과 플레이 설정

1. 두 지역의 Native v3 사슴과 기존 사냥을 확인하려면 `/Game/JWJ/1102_Integration_JWJ`의 작업용 복사본을 연다. 기존 전투 설정 기준인 `BP_EcoGameMode_Common`과 `BP_EcoPlayer_Common`을 사용한다. 사슴이 있는 권위 지역 이름은 현재 레벨의 Outliner 또는 ECO INSPECTOR에서 확인한다. [1102 전투 설정](PLAYER_HUNTING_IMPLEMENTATION_AND_TEST_20261106.md#직접-확인-순서)
2. Outliner에서 `EcoCreatureIntegrationSpawner`를 선택한다. Details의 **Creature → V3 → Use V3 Policy**, **Creature → Life → Enable Ecosystem Life**를 켠다. 피해 비교에는 **Creature → Combat → Enable Player Combat**도 켠다.
3. 같은 스포너의 **Creature → Deer Player Experience**에서 **Enabled**와 **Log**를 켠다. 기능 기본값은 OFF다. **Number of Players = 1**, **Net Mode = Play Standalone**으로 시작한다.

## 비교에 쓸 값

| 설정 | 체감 확인용 값 | 효과 |
| --- | --- | --- |
| Sensitivity | `1.0` | 지역 경험의 거리 반영 배율. `0`은 중립 거리 비교, `0.25`~`0.5`는 작은 변화부터 살펴볼 때 쓸 수치 |
| Damage Gain / Feeding Gain | `2.5` / `4.0` | 실제 HP 손실률과 급여가 더한 Energy 비율에 곱해 지역에 누적 |
| Half Life | `240 s` | 각 지역의 피해 압력과 급여 친숙도가 절반으로 줄어드는 권위 게임 시간. 빠른 확인에는 `30 s` 사용 |
| Alert Distance / Retreat Distance | `1200 cm` / `400 cm` | 중립 성향에서 천천히 접근할 때의 기준 거리 |
| Alert Distance Shift / Retreat Distance Shift | `1200 cm` / `850 cm` | 성향이 ±1일 때 적용할 거리 차이. 실제 문턱은 해당 사슴의 현재 시야 범위로 제한 |
| Release Margin / Fast Approach Bonus | `200 cm` / `450 cm` | 이미 반응한 사슴의 해제 거리 여유와 빠르게 접근하는 사람을 봤을 때의 거리 가산 |
| Starting Rations / Feed Range / Ration Energy | `12` / `800 cm` / `0.2` | 접속한 플레이어의 초기 급여 재고, 최대 급여 거리, 한 번의 최대 에너지 공급량 20% |

## 플레이 중 조작과 관측

- 피해를 줄 지역에서는 기존 **F** 근접 공격으로 실제 명중을 낸다. 비교 지역에서는 약 8m 이내에서 배고픈 사슴을 바라보고 **H**로 한 번씩 급여한다. `H`는 개발 빌드 입력이다. 게임 입력에 연결할 때는 플레이어의 `EcoPlayerEcologyComponent.TryFeedDeer` Blueprint 함수를 사용한다.
- **Output Log**에서 `[Eco Deer Experience]`로 검색한다. `Kind=Damage`와 `Kind=Feeding`은 확정 결과다. `Pressure`, `Familiarity`, `Attitude`를 지역별로 확인한다. `Feed returned`가 나오면 예비 재고가 돌아간다. 에너지가 90% 이상이면 급여를 반환한다.
- 잠깐 물러났다가, 각 지역에서 사건을 직접 겪지 않은 사슴을 늑대나 은신처가 없는 열린 장소에서 같은 속도로 다시 접근한다. `Response=Alert` 또는 `Retreat` 로그의 첫 `DistanceCm`을 비교한다. 선택한 사슴에 `eco.MoveTest Observe`를 실행하면 `Samples.csv`의 `regional_player_response`에 평상시 0, 경계 1, 후퇴 2가 기록된다. `regional_player_attitude`와 거리 열을 같은 행에서 확인할 수 있다. [이동 관찰 가이드](../Performance/MassMovement/10_MOVE_TEST_OBSERVE_GUIDE.md)
- 같은 조건으로 다시 비교할 때 Play를 새로 시작한다. 새 Play와 체크포인트 복원은 지역 경험을 중립으로 시작한다. 체크포인트 복원은 당시 진행 중인 급여 예약을 반환한다.

## 지역 성향의 자연 회귀

권위 생태계가 먹이 자원 단계를 진행하면서 각 지역의 피해 압력과 급여 친숙도를 따로 감쇠시킨다. 새 사건이 없으면 두 값은 각각 0에 가까워지고, 그 차인 `Attitude`도 중립인 0에 가까워진다. `Half Life = 240 s`에서 성향이 +1.0 또는 −1.0이었다면 4분 후에는 약 +0.5 또는 −0.5, 8분 후에는 약 +0.25 또는 −0.25다.

빠르게 확인하려면 Play 전에 **Half Life = 30 s**로 바꾸고, 한 지역에서 피해나 급여를 확정한 뒤 같은 Play를 유지한다. 추가 피해·급여 없이 30게임초와 60게임초가 지난 후, 해당 지역의 사슴에게 다시 접근해 `Response=Alert` 또는 `Retreat` 로그의 `Attitude`를 확인한다. 최초 절댓값의 약 1/2과 1/4이 기준이다. [지역 생태계의 갱신 경계](../../Source/AdaptiveEcosystem/Ecology/EcologyResourceSimulation.cpp)
