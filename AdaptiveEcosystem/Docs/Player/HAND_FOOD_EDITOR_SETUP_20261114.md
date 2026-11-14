# 손 먹이와 지역·종 평판 에디터 설정

UE 5.8의 Native v3와 Ecosystem Life를 쓰는 테스트 레벨에서 설정한다. 현재 실행 경로는 Native 초식 개체를 지원하며 팀 레벨에서 사용하는 사슴의 Species ID는 보통 `Herbivore`다. 실제 Mass Entity Config의 Identity 값을 우선한다. 이 문서의 설정은 테스트 레벨, Pawn, GameMode를 선택해서 적용할 수 있다.

## 1. 에디터에서 준비하기

1. **먹이 Data Asset.** Content Browser에서 **Miscellaneous → Data Asset → EcoHandFoodDefinition**을 선택한다. 예를 들어 이름은 `DA_TestHandFood`, **Food Id**는 `TestFruit`로 설정한다. **Show Engine Content**를 켜고 **Mesh**에 `/Engine/BasicShapes/Sphere`를 지정한다. 움직이며 풀을 뜯는 사슴도 시험하려면 **Units Per Bite**를 `0.01`로 시작한다. 정지 중 먹는 사슴에는 `0.04`를 시험할 수 있다. 실제 허용량은 해당 스텝의 식욕과 기존 섭식 상태에 따른다.
2. **PlayerState.** 테스트 GameMode의 Player State Class에 `EcoPlayerState` 또는 그 자식 Blueprint를 사용한다. 직접 값을 설정하려면 자식 Blueprint를 만들고 Class Defaults의 **Hand Food**에 Data Asset, **Starting Hand Food Count**에 `10`을 지정한다. GameMode에서 이 PlayerState를 선택한다. 수량은 PlayerState가 처음 시작할 때 한 번 지급된다.
3. **오른손 표현.** Pawn에 **Eco Hand Food** 컴포넌트를 추가한다. 실제 Skeletal Mesh의 오른손 본, 예를 들어 `hand_r`에 소켓을 만든다. 컴포넌트의 **Food Grip Socket**과 이름을 맞춘다. 초기 이름은 `socket_Food_R`이다. **Visual Offset**은 Sphere의 크기와 손에서의 위치를 조정한다. **Hand Offset Cm**은 Pawn 원점 기준 서버 접촉점이다. 플레이어가 바닥 위에 서 있는 상태에서 손과 입이 맞닿도록 필요에 따라 두 값을 따로 조정한다.
4. **입력 연결.** 아래 클릭 순서에 따라 Input Action과 Pawn의 이벤트를 연결한다.
5. **지역·종 규칙.** 아래 클릭 순서에 따라 Spawner의 사슴 프로필을 추가한다.
6. **감각·전투.** Pawn에 기존 **Eco Player Ecology** 컴포넌트를 두고 **Threat Enabled**를 켠다. 사슴의 사람 감각이 이 등록을 사용한다. 총까지 시험할 때는 기존 총 세팅, **Enable Player Combat** 및 해당 맵의 전투 등록을 확인한다. 급여는 늑대가 없는 곳에서 먼저 성공시킨다.

### 1.1 입력을 Pawn Event Graph에 연결

