# 손 먹이·지역 사슴 평판 구현 계획과 Python 인계

게임 코드 적용과 현재 검증 범위는 [에디터 설정 안내](../Player/HAND_FOOD_EDITOR_SETUP_20261114.md)를 따른다. 이번 요청에서 자동화 테스트, Python 환경 연결, 학습은 생략한다. 아래 H0~H5는 최초 계획이며 실행 완료 기록은 아니다.

2026-11-14 / **현재 활성 계획, 구현 미착수**. 사용자는 지면 배치가 아닌 **소유하고 손에 든 먹이의 지각, 키로 실제 급여, 급여/무기 피해에 따른 지역×종의 전체 플레이어 대상 평판**을 요청했다.

계약은 [손 먹이·호감도 개발 명세](PLAYER_HELD_FOOD_AFFINITY_SPEC_20261114.md)다. 이전 PLAYER_FOOD_MINIMUM_* 및 PLAYER_FOOD_PROVISION_RL_PLAN은 지면형 검토 이력이며 현재 실행 지시가 아니다.

후속 결정으로 플레이어별 평판을 지역·종별 인간 공통 Reputation으로 대체했다. 1번 키/오른손 Socket/기본 Sphere를 사용하고, 종별 설정은 분리하되 처음에는 사슴만 활성화한다. 규칙판은 `held_food_regional_species_human_reputation_v3`. **같은 지역의 처음 만나는 사슴도 공통값을 읽는 것이 필수 조건**이다. 실제 팀 전달 절차·복사 가능한 메시지는 [RL 단계별 인수인계](../RL_Policy/PLAYER_INTERACTION_RL_HANDOFF_20261114.md)를 따른다.

## 1. 개발 완료 지점

게임 담당은 다음 흐름을 완성해 `G_GAME`에서 인계한다.

```text
소유 수량 → 손 Food 상태 → 사슴의 시각 증거
                         ↓
플레이어 키 요청 → 실제 접촉/식욕/행동 검증 → Native 한입 섭취
                                             ↓
                                     받은 사슴의 지역·종→전체 플레이어 Reputation 증가

총/근접 실제 피해 commit → 공격자 인식/Unknown 구분
                         → 피해 사슴의 지역·종→전체 플레이어 Reputation 감소

위 규칙 + 센서 + 의도/결과 + 사건순서 → Python 참조/동일입력 비교
```

RL 담당은 이 규칙을 학습 env에 연결해 관측·행동·reward·reset을 정하고 `G_TRAIN`에서 학습을 시작한다. **호감도 갱신 규칙은 게임 담당, 그 호감도를 읽고 행동하는 정책은 RL 담당**이다. 게임 담당이 관계값만 추가하고 현재 R7e가 이를 이미 쓴다고 보고하지 않는다.

### 인계 범위와 브랜치 전달

H0~H5가 사용자의 개발 완료 지점이다. 장착·감각·섭취·피해·공통 평판이 실제 동작하고, 규칙·입출력·재현 사례를 전달할 수 있어야 한다. 학습된 접근/회피 모델 완성은 RL 담당 작업이다. H1의 규칙·fixture는 먼저 공유하여 병행 작업할 수 있다.

Python 담당자가 전체 게임 브랜치를 병합할 필요는 없다. 공통 기준 SHA와 기능에 한정한 commit/patch, 규칙 reference/config/cases를 전달한다. Python 환경에서 동일 규칙을 구현·비교하는 데 필요한 범위부터 가져가고, UE 실행 검증이나 최종 runtime 통합 시 필요한 게임 commit을 함께 사용한다. 문서만으로 실제 학습-ready를 선언하지 않는다.

## 2. 시작 상태와 선행 확인

