# 플레이어 제공 먹이: 최소 구현 계획과 다음 작업 시작점

> **대체된 지면형 계획 — 아래 시작문으로 개발하지 않는다.** 사용자 후속 요청에 맞춘 [손 먹이·호감도 활성 계획](PLAYER_HELD_FOOD_AFFINITY_PLAN_20261114.md)과 [활성 명세](PLAYER_HELD_FOOD_AFFINITY_SPEC_20261114.md)를 사용한다. 아래 M0~M4와 시작문은 이력이다.

작성 2026-11-14 / **계획만 작성, 모든 구현 단계 미착수**.

실제 개발 계약은 [최소 개발 명세](PLAYER_FOOD_MINIMUM_TRAINING_SPEC_20261114.md)다. 이전 [전체 계획](PLAYER_FOOD_PROVISION_RL_PLAN_20261114.md)은 조사 근거·장기 방향이다. 이번 최소 범위에서는 ON 저장/로드, 손먹이, 호감도, production 후보 모델까지 구현할 필요가 없다. 현재 Source와 루트 불변 계약이 최우선이다.

## 1. 무엇을 완성하고 멈출 것인가

사용자는 Python 담당자가 학습을 시작할 수 있도록 최소 게임 기반을 원한다. 구현자는 **소유→놓기→감지→의도→실제 이동/접촉→섭취→결과**를 작은 fixture에서 완성하고, Python 참조 규칙과 C++ 비교 자료를 전달한다. 그 시점을 `G_GAME`으로 부른다.

실제 학습은 RL 담당이 환경·관측/행동·reward를 연결한 `G_TRAIN` 이후 시작한다. 게임 담당은 인계 전에 단순 empty API나 샘플 JSON으로 끝내지 않고 실제 소비 규칙까지 검증한다. 반대로 기존 모델이 자발적으로 먹이를 찾도록 임의 규칙을 추가하거나 재학습까지 수행하지 않는다.

## 2. 현재 시작 상태

- branch `jwj-1024`, HEAD `ef44df15` 기준. 실행 때 재확인한다.
- 기존 dirty: `EcoWorldV3.h`, `EcoPolicyV3IntegratedProcessors.cpp/.h`, `EcoCreatureInspection.cpp`, `EcoWeaponFireComponent.cpp`, owned/팀 맵 external actor, PROJECT_CONTEXT/PROGRESS와 다른 신규 명세들. 특히 Native 파일과 현재 작업이 겹친다.
- 이번에 작성한 문서는 기능 구현이 아니다. 먹이 전용 기능을 Source 검색 범위에서 찾지 못했으며, Blueprint 전체를 조사했다는 뜻은 아니다.
- 필수 선행 읽기: [PROJECT_CONTEXT](../PROJECT_CONTEXT.md), [RL 담당 가이드](../RL_Policy/ECOSYSTEM_RL_AGENT_GUIDE_20261024.md), [자연 풀 패치 계약](FOOD_PATCH_GRAZING_IMPLEMENTATION_SPEC_20261108.md).
- 현재 Native는 이전 step 끝에 준비한 요청을 다음 step에 실행한다. 새 먹이를 같은 step에 이미 본 것으로 처리하지 않는다.

## 3. 구현 순서

### M0 — 기준선과 작업 경계 고정

- [ ] HEAD/status/diff, 추가 AGENTS, 현재 UE 프로젝트/engine 위치 확인.
- [ ] 소유·입력·Native context·C4·장부·표현·checkpoint 소비자를 조사하고 변경 파일 목록 작성.
- [ ] 기존 `LastIntake`/`FoodEaten` 모든 읽기/쓰기 조사. 자연 기록을 보존할 신규 필드 위치 결정.
- [ ] fixture 설정을 하나의 config로 고정하고 baseline 모델/flags/geometry·단위 manifest 구성.
- [ ] 기존 기능 OFF 기준선 확보. 장시간 전체 QA 대신 변경 경계에 필요한 기존 Golden/자원 테스트 선택.

**완료:** 실제 Source 기준 변경점과 보존 대상이 문서화된다. 이 단계에 모델/관측/reward 변경은 없다.