1. 레벨에서 **World Settings → Game Mode → Selected GameMode → Default Pawn Class**를 확인한다. JWJ 설정을 쓰면 `/Game/JWJ/Gameplay/BP_EcoPlayer_Common`이 예시다. `Common` 폴더에도 같은 이름의 Pawn이 있으므로 경로를 확인한다.
2. Content Browser에서 **Add → Input → Input Action**으로 `IA_EquipHandFood`, `IA_Feed`, `IA_StowHandFood`를 만든다. 각 에셋을 열어 **Value Type → Digital (bool)**을 선택하고 저장한다. 단순 키 누름 예제는 Triggers를 비워 둔다.
3. 현재 Player Controller에 등록된 **Input Mapping Context**를 연다. JWJ의 예시는 `/Game/JWJ/Gameplay/IMC_EcoAbilities_Common`이다. **Mappings** 옆 **+**를 눌러 세 Action을 추가한다. 각 행을 펼쳐 `IA_EquipHandFood → 1`, `IA_Feed → E`, `IA_StowHandFood → 2`로 키를 지정하고 저장한다. `E`와 `2`는 비어 있을 때의 예시이며, JWJ의 `F`는 콤보에 쓰인다. Player Controller의 **Class Defaults → Default Mapping Contexts**에서 활성 IMC 목록을 확인하고, 해당 키가 이미 쓰이면 테스트용 설정에서 충돌을 해소한다.
4. Pawn을 열고 **My Blueprint → EventGraph**로 이동한다. 빈 곳을 우클릭하고 `IA_EquipHandFood`를 검색해 **Enhanced Action Events**의 이벤트 노드를 선택한다. 이벤트 노드에서 **Started**가 안 보이면 노드 아래 작은 펼침 화살표를 클릭한다.
5. 왼쪽 **Components**의 **Eco Hand Food**를 그래프로 끌어와 컴포넌트 참조를 만든다. 파란 출력 핀에서 선을 끌어 `Equip Hand Food`를 검색해 호출 노드를 만든다. 파란 선은 함수의 **Target**에 연결된다. 이벤트의 **흰색 Started 출력**을 호출 노드 왼쪽 **흰색 실행 입력**에 연결한다.
6. 같은 방식으로 `IA_Feed`의 **Started**를 **Request Hand Feed**에, `IA_StowHandFood`의 **Started**를 **Stow Hand Food**에 연결한다. 모두 같은 **Eco Hand Food** 컴포넌트를 Target으로 사용한다. **Compile → Save**를 누른다. 예제 매핑에서는 `1`로 장착, `E`로 급여, `2`로 수납한 뒤 기존 총 입력을 사용한다.

### 1.2 레벨 Spawner에 사슴 프로필 추가

1. PIE를 종료한다. 레벨 **World Outliner**에서 실제 사슴을 스폰하는 **Eco Creature Integration Spawner** 인스턴스를 선택한다. 이름이 다르면 선택한 액터의 클래스와 `Herbivore Config`를 확인한다.
2. **Details** 검색창에 `Use V3 Policy`를 입력해 체크한다. 검색을 지우고 `Enable Ecosystem Life`를 찾아 체크한다.
3. **Details**에서 `Player Interaction Profiles`를 검색한다. 속성 오른쪽 **+**를 눌러 항목을 하나 추가하고 화살표로 펼친다. **Key**에 `Herbivore`를 입력한다. 현재 Native 사슴 스폰에서 사용하는 종 ID다.
4. 검색 때문에 내부 필드가 안 보이면 검색창을 지운다. 추가한 **Value**를 펼치고 **Accepted Food Ids** 옆 **+**를 누른다. 첫 값에 `TestFruit`를 입력한다. 실제로는 PlayerState에 지정한 먹이 Data Asset의 **Food Id**와 같은 값을 쓴다. 예제 에셋 이름은 `DA_TestHandFood`이고 그 안의 **Food Id**는 `TestFruit`다.
5. 첫 시험에는 아래 기본값을 그대로 두고 레벨을 저장한다. 다른 지역에서도 같은 사슴 프로필을 사용하며, 실행 중 쌓이는 점수는 각 지역과 종별로 따로 관리된다.

| 필드 | 첫 시험 값 |
|---|---:|
| Initial Score / Minimum Score / Maximum Score | 0 / -100 / 100 |
| Feed Gain / Damage Scale / Positive Lockout Steps | 5 / 60 / 75 |
| Mouth Offset Cm (X, Y, Z) | 30, 0, 75 |
| Max Body Distance Cm / Max Bite Distance Cm / Max Bite Height Difference Cm | 200 / 100 / 50 |
| Eye Height Cm / Sight Range Cm | 75 / 1600 |

예를 들어 평판이 5인 지역에서 사슴이 공격자를 인식한 피해가 30/100 HP라면, 18점이 깎여 -13이 된다. 이후 75 Native 스텝 동안에도 성공한 급여는 먹이를 소비하지만 점수는 올리지 않는다.