- 감사 기준 `jwj-1024`, HEAD `ef44df15` + 기존 Native/무기/Inspector/맵/문서 dirty. 매 작업 시작에 실제 branch·HEAD·diff를 다시 본다.
- 필수 읽기: 루트 AGENTS, [PROJECT_CONTEXT](../PROJECT_CONTEXT.md), [RL 담당 안내](../RL_Policy/ECOSYSTEM_RL_AGENT_GUIDE_20261024.md), 활성 명세.
- 현재 장비 component는 무기 전용이며 BaseDamage/총구 등의 검증을 한다. 먹이를 무기 DataAsset로 가장하지 않는다.
- 총기/콤보의 Committed와 이후 DeathCommitted, 직접 strike 경로를 함께 감사한다. 단일 실제 damage commit에서 relation event를 내야 한다.
- 기존 FMemory는 player별이 아니다. 지역 ReputationKey/세션 identity와 관측 local_handle을 별도로 설계해야 한다.
- Native step과 resource step의 순서를 확인한다. 피해 처리 위치를 옮겨 새 기능을 쉽게 구현하지 않는다.

## 3. 단계별 작업

### H0 — 계약·baseline 고정

- [ ] 현재 Source/추가 AGENTS/dirty 변경과 담당 경계 확인.
- [ ] IMC 키·기존 무기 저장 상태·PlayerState 리스폰·장비 grant/취소 경로 감사.
- [ ] 기존 KnownHumanDanger의 소비자/인식 범위와 신규 평판의 차이를 감사. 기존 반사를 score로 덮거나 같은 피해를 두 번 적용하지 않음.
- [ ] damage writer/결과 publisher와 모든 LastIntake/FoodEaten 소비자 목록 작성.
- [ ] 논리 hand/mouth anchor와 Native/GT 경계 결정. dedicated 서버 pose와 독립적인 위치 규칙 고정.
- [ ] 2region·최대2player·여러 사슴 fixture·한입·관계식·감각/시간·기존 active world config export 및 OFF baseline 준비.

완료: 사용자 수정 보존 계획과 새로운 코드 접점이 명확하고, 기존 모델/정규화·mask·reward를 변경할 필요 없이 규칙 개발을 시작할 수 있다.

### H1 — 순수 규칙·지역 평판·참조 비교

- [ ] engine-free inventory/hand generation/feed request/한입 commit 규칙.
- [ ] 종별 opt-in/허용 먹이/관계 계수 설정. 사슴만 실행 활성화, 독립 SpeciesId 순수 규칙 격리 검증.
- [ ] ReputationKey별 값·clamp·인식 피해·positive lockout·event dedup·reset.
- [ ] Ecology 소유 `(WorldEpoch,RegionId,SpeciesId)` 장부와 권위 membership/revision snapshot. event1회 갱신과 여러 개체의 read를 구분.
- [ ] 같은 지역 새 사슴/다른 지역/다른 종/출생/지역 소속 인계/접경 사건/목격 재전파의 기대값 검증.
- [ ] Source시간/AuthoritySequence/Native관측시점 replay 계약과 순서 검사.
- [ ] C++ CLI + Python reference + 고정 fixture/비교 도구.
- [ ] A급여/B공격, Unknown, Committed/DeathCommitted중복, 마지막1개경합 등 독립 산술 기대값 검증.

완료: UE 없이 게임의 급여/호감도 규칙을 동일 입력으로 재현. RL 담당은 이때 규칙 검토를 시작할 수 있으나 G_GAME은 아니다.

### H2 — 실제 손 먹이와 센서

- [ ] PlayerState 서버 소유 수량/PlayerKey와 현재 Pawn HeldState/OfferGeneration 구현.
- [ ] 기존 장비에 Food/Weapon 전환 조정자 연결. 무기 상태 보존, 식량을 들 때 조준/발사/근접 서버 차단.
- [ ] owned IA_EquipFood를 숫자1에 binding, 오른손 FoodGripSocket의 기본 Sphere·수량 HUD. IA_InteractFeed는 별도 키로 IMC 감사 후 확정. 중복 키·socket 누락·무기 전환 검증.
- [ ] 논리 anchor와 시각 socket 정렬 검증, Client relevance/렌더 LOD가 논리 상태에 영향0.
- [ ] player별 시각/hand food evidence + reputation mask/local_handle. 행 재정렬과 player 식별 보존 검증.

