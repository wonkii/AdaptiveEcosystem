# 사슴 조우 온라인 학습 직접 확인

소유 테스트 맵에서 **Standalone**으로 시작한다. 이 기능은 개발 빌드에서 권위 월드의 Native v3 사슴에 적용한다. 기본 모드는 `Off`다. [개발 명세](DEER_ENCOUNTER_ONLINE_LEARNING_SPEC_20261125.md)

## 에디터 설정

1. `/Game/JWJ/1102_Integration_JWJ`의 소유 복사본 또는 Native v3 사슴과 플레이어 전투가 이미 연결된 소유 맵을 연다. 학습용으로 사용할 준비된 `EcoCreatureIntegrationSpawner`는 하나로 둔다.
2. 스포너의 **Use V3 Policy**, **Enable Ecosystem Life**, **Enable Player Combat**을 켠다.
3. **Creature → Deer Player Experience**의 **Enabled**, **Allow Deer Encounter Learning**, **Log**를 켠다. 기존 플레이어 Pawn의 `EcoPlayerEcologyComponent`에서 **Threat Enabled**를 확인한다. 적응 전후 비교 중에는 기존 `eco.IntegratedV3.AutoAdapt`를 끈다.
4. UE 5.8 Windows의 엔진 번들 Python과 프로젝트 경로의 `Tools/DeerEncounterLearning/train.py`를 사용한다. 추가 패키지는 필요 없다.

## 콘솔 명령

| 명령 | 동작 |
|---|---|
| `eco.DeerEncounter.Mode` | 현재 모드, 진행 중 작업, 지역별 정책 버전 및 표본 수를 표시 |
| `eco.DeerEncounter.Mode Frozen` | 가중치가 고정된 조우 정책으로 전환 |
| `eco.DeerEncounter.Mode Online` | 조우 정책으로 행동하며 실제 조우를 수집하고 학습 |
| `eco.DeerEncounter.Mode Rollback` | 이전에 승인된 가중치가 있는 지역을 직전 가중치로 복구 |
| `eco.DeerEncounter.Mode Off` | 기존 조우 규칙으로 복귀하고 학습 작업을 취소 |

전환은 다음 Native 스텝에서 적용한다. Online → Frozen은 현재 모델로 추론을 계속한다. Off 이후 재활성화, 새 Play, 체크포인트 복원은 이전 학습을 이어받지 않는다. Dedicated Server 비교에서는 서버 콘솔에서만 명령을 실행한다.

## 플레이로 확인

1. **Off와 Frozen 비교.** Output Log에서 `LogEcoDeerEncounter`와 `[Eco Deer Experience]`를 찾는다. 사람이 보이는 곳에서 비슷한 속도로 접근해 첫 `Alert` / `Retreat`의 `DistanceCm`을 비교한다. 같은 줄의 `AlertCm`과 `RetreatCm`은 기존 규칙의 비교용 문턱이다. 신경망 모드에서는 현재 모델이 표본 추출로 행동을 고른다. 직접 피격이나 늑대 위협이 기존 도주를 일으키는지도 확인한다.
2. **Online에서 표본 수집.** 같은 지역에서 `F`로 사슴을 실제로 맞히거나, 사슴을 바라본 채 약 8m 안에서 `H`로 급여한다. 서버가 확정한 HP 손실과 Energy 획득은 활성 조우가 없는 경우에도 지역 학습에 들어간다. 밤, 수면, 달리기, 연속 피격도 피해 수집을 막지 않는다. 급여 취소·빗맞음에는 학습 신호가 생기지 않는다. [기존 사냥·급여 설정](DEER_PLAYER_EXPERIENCE_SETTINGS_20261123.md)
3. **지역별 버전 확인.** 지역당 확정 결과가 6건 모이면 배치를 제출한다. 사냥만 6건으로도 가능하다. `eco.DeerEncounter.Mode`의 `pending_damage` / `pending_feeding`은 아직 제출되지 않은 확정 결과 수다. `training` 뒤 `v1 accepted`가 찍히면 첫 정책이 적용된 것이다. `reference_retreat`는 같은 기준 관측을 입력했을 때 부모와 후보 모델의 후퇴 확률이다. `rejected`면 기존 버전을 사용한다. 배치와 결과는 `Saved/DeerEncounterLearning/`에 남는다.
4. **지역 격리와 해제.** 다른 지역의 버전이 그대로인지 확인한다. Online → Frozen → Off로 바꾸고 새 작업이 시작되지 않는지 본다. 학습을 수집하던 중 Off로 전환하면 뒤늦은 결과도 해당 세션에 적용되지 않는다.

## 첫 구현의 실험 범위

확정 결과 학습은 기준 조우에서 후퇴 확률을 피해에 대해 80%, 급여에 대해 12% 방향으로 조정한다. 각 사례의 학습 가중치는 실제 HP 손실 비율 ×3 또는 급여 Energy 비율 ×2이며 최대 1이다. 여섯 건 중 네 건으로 지역 출력층의 후퇴 편향을 학습하고 나머지 두 건으로 후보를 평가한다. 한 배치당 편향 변화는 최대 0.30이다. 확정 결과 학습은 실제 조우 행동에 연결하지 못한 피해에도 작동하고, 상승한 후퇴 확률이 실제 피해를 감소시켰는지는 별도 플레이가 필요하다.

실제 조우의 결정과 보상 표본이 충분하면 기존 PPO 평가 경로도 사용할 수 있다. 직접 피격 후의 반사 도주, 수면, 이주, 포식자 대응 등은 기존 Native 제어를 따른다. 채택 후에는 같은 Play 세션에서 `eco.DeerEncounter.Mode Frozen`을 입력하고, 해당 지역의 평상시 사슴이 사람을 발견했을 때 관찰한다.
