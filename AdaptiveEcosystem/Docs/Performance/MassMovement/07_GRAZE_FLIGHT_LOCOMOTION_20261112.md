# 평상시 먹이 이동과 긴급 도주 실행 개선 설계

2026-11-12, `jwj-1024 ef44df15` 작업 트리 기준. 상태는 제안이다. [06 진단 명세](06_INSIGHTS_MASS_DEBUGGER_20261112.md)의 결과를 바탕으로 다음 구현을 선택한다. 이번 주제는 행동 지속·먹이 접근/정지·긴급 반응·화면 움직임이며 Avoidance 확장은 별도01/02 명세에 둔다.

## 1. 현재 동작과 개선 목표

현재 Native step은8/60초다. `Prepare`가 잠금/사건으로 재판단을 정하며 잠금 종료 후 같은 행동을 선택해도 잠금을 다시 걸지 않는다. GRAZE를 유지하면서 매 step 먹이 상태에 따라 걷기/정지가 바뀔 수 있다. R7e의 긴급 도주는 WildFlight가 전환을 정하고 길 선택에 정책을 사용한다. 기본 GRAZE→FLEE에는3step 놀람 정지가 있다. 화면은 위치 보간과 Actor 회전 보간을 이미 수행한다.

개선 목표는 평상시 이동 방향·식사 동작의 지속성을 높이고, 긴급 상황의 기존 반응 시점과 목적을 지키는 것이다. 같은 고정 step 안에서도 행동별 제어 상태를 둘 수 있다. 시뮬레이션 step을 늦추는 변경은 첫 개선에 포함하지 않는다.

## 2. 상태 배치 계약

`FMassCustomMovementTag`는 Native 이동 소유 경로를 선택하는 고정 구성, PolicyV3/ThreatV3 Tag는 종별 실행 Query를 선택하는 구성으로 유지한다. 모델 종류는 활성 brain 상태에서 읽는다. 자주 바뀌는 이동 모드·방향·정지 시간은 개체별 상태값으로 둔다.

필요한 신규 상태는 기존 `Rep`, `Control`, life의 `ForageCell`/Residence가 소유하는 값을 먼저 재사용한다. 추가 필드 후보는 이전 유효 방향, 유효 목표 식별자, 마지막 목표 갱신 step, 이동/정지 전이 시각, 적용 이유, context generation이다. 여러 단계에서 지속적으로 공유해야 하면 개체별 Fragment에 두고, 한 step 내 임시값은 scratch로 둔다. 기존 목표를 두 개의 writer가 따로 갱신하지 않는다.

개체별 실행 결과는 Shared Fragment에 저장하지 않는다. 공유 profile에는 조절 계수만 둔다. debug mirror는 읽기 전용으로 만들며, 새로운 실행 기억을 도입하면 저장/복원·runtime image hash·profile 호환성을 함께 정의한다. 동작에 영향을 주는 기억을 재시작 시 조용히 초기화해 동일 복원이라고 주장하지 않는다.

## 3. 변경 후보를 선택하는 규칙

| 진단 결과 | 구현 후보 | 확인할 부작용 |
|---|---|---|
| Behavior/Seq는 유지되지만 방향이 반복 반전 | 평상시 유효 방향 유지·회전율 제한, 기존 GrazeCalm 확장 여부 비교 | 먹이 접근 지연·위협 방향으로 잔류 |
| 같은 셀 주변에서 정지/걷기 왕복 | 이동/정지 hysteresis 또는 짧은 표시 전환 완화 | 먹이 없는 곳에서 섭취·장시간 정체 |
| 목표 셀이 자주 바뀜 | 기존 ForageCell 목표의 유효성/재선택 조건 보강 | 고갈·접근 불가 목표 고착 |
| 논리 변위는 안정적, 화면만 급회전 | 기존 Actor 회전/animation blend 보강 | 미끄러짐·논리와 외형 방향 오차 |
| 실제 행동 전환이 잦음 | 관측/사건/잠금 근거를 RL 담당에게 전달 | 잠금 임의 연장으로 긴급 반응 지연 |
| frame 밀림과 catch-up 때만 불연속 | CPU 병목과 보간 시각 수정 | 시간을 건너뛰어 생태 결과 변경 |

한 번에 원인이 입증된 후보 하나를 구현한다. 임계값은 현행 baseline과 계측으로 정하며 제안 수치를 학습 계약의 확정값으로 취급하지 않는다.