완료: 가방/손/빈손/무기 전환이 다르게 관측되고, 단순 장착은 관계값/영양을 바꾸지 않는다. 시야 밖 사람의 정보를 유출하지 않는다.

### H3 — 키 급여와 C4 자원 거래

- [ ] 서버가 검증하는 근접 사슴 후보/target selection과 상호작용 키 구현. CanAttack/weapon proxy 의존 방지.
- [ ] 요청 dedup/기한/pending 예약/Avatar·Offer 변경 취소 구현.
- [ ] C4 실제 접촉/행동/식욕 재검증 후 한입, inventory−1, hand-feed receipt.
- [ ] 성공량에 대응한 관계+5/lockout0을 실제 event로 연결. 키/거절/예약만으로 관계를 올리지 않음.
- [ ] 자연 섭식의 배분/표현/흔적/장부와 제공 섭식 분리. 단일 energy/reserve writer 유지.
- [ ] 2명의 같은 사슴 요청 경합과 1명의 마지막1개 중복 요청 검증.

완료: 보이는 손 먹이를 실제 키로 가까운 사슴에게 줄 수 있고, 실제 성공 때만 수량·영양·지역 평판이 일치한다. 자동 사슴 유인 규칙은 없음.

### H4 — 실제 피해와 관계 연결

- [ ] ApplyPlayerDamage의 공통 확정 경계를 확장하여 AppliedDamage/MaxHP/Known/PlayerKey/event provenance 제공.
- [ ] 총기/근접 combo/직접 strike 모두 내부 PlayerHarmCommitted를 정확히1회 생산.
- [ ] 지역 평판 테이블은 이 authoritative event만 소비. UI/Client delegate/DeathCommitted는 별도 mutation 금지.
- [ ] Unknown 피해는 기존 기억 경로 유지, 지역 공통 평판 차감0.
- [ ] 현재 combat/resource/Native 순서를 보존하고 feed/damage event order capture.

완료: 실제 총기 피해가 올바른 지역·종의 전체 플레이어 대상 평판에 반영된다. friendly player와 attacker가 달라도 같은 지역·종의 점수를 공유하고, 실제 원인/개인 inventory는 구분하며 lethal 결과가 두 번 반영되지 않는다.

### H5 — 작은 게임 검증·학습 패키지 인계

- [ ] 명세 H01~H25 결과를 pure-rule/UE실제/네트워크 범위별로 기록.
- [ ] Standalone + server/client2에서 A급여/B공격, 역할 교체, 무기/food 전환, 이동·취소/리스폰 검증.
- [ ] A의 공격 후 B/C도 같은 지역·종의 악평 적용, B의 급여로 공통 잠금 우회 불가, 새 접속/로그아웃으로 초기화 불가.
- [ ] R1의 D1에게 급여→R1의 첫 조우 D2가 같은 평판 조회, R2 D3는 중립 유지. D2가 직접 사건/위험 좌표를 받은 것처럼 만들지 않음.
- [ ] ON 기존 checkpoint/복원 export 명시 거절, OFF 기능/저장/모델·무기 회귀 확인.
- [ ] 작은 capture/replay와 C++/Python 차이 보고서. 숨은 지형/animation이 mirror에 없으면 범위 제한 기록.
- [ ] 새 clone에서 실제 실행 가능한 도구/설정/README/manifest와 RL 담당 작업 목록 작성.
- [ ] `handoff_ready=true`, `training_ready=false`로 게임 인계 완료. H0~H5 체크리스트는 실제 통과 때만 변경.

완료: **G_GAME**. 호감도 게임 규칙/실제 이벤트와 학습 연결 기반이 준비됐다. 아직 학습된 지역 평판 행동/새 모델 배포 완료가 아니다.

### RL — 학습 담당 연결과 실행

