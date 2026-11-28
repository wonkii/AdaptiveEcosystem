# 1102에서 Client 동물 표시 확인

UE 5.8 · 2026-11-25 · [개발 명세](CLIENT_CREATURE_VISIBILITY_SPEC_20261125.md)

## 에디터 설정

1. `/Game/JWJ/1102_Integration_JWJ`를 직접 연다.
2. Play 메뉴를 열고 **Spawn Player At → Default Player Start**, **Net Mode → Play As Client**, **Number of Players → 2**로 설정한다. **New Editor Window (PIE)**로 시작하면 각 Client 콘솔과 화면을 따로 볼 수 있다.
3. `Project Settings`에서 **Adaptive Ecosystem → M4 → Presentation → Client Visual Distance by Map (cm)**에 `/Game/JWJ/1102_Integration_JWJ = 20000`이 있는지 확인한다. 프로젝트 기본값을 이미 넣어 두었다. 이 값은 Client 시작 시 `JWJ_WildlifeTest_Runtime`의 로컬 카메라 표시 거리에 적용된다. `[Eco Visibility] ... ClientRangeCm=20000.0`으로 적용을 확인한다. [프로젝트 설정 파일](../../Config/DefaultGame.ini)
4. `JWJ_WildlifeTest_Runtime`에서 기존 사슴·늑대 표시 클래스 `BP_JYU_Deer`, `BP_JYU_Wolf`와 **Is Spatially Loaded** 꺼짐을 확인한다. Play 후 `JWJ_WildlifeTest_PlayerStart`에서 북쪽(월드 +Y, WildlifeTest 중심)으로 이동하면 사슴 스폰 구역을 지난다. 늑대 스폰점은 같은 지역에서 더 북쪽이다. 개체가 움직이므로 실제 위치는 서버의 아래 명령으로 확인한다.

표시 거리를 바꿔 비교할 때는 Play를 끝낸 뒤 같은 Map 항목을 수정하고 다시 시작한다. `1000`은 기존 10m 제한을 재현하는 값이다. `0`은 유효한 로컬 카메라에서 거리 제한을 해제한다. 서버 Bubble의 거리·LOD와 로컬 Actor 예산은 계속 적용된다. 검증 뒤 기본 `20000`으로 돌린다.

## 화면과 콘솔에서 확인할 것

| 창 | 입력과 판정 |
| --- | --- |
| 서버 월드 | `eco.M4.Dump details`에서 `Alive=1`인 사슴(`Herbivore`)과 늑대(`Wolf`)의 ID·좌표를 고른다. `eco.M4.Dump details <ID>`로 연결별 viewer 위치와 대상 Bubble 항목을 확인한다. |
| Client A와 B | 같은 대상에 접근해 두 창에서 몸체와 움직임을 본다. `eco.M4.Dump details <ID>`에서 `Role=ClientProxy`, 대상 Bubble의 `TemplateRegistered=1`, 대상 ID와 Sequence를 확인한다. |
| Client에서 안 보일 때 | 같은 Client 창에서 `eco.Integration.Visibility <ID>`를 실행한다. Output Log의 `[Eco Visibility]`에서 `Stage`, `DistanceCm`, `RangeCm`, `ExpectedClass`, `Bound`, `Hidden`, `Mesh`를 확인한다. |
| 이동과 재진입 | A는 대상 근처에 두고 B만 멀어졌다가 돌아온다. B에서 표시가 사라졌다 돌아오는 동안 A에서도 같은 ID의 몸체가 계속 보이는지 확인한다. 한도를 10m로 줄여 따로 비교하면 로컬 범위 밖에서도 Bubble과 proxy가 남아 있는 경우를 볼 수 있다. |

`NoProxy`이면 같은 ID의 서버/Client Bubble 로그부터 비교한다. `NoSample`이면 첫 수신과 템플릿 등록을 확인한다. `OutsideVisualRange`이면 카메라 거리와 적용 한도를 확인한다. `ActorBudget`이면 가까운 후보 수와 표시 예산을 확인한다. `Bound`인데 화면에 몸체가 없다면 로그의 실제 Actor·메시·좌표를 Client Outliner와 대조한다.

늦은 접속을 별도로 볼 때는 **Run Under One Process**를 끄고 Dedicated 서버와 첫 Client를 시작한다. 두 번째 게임 프로세스에서 `open 127.0.0.1:<서버 포트>`로 같은 서버에 접속한 뒤 같은 ID를 확인한다. 서버 포트는 해당 실행의 서버 로그에서 확인한다.
