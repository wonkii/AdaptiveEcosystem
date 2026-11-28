# 사슴·늑대 Mass Gameplay 시험 레벨 배치 안내

UE 5.8 · 2026-11-24 · 새 레벨에서 현재 `SpeciesAI` 전체 행동 경로를 직접 확인하는 순서다. 아래의 맵 이름·좌표·값은 평면에서 시작하기 위한 예시다. 단위는 언리얼 기본 cm이며 좌표는 월드 XYZ다. [개발 명세와 전체 구조도](DEER_WOLF_MASS_GAMEPLAY_SPEC_20261123.md)

## 1. 빈 레벨과 이동면

1. **File → New Level → Empty Level**로 만든 뒤, 예를 들어 `/Game/Experiments/SpeciesAI/Maps/LV_DeerWolf_Mass`에 저장한다. 팀 맵의 Landscape를 사용하려면 에디터에서 소유 경로로 복제하고, 복사본에 기존 coordinator나 논리 지역이 있는지 먼저 확인한다. World Partition 복사본에서는 지형·NavMesh 관련 셀을 로드한다.
2. **World Settings → GameMode Override**를 `EcoCreatureIntegrationGameMode` 또는 그 자식 BP로 설정한다. 이 GameMode가 `EcoGameState`와 WorldEpoch를 초기화한다. 관찰용 `PlayerStart`를 바닥 위에 둔다. 메시 확인용 조명은 Directional Light와 Sky Light로 시작할 수 있다.
3. 아래 표대로 바닥과 벽을 배치한다. 기본 100cm Cube를 쓴다면 표의 실제 크기를 100으로 나눈 값을 Scale에 넣는다. 바닥과 벽은 **Static**, Object Type은 **WorldStatic**, Collision Preset은 **BlockAll**로 시작한다. 바닥 충돌은 발 위치를 찾고, 벽 충돌은 캡슐 이동과 Visibility 시선 검사를 막는다.
  4. `NavMeshBoundsVolume`을 놓고 **Build → Build Paths**, 뷰포트에서 **P**를 누른다. 바닥, 스폰 위치, 먹이 슬롯, 벽 양쪽 우회 통로에 녹색 이동면이 연속되어야 한다. 벽 윗면에 녹색이 생겨도 바닥과 연결되지 않는지 확인한다.
5. `DA_Deer`, `DA_Wolf`의 Body Radius/Height를 확인한다. C++ 기본값은 **30 / 130cm**다. Project Settings → Navigation System → Supported Agents를 쓰는 프로젝트에서는 두 프로필에 맞는 NavData가 등록되는지 확인한다. Navigation Mesh의 Recast 에이전트 설정도 실제 폭·높이에서 길을 찾을 수 있어야 한다. 프로젝트 공용 설정을 변경하기 전에 기존 NavData와 녹색 면부터 확인한다.

| Actor | 위치 | 실제 크기 또는 설정 |
|---|---|---|
| 바닥 Cube | `(0, 0, -50)` | `4800 × 4800 × 100`. 윗면 Z=0 |
| 가림벽 Cube | `(0, 0, 150)` | `100 × 800 × 300`. 사슴 우회 시험용. 벽을 옮긴 뒤 경로를 다시 빌드한다. |
| NavMeshBoundsVolume | `(0, 0, 100)` | Brush Settings의 X/Y/Z 크기 또는 Volume Scale로 전체 크기를 `4200 × 4200 × 600`에 맞춘다. |
| PlayerStart | `(-1500, -1400, 120)` | 동물·벽과 떨어진 관찰 시작점 |

## 2. 권위 Actor와 먹이 슬롯

**Outliner 배치**는 `EcologyRegion` 한 개, `EcoCreatureIntegrationSpawner` 한 개, 식사에 사용할 `EcoSpeciesAIActivitySite` 한 개부터 시작한다. `BP_EcoCreatureIntegration`을 쓰는 경우 해당 BP가 coordinator 한 개 역할을 한다. 동물 BP는 spawner의 클래스 필드에 지정한다. 스폰은 이 coordinator가 담당하므로 동물 BP나 별도 MassSpawner, M3 Box Bootstrap을 레벨에 추가할 필요가 없다.