- [ ] 실제 baseline 선정: 기존 env_v2 world/r8 등 후보 중 Source/모델 계약으로 고정.
- [ ] 관측 후보K2·identity handle·reputation_known·held_food_visible·피해인식/기존위험 입력의 schema/정규화/결측 결정.
- [ ] 플레이어 목표 접근·거리유지/해제와 기존 repertoire action의 mapping 결정. 필요하면 별도 버전.
- [ ] scripted player state machine: Food/Weapon/빈손 전환, 접근/이탈, 급여 키, 실제 피해/Unknown 시나리오.
- [ ] reward와 reset/종료/timeout/dead-agent 처리. Reputation값을 자동 reward로 간주하지 않음.
- [ ] 짧은 rollout/seed 재현/finite obs·reward/동작효과 검증 후 `training_ready=true`.
- [ ] 역할/ID/후보행을 바꾼 holdout, 친절→공격 전환·Unknown·복수player·포만/위협을 평가.
- [ ] 최초 관측 사슴의 접근을 필수 평가. 손 먹이 유무×지역 평판을 교차하고, 같은 위치/식욕/위험에서 score만 바꾼 대응 결과를 비교. 다른 사슴의 급여 보상을 전체 사슴에게 중복 지급하지 않음.

완료: **G_TRAIN**. 훈련량/장비/새 가중치 선택·export는 RL 담당의 별도 승인된 업무이며, 이번 게임 구현에 포함하지 않는다.

## 4. Python 전달 패키지 재정의

예정 배치: `AdaptiveEcosystem/Tools/EcoPlayerInteractionEval/`에 C++ 평가기/Python 참조·비교, `Docs/RL_Policy/Examples/PlayerInteraction/`에 작은 계약/config/manifest/cases. 원시 실행 산출물은 `Saved/PlayerInteraction/`에 둔다. 아래 파일은 구현 단계에서 만들어야 하며 현재 존재한다고 가정하지 않는다.

| 산출물 | 기존 지면형 대비 달라지는 내용 |
|---|---|
| `interaction_contract_v3.md` | 손 상태·한입·지역 평판·인식 피해·동일시각 순서 |
| `interaction_config_v3.json` | 양·급여거리·anchor·전환·관계식·lockout·baseline 감각/대사 |
| `interaction_reference.py` + C++ CLI | inventory/held/feed/reputation/event-order의 순수 규칙. 학습 env는 별도 |
| `cases.jsonl` | 실제 소비/HP 감소/인식/관계 기대값과 실패·중복·2player 사례 |
| `player_scripts.json` | 관측 가능한 시각 장비/gesture와 실제 급여/피해 결과를 구분한 환경 입력 |
| `replay.jsonl` | initial state + geometry·감각 + feed/damage commits + reputation + policy request/executed/override |
| `compare_interaction.py` | 사건 순서·identity·급여 수지·호감도 변화의 첫 divergence |
| `manifest.json` | SHA/dirty hash, UE·native/설정/geometry/모델·schema 지문, supported/unsupported |
| `README.md` | 실제 실행 명령, 예시 출력, G_GAME/G_TRAIN 상태, RL 연결해야 할 API |

기존 partial snapshot이나 지면 source 예제를 이름만 바꿔 재사용하지 않는다. per-step 모든 raw state를 actor observation으로 넣지 않는다. 정책에 들어갈 수 있는 정보와 서버 정답/감사 자료의 필드를 문서와 타입에서 분리한다.

## 5. 코드 변경 지도와 불변 계약

신규 명칭은 구현 후보이며 H0에서 최소 변경으로 확정한다.

| 위치 | 작업 |
|---|---|
| `Player/EcoPlayerFoodInventoryComponent.*` 신규 | 소유량·PlayerKey·grant/급여 debit |
| 기존 Equipment와 신규 held-food/interaction component | hand mode·무기 전환/suspend·권위 키 요청·socket 표현 |
| `AI/Policy/EcoPlayerInteractionRules.h` 신규 | 엔진 비의존 관계·급여·이벤트 규칙 |
| 기존 Integrated context/fragments/processors | 감각 snapshot/local_handle, 지역 reputation 읽기 snapshot/개별 의도, 단일 writer adapter |
| 기존 Ecology 서비스 내부 평판 테이블 | event region/species 고정, 지역·종별 공통 score/revision·공통 잠금, 개체 생명주기와 독립된 수명 |
| 기존 `EcoWorldV3.h` C4 | 자연/hand-feed 출처 결정, 한 번의 영양/비축 반영 |
| `EcoCreaturePlayerOutcomes.cpp` 및 Combat/Lifecycle 경계 | 확정 피해 내부 사건, 기존 Known 재사용, 중복 mutation 방지 |
| 기존 Ecology/resource types | hand-feed 보존 receipt, 자연 장부 의미 유지 |
| checkpoint/snapshot export | 미지원 ON 판 거절, OFF 보존 |
| 테스트/owned 입력·fixture | 실제 키/총/멀티/감각·자원/관계 acceptance |