### M1 — 엔진 비의존 규칙과 비교 도구

- [ ] `EcoPlayerFoodRules` leaf: 유한 source, 만료, 중복 요청, 분배, source/receipt 수지 구현.
- [ ] 명세의 데이터 형식과 ResultCode 구현. generation과 AmountRevision 구분.
- [ ] JSON 기반 C++ CLI, 순수 Python 참조 규칙, 동일 입력 비교 도구 작성.
- [ ] 작은 산술/경계 cases: 재전송, 같은 마지막 수량, 부분 섭취, 2개체 비례 배분, 만료 t−1/t, stale epoch, no demand.
- [ ] 두 구현이 각각 보존식을 검사하고 교차 비교. 같은 버그를 복사한 테스트만 두지 않도록 독립 산술 기대값 포함.

**완료:** Unreal을 실행하지 않아도 공급/섭취 규칙을 재현하고 실패 원인을 확인할 수 있다. 이때 Python 자료를 미리 공유할 수 있으나 UE 접촉/센서 parity와 G_GAME 완료는 아니다.

### M2 — 소유량과 실제 지면 배치

- [ ] PlayerState 소유 수량 component, 기존 입력/능력 경로의 1회 놓기 요청 구현.
- [ ] 현재 소유 Pawn/Avatar/생존·수량·지면·차폐·거리·region·budget 검증, pending 큐 상한/중복 키 구현.
- [ ] Native 경계에서 재검증 후 inventory 차감+source 등록을 원자적으로 반영.
- [ ] owner 수량 및 region source DTO, 간단한 수량 텍스트/먹이 marker 구현.
- [ ] 초기 지급은 episode당1회, 리스폰 재지급 금지. source 생성 실패 시 수량 보존 확인.

**완료:** 플레이어가 10개를 가지고 시작해 1개 놓으면 9개와 실제 source 1개가 남고, 실패/중복 요청이 복제 공급을 만들지 않는다.

### M3 — Native 감지·의도·섭취 연결

- [ ] E~F/H 관측·판단 경계와 맞는 FoodEvidence cache/generation 구현. 초기 reset 관측 포함.
- [ ] test-only `FeedIntent` producer와 실제 실행 consumer 구현. production 자동 target 선정은 기본 비활성.
- [ ] 기존 여행/피난/Social/Life와의 우선순위 결정 및 reject reason 기록. 위치는 C1만 갱신.
- [ ] 실제 이동 후 source 접촉·높이·LOS·유효성 검사, C4 source별 수요·배분·단일 energy/reserve 반영.
- [ ] 자연 LastIntake/FoodEaten 보존, Provision/Total 기록과 자연 흔적/표현 소비자 연결.
- [ ] 기존 Ecology 서비스에 제공 먹이 receipt 연결. 자연 풀 장부 수지 검사 보존.
- [ ] feature ON의 기존 checkpoint/복원 export 명시 거절, OFF 저장 유지. reset/teardown 정리 구현.

**완료:** scripted fixture가 선택한 먹이로 기존 Native 경로를 통해 실제 이동하고, 접촉 후에만 잔량과 영양이 변한다. 이는 학습된 행동의 검증이 아니라 환경 실행 검증이다.

### M4 — 작은 장면 검증과 인계 확정

- [ ] 명세 A01~A14의 결과를 표로 기록. leaf-only와 UE end-to-end 사례를 구분.
- [ ] Standalone 1플레이어/1사슴 + 2사슴 경합. 기본 life/생존 규칙을 꺼서 성공한 것으로 꾸미지 않음.
- [ ] 서버+client1에서 배치·수량·잔량 동기화와 client 논리 writer0 확인.
- [ ] UE 실제 source/감각/이동/섭취 trace를 저장하고 해당 입력을 Python으로 재생. 미지원 지형 범위 명시.
- [ ] `Docs/RL_Policy/Examples/PlayerFood/`에 작은 공용 fixture/manifest/실제 README, `Tools/EcoPlayerFoodEval/`에 비교 도구 정리.
- [ ] 새 clone에 필요한 파일과 실제 실행 명령 검증. Saved의 개인 로컬 파일에만 의존하지 않음.
- [ ] 최종 `handoff_ready=true`, `training_ready=false`와 미완료 RL 연결 항목을 정확히 기록.