급여 결과는 **On Feed Completed**에서 받을 수 있다. 처음에는 Receipt를 **Break Eco Hand Feed Receipt**로 읽어 `Result`, `Target Id`, `Actual Food`, `Reputation Delta`를 Print String으로 표시하면 된다. 수량 표시는 PlayerState의 **Get Hand Food Count**를 사용한다. 열거형의 `No Eligible Target`은 거리·방향·시야·먹이 종류를 확인할 신호다. `Out Of Reach`는 실제 Native 이동 후 손과 입 사이의 거리·높이·차폐 검사에서 실패한 경우다.

## 2. 직접 플레이하며 검증하기

| 장면 | 기대 결과 |
|---|---|
| 1번을 누르고 반복 입력 | Sphere 하나가 오른손에 표시되고 수량은 10이다. 조준·발사·콤보 시작이 차단된다. 수납하면 무기를 다시 사용할 수 있다. |
| 가까운 사슴이 풀을 뜯을 때 사슴 쪽을 보고 급여 | `Success`, 수량 9, `Actual Food` 한입, `Reputation Delta` +5가 나온다. |
| 멀리서 급여, 벽 뒤 급여, 도주 중이나 포만 상태의 사슴 | 실패 원인이 표시되고 먹이 소비와 평판 변화는 없다. |
| 같은 지역 사슴 두 마리가 서로 다른 플레이어를 봄 | 같은 종·지역이면 동일한 점수와 revision을 관측한다. 다른 지역은 그 지역의 점수를 읽는다. |
| A가 사슴 정면에서 피해를 줌 | 서버 로그 `[EcoHumanReputation]`에 실제 HP 감소에 비례한 차감이 한 번 기록된다. 이후 B를 보는 사슴도 해당 평판을 읽는다. |
| 사슴이 공격자를 인식하지 못한 상태의 피해 | 사슴이 받는 피해와 기존 위협 반응은 발생한다. 이 피해에 대한 공통 평판 차감 로그는 없다. |
| 멀티 PIE에서 두 명이 같은 사슴에게 동시에 시도 | 한 스텝에 유효한 한입만 적용된다. 각 소유자는 자기 수량과 급여 결과를 받는다. |
| 사망·리스폰, 새 PIE | 현재 세션에서 리스폰하면 수량과 평판은 유지되고 손은 비워진다. 새 PIE는 초기 수량과 평판으로 시작한다. |

권위 창의 Output Log에서 `EcoHandFood`, `EcoHumanReputation`으로 필터링한다. Inspector HUD를 쓰는 레벨에서는 사슴을 선택해 현재 보이는 플레이어, 손 먹이, 지역 평판을 볼 수 있다. JSON에는 `visible_player_interactions`, `hand_known`, `last_hand_feed_intake`가 들어간다. 손이 가려져 있으면 `hand_known=false`다. 숫자를 직접 읽고 비교하려면 서버 Blueprint에서 Spawner의 **Read Regional Human Reputation**에 실제 Region ID와 Species ID를 전달한다.

현재 상태는 PIE 세션 범위다. 손 먹이 규칙을 사용하는 레벨의 기존 생태계 체크포인트 저장·복원은 거절한다. Python 담당자는 게임 측 후보 증거인 `FEcoVisiblePlayerInteraction`과 급여 결과인 `FEcoHandFeedReceipt`를 조율해 기존 학습 환경에 연결한다. 기존 두뇌에는 공통 평판에 따른 접근·회피 동작이 연결되기 전이므로, 손 먹이 감지와 점수 변화는 Inspector와 로그로 검증한다.

UE 5.8 UBT로 `AdaptiveEcosystemEditor Win64 Development` 컴파일과 링크를 확인했다. 에디터와 Live Coding이 실행 중인 동안에는 저장소 지침에 따라 UBT 빌드를 시작할 수 없다. 실제 장착·급여·멀티플레이 동작은 위 절차로 PIE에서 확인한다.