| Details 위치 | 입력값 |
|---|---|
| EcologyRegion 본체 | Location `(0,0,0)`, **Region Id = Forest_A**. Initial Food Amount = **800**, Food Capacity = **1000**, Food Regeneration Rate = **0**. Adjacent Region Ids는 빈 배열. |
| EcologyRegion → RegionBounds | Box Extent `(2200,2200,400)`, Scale `(1,1,1)`. 바닥의 스폰·슬롯·이동 통로를 모두 감싼다. |
| Coordinator → Creature / Config | Herbivore Config = `/Game/Experiments/SpeciesAI/EC_Deer`, Wolf Config = `/Game/Experiments/SpeciesAI/EC_Wolf`. 각 EC의 `Eco Integrated Creature` Trait에서 **Use Species AI**, 종별 `DA_Deer` / `DA_Wolf`, Profile의 **Policy Only 꺼짐**, `ST_Deer` / `ST_Wolf`를 확인한다. 단독 모드의 상대 Config는 비워도 된다. |
| Coordinator → Creature / Visual | Herbivore Actor Class = `/Game/Creatures/Integrated/BP_EcoDeer`, Wolf Actor Class = `/Game/Creatures/Integrated/BP_EcoWolf`. 원거리 멀티 관찰이 필요하면 해당 맵의 Client Visual Distance를 **5000cm**로 설정한다. |
| Coordinator → Creature / Population → Groups[0] | 그룹 1개, Region Id = **Forest_A**. Use Food Patch Position 켬, Food Patch Position = `(800,0,0)`. Schedule → Region Population Limit = **16**, Global Population Limit = **16**. 종별 수와 좌표는 아래 시험 표를 사용한다. |
| Coordinator → 시험용 옵션 | Use V3 Policy, Ecosystem Life, Enable Vegetation, Enable Food Grass, Enable Waves, Enable Herd Journeys, Enable Population Pressure, Migration의 Enabled를 끈다. Natural Habitat Profile·Region Habitat Profiles·추가 Fauna는 비워 둔다. 늑대의 사냥·영양 시험에서 Enable Predator Survival을 켠다. |
| EcoSpeciesAIActivitySite 본체 | 위치 `(800,0,0)`, Region Id = **Forest_A**, Kind = **Graze**, For Predator 꺼짐. |
| ActivitySite → SmartObject 컴포넌트 | Definition = `/Game/Experiments/SpeciesAI/SO_ActivitySlot`. 기본 슬롯 한 개를 사용한다. 슬롯 위치 표시를 켜서 스폰과 같은 지면 높이의 녹색 NavMesh 위인지 확인한다. |

먹이 위치와 슬롯을 같은 곳에 두면 행동과 장부를 함께 확인하기 쉽다. 먹이 재고는 EcologyRegion의 초기 설정과 서버 장부에서 나오며 슬롯은 실제 이용 위치다. Rest 장소를 추가하려면 ActivitySite를 한 개 더 배치하고 Kind = **Rest**와 종에 맞는 For Predator 값을 준다. 별도의 Shelter 시험에서는 `EcoShelterAnchor`를 실제 차폐 뒤에 놓고 Capacity **1**, Quality **0.65**, Radius **150**, Project Slots to Ground를 설정한 뒤 슬롯까지 녹색 이동면이 닿는지 확인한다.

저장 후 coordinator Details의 **Validate Editor Setup**을 실행한다. Output Log의 `[Eco Setup] ... Errors=0`이 배치 확인 기준이다. 별도로 Play 전에 에디터 콘솔에 `eco.IntegratedPolicyVersion 1`을 입력한다. 세션 옵션이나 복제한 맵의 Use V3 Policy가 켜져 있으면 Native v3가 요청될 수 있으므로 두 값을 함께 확인한다. 첫 실행은 에디터의 오프라인 Play를 사용한다.

## 3. 시험 순서와 배치 좌표

| 시험 | Coordinator와 스폰 좌표 | 대역 또는 지형 |
|---|---|---|
| 사슴 우회·섭식 | **DeerOnly**, Herbivores=1, Wolves=0. Herbivore Spawn Points[0] = `(-1000,0,0)`. Fixtures 꺼짐. | 중앙 벽과 먹이 슬롯 유지. 사슴이 벽 끝을 돌아 슬롯에 도착할 수 있게 양옆을 비운다. |
| 사슴 위협·복귀 | 위와 같은 설정에서 Enable Species AI Fixtures를 켠다. | `EcoSpeciesAIFixture` 한 개, Prey 끔, Visible 끔, Makes Noise 끔, Motion Radius=0. 슬롯 근처 `(800,-500,0)`에 둔다. 섭식 후 서버 PIE Outliner에서 대역을 선택해 **Makes Noise**를 켜고 반응한 뒤 다시 끈다. 직접 시야로 시험할 때는 사슴이 바라보는 쪽에 대역을 두고 Visible을 켠다. |
| 늑대 추적·포기 | **WolfOnly**, Herbivores=0, Wolves=1. Wolf Spawn Points[0] = `(800,1050,0)`. Predator Survival과 Fixtures 켬. | 대역 한 개, Prey와 Visible 켬, Makes Noise 끔, Motion Radius=0, 위치 `(800,450,0)`. 첫 늑대의 초기 방향은 거의 -Y다. 대역이 포획되기 전에 서버 PIE에서 Visible을 끄고 Makes Noise를 켜 소리만 남긴 뒤 둘 다 끈다. 가림벽으로 LOS를 확인하는 별도 실행에서는 두 점 사이로 벽을 옮기고 Build Paths를 다시 한다. |
| 실제 두 종 | **Both**, Herbivores=4, Wolves=1, Predator Survival 켬, **Fixtures 끔**. 사슴 `(800,450,0)`, `(1150,0,0)`, `(1150,-350,0)`, `(400,-350,0)`. 늑대 `(800,1050,0)`. | 전용 가림벽은 처음 위치로 돌린다. 포획을 따로 관찰하려면 사슴 수를 1로 줄이고 첫 사슴 좌표만 사용한다. 늑대 2마리 경쟁 시험에서는 서로 떨어진 Wolf Spawn Points 두 개를 입력한다. |