FEcoCombatDamageOutcome.StepId(공격 stage)와 NativeStep을 혼용하지 않는다. StableAgentId/PlayerKey를 영속 Actor pointer로 바꾸지 않는다. 평판은 Ecology의 지역·종 테이블에 두고 개체별 판단은 per-agent snapshot으로 읽는다. 개체별 정책 출력을 Shared Fragment로 옮기지 않는다.

Source 수정 전 Core/AI Policy/Mass 계약을 대조한다. C++/빌드 입력 변경 후 직접 UBT `AdaptiveEcosystemEditor Win64 Development`를 완료까지 검증하고, UBT/dotnet/Editor/LiveCoding/MSBuild/ShaderCompileWorker 실행 중 새 빌드/임의 중단을 하지 않는다. 현재 설치 엔진/권한·실행 경로를 확인한다.

## 6. 다음 개발 요청에 붙일 시작문

> 손에 든 먹이와 지역·종별 인간 공통 평판과 손 먹이 최소 기반을 구현해 주세요. AGENTS.md와 Docs/PROJECT_CONTEXT.md, 활성 Docs/Architecture/PLAYER_HELD_FOOD_AFFINITY_SPEC_20261114.md 및 PLAYER_HELD_FOOD_AFFINITY_PLAN_20261114.md를 먼저 읽고 현재 branch/HEAD/diff·추가 지침을 확인하세요. 이전 PLAYER_FOOD_MINIMUM_*의 지면 먹이 계획은 대체된 이력입니다.
>
> 목표는 H0~H5 완료와 G_GAME 인계입니다. 숫자1로 오른손 Socket에 기본 Sphere 먹이 장착 및 Food/Weapon 전환, 사슴의 실제 시각 증거, 특정 키의 권위 급여 거래, Native C4 한입 섭취·소유량 감소, 지역×종의 전체 플레이어 대상 공통 평판, 실제 피해 commit 기반 감소, C++/Python 순수 규칙/재생 비교를 구현하세요. 기존 개체별 FMemory Fear와 지역 Reputation를 구분하고 Unknown 공격자에게 전지적 개인 귀속을 하지 마세요. 단순 장착/키/피해Queued/DeathCommitted로 이중 갱신하지 마세요.
>
> 무기 상태·사용자 에셋·Native 단일 위치/대사 writer·자연 풀 M4 장부·R7e/V1/R1b·SocialControl=0을 보존하세요. 먹이를 가짜 무기 DataAsset로 넣거나 지면 source를 추가하지 마세요. 관측/행동/mask/reward/weights는 RL 담당 영역이며 인계용 Python 참조/비교 도구만 작성하세요. 현재 모델이 관계를 학습했다고 주장하지 마세요.
>
> owned fixture와 기본OFF에서 검증하고 코드 변경 후 직접 UBT/중복 빌드 금지 규칙을 따르세요. H01~H25을 실제 결과에 맞춰 기록하고 새 clone에서 실행 가능한 패키지와 G_TRAIN에 남은 RL 연결을 설명하세요. 이 요청만으로 대규모 학습·모델 교체·상용 저장/계정 시스템을 시작하지 마세요.

## 7. 이번 계획 수정의 결과

기존 장비/총기·근접·직접strike/피해결과·Native 기억·자원 Source를 재확인했다. 이 명세와 계획, 이전 문서의 대체 안내, 공통 진입 링크만 갱신한다. **기능 구현·UBT·PIE·학습은 실행하지 않았다.**