## 4. 평상시 GRAZE 제어 후보

적용 자격은 살아 있는 비여행 개체, 실제 실행 GRAZE, 유효한 현 지역 상태, 긴급/경계/은신처 등 상위 제어가 없는 상태로 제안한다. Life의 기존 이동 보정과 적용 순서를 명시한다. 기본 Control 직후에 평상시 보정을 적용하면 이후 flight/hook이 이를 덮을 수 있으므로, 먼저 기존 writer별 전후 값을 기록하고 최종 삽입 위치를 확정한다.

- 방향 유지가 필요하면 현재 목표가 유효한 동안 이전 방향을 참조한다. 방향이0인 상태에서 normalize하지 않고 기존 정지 처리를 따른다. 최대 회전율은 rad/s 또는 deg/s로 정의해 step 길이를 곱한다.
- 목표 유지가 필요하면 현재 ForageCell/기존 목표를 재사용한다. 고갈·지역 변경·도달 실패·긴급 전환 시 즉시 무효화한다. 평상시 최소 유지 시간은 위 조건보다 낮은 우선순위다.
- 걷기/정지 hysteresis는 두 문턱과 현재 이동 상태로 정의한다. 실제 음식 소비는 기존 실제 위치/자원량 검사를 계속 사용한다. 화면의 먹는 애니메이션만 유지하는 안과 실제 정지 시간을 변경하는 안의 결과를 각각 평가한다.
- 속도 변화 완화가 필요하면 희망 속도와 실제 적용 속도를 구분한다. 가속/감속 한계를 cm/s² 등 명시 단위로 설정한다. 대사·섭식이 적용 속도 또는 Gait 중 무엇에 따라 계산되는지 기존 계약과 대조한다.

이 후보들은 실행 세계를 바꾼다. 같은 관측에서 정책 결과는 유지해도 다음 위치·먹이 경쟁·관측 궤적은 달라질 수 있다. opt-in profile과 Python mirror 영향 기록을 함께 작성하고 RL 계약 범위에서 채택한다.

## 5. 긴급 도주 우선순위

새 평상시 유지 장치는 기존 WildFlight 긴급 전환 시 같은 적용 경계에서 무효화한다. 새 도주 판정기를 추가하지 않고 기존 KeepMode/KeepActive, 실행 행동, 유효한 여행/Social 요청의 우선순위를 따른다. 기존 도주 해제·안전거리·놀람3step·길 선택 조건은 baseline에 포함한다.

도주 반응은 지각 시각, panic 결정, Commit, 놀람 단계, 첫 유효 이동으로 나눠 측정한다. 평상시 방향 유지/최소 정지 때문에 추가 지연이 생기면 해당 보정을 거절한다. 고정 step의 관측/적용 경계와 화면 보간 지연도 따로 기록한다. '즉시'를0ms로 정의하지 않는다.

도주가 끝나면 현재 위치/먹이에서 평상시 제어를 다시 준비한다. 도주 이전의 오래된 목표·방향으로 복귀시키지 않는다. 실제 위치 적분은 기존 Native에서 한 번 수행한다.

## 6. 표시 단계 후보

논리 결과가 안정적인 경우 기존 `ConsumeVisualState`의 yaw 보간·BlendSpace 입력부터 조정한다. 고개와 몸의 전환, 정지/걷기 blend, 실제 속도에 맞는 animation rate를 확인한다. 긴급 진입에는 평상시보다 빠른 표시 전환을 비교할 수 있다.

표시 보정은 Body/정책 관측/장부를 쓰지 않는다. Root Motion을 추가해 별도 이동을 발생시키지 않는다. 서버·Client 같은 이벤트에서 위치/몸 방향·발 미끄러짐·전환 지연을 비교하고 성장/풀 재바인딩/teleport에서 보간 이력을 초기화한다. 현재 Actor 회전 보간을 또 하나의 manager로 중첩하지 않는다.

## 7. 구현 순서와 완료 조건

1. 06을 구현해 baseline trace·선택 개체 시계열·원인 보고를 만든다.
2. 기존 제어 흐름을 동일 결과의 helper로 정리한다. 기능별 입력/출력·적용 이유를 명시한다. 이 단계에서 관측/로짓/요청/실제 상태·RNG 비교 차이0을 확인한다.
3. 확인된 원인에 맞는 후보 하나를 shadow 계산한다. 신규 출력은 비교 기록에만 사용한다. shadow의 추가 CPU 비용도 기록한다.
4. owned fixture opt-in에서 기존 profile과 비교한다. 표시만 바꾸는 경우 권위 결과가 동일해야 한다. 실행 변경은 고정 입력의 정책 결과 동일과 자유 실행의 행동/생태 차이를 각각 검증한다.
5. 성능·품질 결과로 채택 여부를 정하고 기존 기본값 전환 여부를 별도 릴리스 기록에 남긴다.