**완료: G_GAME.** 여기서 게임 기반 구현을 종료하고 학습 담당에게 전달한다. 사용자 요청 없이 새 weights/장기 학습/온라인 재학습을 시작하지 않는다.

### R1 — RL 담당의 다음 작업 (게임 구현과 구분)

- [ ] 실제 Python baseline 선택 및 제공 먹이 규칙을 env reset/step에 연결.
- [ ] FoodEvidence를 쓸 관측 schema와 action→FeedIntent mapping을 명시적으로 결정.
- [ ] reward/episode 종료·timeout/dead-agent/reset 및 scripted player 입력 연결.
- [ ] 고정 seed 재현·shape·finite values·행동 효과·짧은 rollout 검사.
- [ ] `training_ready=true`로 바꿀 근거 기록 후 승인된 범위의 학습 시작.

**완료: G_TRAIN.** 이 단계의 완료는 기존 R7e를 교체하거나 게임 배포를 승인하지 않는다. 후보 모델 Native 통합은 이후 별도 작업이다.

## 4. 예상 코드 변경 지도

아래 신규 경로는 예정이며 존재 여부를 가정하지 않는다. 실제 연결은 M0 감사에서 확정한다.

| 경로 | 변경 의도 |
|---|---|
| `Source/AdaptiveEcosystem/Player/EcoPlayerFood*.h/.cpp` 신규 | 수량·요청·입력 경로 |
| `Source/AdaptiveEcosystem/AI/Policy/EcoPlayerFoodRules.h` 신규 후보 | engine-free state/배분/수명. UE 전용 구조를 넣지 않음 |
| `AI/Policy/EcoPolicyV3IntegratedSubsystem.*` / 관련 context | 지역 source 상태·권위 큐/geometry 증거 연결 |
| `AI/Policy/EcoPolicyV3IntegratedFragments.h` | 필요한 per-agent 증거/의도/결과 보관. Shared Fragment 금지 |
| `AI/Policy/EcoPolicyV3IntegratedProcessors.*` | GT gather, snapshot/의도 adapter, C4 전후 기록·receipt 연결 |
| `AI/Policy/EcoWorldV3.h` | opt-in source 수요·단일 C4 영양 통합. 기존 OFF 분기 순서/RNG 소비 유지 |
| `Core/EcoResourceTypes.h`, `Ecology/EcologySimulationSubsystem.*` 및 spatial 구현 | 제공 먹이 하위 감사/receipt. 기존 자연 장부 의미 유지 |
| `Creature/Runtime/EcoCreatureLifecycle.cpp` 등 표현 소비자 | Total 섭식 표시, 자연 흔적은 Natural 유지 |
| checkpoint/snapshot exporter | 지원하지 않는 ON 판의 저장/복원 export 거절 |
| `AI/Policy/Tests/` 및 `Tools/EcoPlayerFoodEval/` | 필수 acceptance/CLI/참조/비교/fixture |

복제 transport는 기존 authority Actor/component에 두며 Subsystem에 RPC를 추가하지 않는다. 원래 runtime 모듈을 유지한다. 원본 팀 레벨 입력/에셋을 무조건 재authoring하지 않는다.

## 5. 빌드·검증·결과 보고 규칙

