# 1102 사슴 StateTree 수동 확인

대상은 `/Game/JWJ/1102_Integration_JWJ`다. 1102에 이미 배치된 coordinator, 지역, 스폰, 먹이터와 늑대를 사용한다. 시험용 트리는 `/Game/Experiments/SpeciesAI/V3Deer/ST_V3Deer`다. [개발 명세](JWJ_1102_DEER_STATETREE_SPEC_20261125.md)

## 1. 에디터에서 켜기

1. 빌드된 에디터로 1102를 연다. **Play 전에** 에디터 Output Log의 콘솔에서 `eco.V3Deer.StateTree 1`을 입력한다. 기존 coordinator의 **Use V3 Policy**를 켜 둔다.
2. Play Net Mode를 **Play Offline**으로 두고 시작한다. Output Log의 `[V3DeerDecision] Backend=StateTree Tree=...`와 `Pinned world mask contract=...`를 확인한다. 후자는 도주 경로·행동 허용 범위를 정하는 v3 계약 이름이다.
3. 게임 콘솔에서 아래 명령을 실행한다.

```text
eco.V3Deer.Status
eco.M4.Dump details
eco.Integration.Dump details
```

정상 시작 시 `Backend=StateTree Active=1`과 초기화된 지역 수가 나온다. 조금 기다린 다음 `ActionSelections`가 증가하고 `ActionPPO=0 RoutePPO=0 MaskViolations=0 Fallbacks=0 Fatal=0`인지 본다. 일반 선택이 한 번도 없거나 `Active=0`이면 아직 이 장면을 검증한 상태가 아니다.

별도 프로세스에서 1102를 시작할 때는 `-EcoDeerDecision=StateTree`를 쓴다. 명령줄이 에디터 CVar보다 우선한다. 에디터에서 PPO로 비교하려면 Play를 종료하고 `eco.V3Deer.StateTree 0`을 입력한 뒤 1102를 다시 시작한다. 명령줄로 StateTree를 지정한 프로세스라면 다음 실행에서 해당 인자를 빼거나 `-EcoDeerDecision=PPO`를 쓴다. Play 중에 CVar를 바꿔도 현재 월드의 선택 모드는 유지된다.

## 2. 한 마리의 선택과 이동 대조

`eco.M4.Dump details`를 보고 `JWJ_WildlifeTest`의 살아 있는 사슴 ID를 찾는다. 아래 숫자 123은 실제 ID로 바꾼다.

```text
eco.V3Deer.Status 123
eco.V3Deer.TraceId 123
eco.MoveTest Observe 123 300
```

`TraceId`는 그 사슴의 선택만 Output Log에 기록한다. `Observe`는 기존 실제 개체의 요청·실행·이동을 기록하는 수동 관찰 명령이다. 종료 로그에서 보고서 경로를 확인한다. 끝나면 `eco.V3Deer.TraceId 0`을 입력한다. [기존 이동 관찰](../../Performance/MassMovement/10_MOVE_TEST_OBSERVE_GUIDE.md)

| 장면 | 확인할 결과 |
|---|---|
| 먹이 접근·섭식 | 사슴이 기존 먹이터 주변을 이동한다. `Channel=Action Request=0`은 GRAZE 요청이다. 이동 기록의 실제 위치와 섭식, `eco.Integration.Dump details`의 지역 Food를 함께 확인한다. 기존 주변 먹이 공급·소비가 함께 일어나면 Food 총량 변화만으로 개체 섭취를 판정하지 않는다. |
| 실제 늑대 접근 | 사슴이 Native 도주 반사에 들어가고 실제 방향·속도가 달라진다. 반사만 실행된 스텝에는 일반 Tree 선택 로그가 없어도 된다. |
| 도주 경로 | `Channel=Route`와 `RouteSelections` 증가를 확인한다. 이 채널에서 1은 RUN, 4는 SHELTER다. Action에서 4는 SLEEP이며 마스크가 허용할 때만 후보가 된다. 경로 선택이 한 번도 없었다면 도주 동작만 확인한 것이다. 엄폐 접근이 있는 다른 도주 장면에서 다시 확인한다. |
| 위협 해소 후 복귀 | 같은 ID가 생존해 이동하고 먹이를 다시 소비한다. 늑대가 포획한 장면은 별도로 기록해 사망 확정과 인구 변화가 한 번인지 확인한다. |
| 세션 종료 직전 | `eco.V3Deer.Status`에서 일반·경로 PPO 호출이 둘 다 0인지 확인한다. 순수 PPO 비교 실행에서는 `Backend=PPO`와 일반 추론 횟수 증가를 확인한다. |

한 지역에서 확인한 뒤 기존 5개 지역으로 확인 범위를 넓힌다. 새끼의 첫 판단과 이주 전후 동일 ID를 확인한다. Dedicated와 Client 2개는 **Play As Client / Number of Players=2**로 시작하고 위 명령은 서버에 입력한다. 분리 프로세스를 사용하면 서버 실행 인자에 `-EcoDeerDecision=StateTree`를 전달한다.

## 3. 현재 지원 범위

- Native v3가 행동 잠금과 반사, 이동 적분, 먹이 거래를 소유한다. StateTree는 Native가 요청한 결정마다 루트를 다시 선택한다. 1102의 기존 Actor 배치로 시작한다.
- StateTree 모드는 기본 v3 두뇌의 **허용 마스크 계약**을 시작 시 고정한다. 두 길 계약을 적용할 수 없는 세계에서는 기존 내장 계약을 사용한다. 학습 파일 교체·실시간 적응은 이 모드의 행동 선택에 적용되지 않는다.
- StateTree 모드의 생태 체크포인트 저장·복원은 아직 지원하지 않는다. 기존 PPO 세션은 기존 저장 형식을 사용한다.
- 에셋 로드 실패로 시작이 거부되면 Output Log의 `Startup rejected`와 트리 경로를 확인한다. 에셋 제작에 사용하는 명령은 `UnrealEditor-Cmd.exe <프로젝트.uproject> -run=EcoSpeciesAIAssets -V3Deer -unattended -nop4 -NullRHI`다. 이 명령은 전용 트리를 컴파일해 저장하거나 기존 저장 자산을 검사한다.