대역은 실제 동물 대신 감각 사건을 공급하는 Actor이며 기본적으로 보이는 메시가 없다. 직접 표시를 붙이려면 **No Collision**으로 두어 시야 트레이스를 가로막지 않게 한다. PIE 중 Details 편집은 **Eject**한 뒤 현재 서버 PIE 월드의 Actor에서 수행한다. 종료 후 같은 장면을 반복하려면 에디터 월드의 초기값을 확인한다.

## 4. Play 중 확인

```text
eco.SpeciesAI.Status
eco.M4.Dump details
eco.M4.SetVitals <StableAgentId> 1 0.2
eco.Integration.Dump details
eco.SpeciesAI.Capture
```

1. **실행 경로.** Status에서 사슴 단독은 `Deer=1 Wolf=0 V1=0`, 늑대 단독은 `Deer=0 Wolf=1 V1=0`, 두 종 초기 실행은 `Deer=4 Wolf=1 V1=0`을 확인한다. `[Eco Integration] Template=...`의 Config 경로도 확인한다. 0마리라면 NavMesh와 실제 EC 참조를 먼저 확인한다.
2. **섭식.** Dump에서 실제 ID를 찾고 SetVitals의 첫 인자로 넣는다. 두 번째 인자는 HP 비율, 세 번째 인자는 Energy 비율이다. 사슴의 Forage → Graze, 예약 번호, `Granted` 또는 `TotalFood` 증가, 지역 `Food` 감소를 함께 확인한다. 위협 뒤에는 예약 해제와 새 섭식 중단을 본다. 위협 전에 누적한 섭식은 원장의 다음 처리 시점에 반영될 수 있다.
3. **추적과 포획.** 늑대에도 같은 명령으로 허기를 준다. 소리만 있는 동안 공격 성공 수가 늘지 않고 마지막 단서 확인 뒤 수색을 포기하는지 본다. 대역 포획은 `FixtureCaptures`로 확인한다. 실제 포획 성공 시 `AttackSuccess`, 사슴 사망과 지역 `Pop` 감소, 늑대 Energy 지급을 대조한다.
4. **증거 저장.** 위협 전·후나 추격이 끊기는 시점에 `eco.SpeciesAI.Capture`를 각각 실행한다. Output Log에 표시된 `Saved/SpeciesAI`의 CSV에서 같은 ID의 `Decision`, `Activity`, `NavStatus`, `MoveRevision`, `SlotReservation`, `AttackSuccess`를 비교한다. 서버와 클라이언트 확인이 필요하면 에디터 Net Mode를 **Play As Client**, Number of Players를 **2**로 바꿔 전용 서버 세션을 열고, 권위 진단은 서버 콘솔에서 실행한다.

| 증상 | 확인할 설정 |
|---|---|
| `Missing supported NavMesh` | P로 이동면 확인 → NavMeshBoundsVolume과 지면 충돌 → Build Paths → Supported Agents와 프로필의 몸체 수치 확인 |
| `Invalid spawn ground` 또는 좌표 개수 오류 | world-space 위치의 바닥 Z·RegionBounds 확인, 종별 개체 수만큼 서로 다른 스폰 좌표 입력 |
| 사슴이 먹이 앞에 멈추고 `Slots=0` | 같은 RegionId, Kind=Graze, For Predator=false, SmartObject Definition과 슬롯 위치, 초기 Food가 양수인지 확인 |
| 늑대가 배회나 휴식만 함 | 해당 ID의 Energy, 실제 `EC_Wolf`, 대역 Prey/Visible, 늑대 전방의 거리·벽 뒤인지 확인 |
| 화면에 동물 없음 | Status 개체 수, 종별 Actor Class, 플레이어 카메라와 Host/Client Visual Distance 확인 |

[기존 SpeciesAI 에디터 적용](EDITOR_QUICK_START.md) · [M4 Actor 구성](../M4_NEW_LEVEL_MANUAL_SETUP.md) · [진단 소스](../../../Source/AdaptiveEcosystem/Debug/EcoSpeciesAIDiagnostics.cpp)