- C++/빌드 입력 변경 시 직접 UBT로 `AdaptiveEcosystemEditor Win64 Development` 빌드하고 종료까지 기다린다. 실제 엔진 경로·쓰기 권한을 확인한 뒤 실행한다.
- UBT/dotnet/UnrealEditor/LiveCoding/MSBuild/ShaderCompileWorker 실행 중에는 중복 빌드를 시작하거나 기존 프로세스를 중단하지 않는다. pure leaf 개발·문서/비교 준비는 가능한 범위에서 진행한다.
- 변경에 맞는 순수 규칙/관련 자동화→Standalone→작은 서버/client 검증 순서로 실행한다. 기능 밖 장시간 전체 테스트를 무조건 요구하지 않는다.
- M1의 CLI 성공은 UBT 성공이 아니며, scripted 성공은 PPO 학습 성공이 아니다. geometry 입력 replay 통과도 전체 지형 궤적 parity가 아니다.
- 학습 담당의 관측·정규화·action/mask·reward·weights를 바꾸지 않는다. 다만 인계용 엔진 비의존 Python 규칙/검증 도구는 이번 범위에 포함한다.
- 이미 존재하는 partial snapshot을 training-ready로 재명명하지 않는다. README 명령은 실제 도구를 구현·실행한 뒤 정확히 기록한다.
- 기본 rollout/GUI에는 개발용 Episode/SourceId를 과하게 노출하지 않고 디버그 패널/로그에 둔다.

최종 보고는 구현한 stage, actual SHA/dirty 상태, 실행한 테스트 결과/조건, 실패/미지원, G_GAME/G_TRAIN 상태, RL 담당이 해야 할 구체적 작업을 포함한다. 실제 완료된 체크박스만 갱신한다.

## 6. 다음 채팅에 붙여 넣을 개발 시작문

> AdaptiveEcosystem의 플레이어 제공 먹이 최소 학습 기반을 구현해 주세요. 먼저 루트 AGENTS.md와 Docs/PROJECT_CONTEXT.md를 읽고 실제 HEAD·branch·diff·추가 지침을 확인하세요. 개발 계약은 Docs/Architecture/PLAYER_FOOD_MINIMUM_TRAINING_SPEC_20261114.md, 실행 계획은 PLAYER_FOOD_MINIMUM_IMPLEMENTATION_PLAN_20261114.md입니다. 전체 조사 계획 PLAYER_FOOD_PROVISION_RL_PLAN_20261114.md보다 최소 범위와 구체적 step 순서는 이 두 문서를 우선합니다.
>
> 목표는 M0~M4를 완료하여 G_GAME(handoff_ready)에 도달하는 것입니다. 먹이1종/정수 수량/지면 놓기/서버 검증/감지 증거/실행 의도/실제 Native C4 소비/결과 trace/engine-free C++ 및 Python 참조 비교를 구현하세요. 학습 전 실행은 test-only scripted intent로 확인하고 production 자동 유인·호감도·새 정책을 추가하지 마세요. Python 학습 env/관측·행동·reward·weights는 담당자 영역이며 인계용 순수 규칙/비교 도구만 작성하세요.
>
> 기존 R7e/V1/R1b·SocialControl=0, 단일 Native 위치/대사 writer, 자연 FoodGrid/M4 수지를 보존하세요. 제공 음식은 출처별 수지로 기록하고 자연 풀 소비/뜯김 흔적으로 계산하지 마세요. source 생성 t→끝 t 증거→최초 t+1 선택 순서와 reset 초기 관측을 지키세요. 기존 사용자 수정·원본 팀 에셋을 보존하고 owned fixture/기본OFF로 검증하세요.
>
> ON 저장 지원은 후속입니다. 이번에는 미지원 저장/복원 export를 명시 거절하고 OFF를 보존하세요. 명세 A01~A14를 검증하고 실제 실행 가능한 패키지/README/manifest를 남기세요. C++ 변경 뒤에는 루트의 직접 UBT·중복 빌드 금지 규칙을 따르세요. 체크리스트는 실제 완료에 맞춰 갱신하고, 최종에는 G_GAME 완료 여부와 G_TRAIN에 남은 RL 연결을 분리해 설명하세요. 이번 요청만으로 실제 학습이나 모델 교체를 시작하지 마세요.

## 7. 이번 문서 작성의 검증 범위

현재 Source의 A/B/C4/E~F/H와 hook·섭식 수식, 기존 플레이어/자원/학습 계약을 확인했다. 새 문서 링크·표기·변경 범위를 검사한다. **코드/에셋 수정·UBT·PIE·학습 실행은 하지 않았다.** 두 gate는 모두 아직 미완료다.