| 항목 | 초기 acceptance 제안 |
|---|---|
| 평상시 움직임 | 이동 중 방향 반전(연속 방향 내적<0, 정지 제외)/분, gait 왕복/분, 평균 유지시간. 입증된 문제 지표의 baseline 대비30% 감소를 실험 목표로 제안 |
| 음식 접근 | 도달 가능한 fixture의 도착률 하락≤1%p, 도착시간 p95 증가≤10%. 실제 섭취량·에너지 결과도 기록 |
| 긴급 반응 | 평상시 보정으로 인한 panic 적용 추가 지연0step. 기존 놀람 지연을 새 회귀로 집계하지 않음 |
| 안전/상태 | stale 목표 적용0, 단일 writer, 잘못된 lease/여행 개입0, 실제 위치 기반 소비/도착 유지 |
| 비용 | 전체 Native step p95 회귀≤5%를 기존05와 맞춘 초기 목표로 사용. 표시 비용은 GT/GPU 별도 측정 |
| 생태 | 포획·생존·섭식·이주 분포 보고. production 허용 편차는 담당 계약으로 정하며 미정이면 실험 상태 유지 |

수치는 새 측정 결과가 아닌 프로젝트의 제안 gate다. 선택 개체·실험 기간·seed·정지 속도 문턱을 측정 전에 manifest에 고정한다. 자연스러움은 수치와 동일 카메라 비교 영상을 함께 검토한다. 느리게 움직여 반전 수만 줄이는 결과는 도착/섭취 평가에서 확인한다.

C++ 변경은 AGENTS 직접 UBT 및 기존 Policy/Route/Flight/Life/표현 회귀를 따른다. 신규 보정의 긴급 해제·저장 복원·경계 시간 오류는 필요한 회귀 테스트로 검증한다. Standalone 및 Dedicated+Client2/late join에서 확인한다. rollback은 완료 step에서 pending 작업/캐시를 정리한 후 기존 profile로 복귀 또는 재시작하며 저장 호환성을 검사한다.

## 8. 구현 소스와 참고자료

프로젝트 루트 `AdaptiveEcosystem/` 기준으로 다음 파일을 확인한다.

- `Source/AdaptiveEcosystem/AI/Policy/EcoRepertoire.h`의 Prepare/Commit/Control/ApplyMove.
- `Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h`의 DecideHerb/UpdateWildFlight/CommitFlight와 Step A~H.
- `Source/AdaptiveEcosystem/AI/Policy/EcoPolicyV3IntegratedProcessors.cpp`의 IntegratedControlHook/LifeLocomotion와 Visual Processor.
- `Source/AdaptiveEcosystem/Creature/Representation/EcoCreatureRepresentationActor.cpp`의 ConsumeVisualState.
- `Source/AdaptiveEcosystem/Creature/Debug/EcoCreatureInspection.cpp`의 기존 진단 출력.

Craig Reynolds의 GDC1999 원문은 action selection·steering·locomotion의 책임을 나누는 근거로 참고한다. 이번 설계에서 정책 선택과 이동 실행·표현의 조정 지점을 구분하는 데 적용했다. 본문의 구체적인 상태·문턱·테스트 수치는 프로젝트 제안이다. [Steering Behaviors For Autonomous Characters](https://www.red3d.com/cwr/steer/gdc99/).

Epic의 MassEntity 설명은 구성과 Query 처리, Trace/Mass Debugger 문서는 관측 도구에 대한 근거다. Epic이 이 프로젝트의 이동 계수나 RL 호환성을 검증했다는 의미는 아니다. [MassEntity Overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine?application_version=5.8), [Mass Debugger Overview](https://dev.epicgames.com/documentation/unreal-engine/mass-debugger-overview?lang=en-US).

다음 구현 지시: “06의 원인 보고를 읽고07의 후보에서 근거가 확보된 하나를 구현한다. 기존 정책 판단과 긴급 반응을 보존하며 shadow→owned opt-in 비교를 수행하고 실제 실행 변화·Python mirror 영향을 기록한다.”
