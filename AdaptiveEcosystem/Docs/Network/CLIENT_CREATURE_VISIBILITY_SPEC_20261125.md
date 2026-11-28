# 1102 멀티플레이에서 서버 개체를 Client에 표시하는 개발 명세

2026-11-25 · UE 5.8 · `jwj-1115` / `260ca50c`와 로컬 작업 트리 기준 · 후속 구현용

구현 후 설정과 직접 확인 순서는 [1102 Client 동물 표시 확인](CLIENT_CREATURE_VISIBILITY_PLAY_GUIDE.md)에 있다. 저장본 조회에서 Client 거리 1,000cm와 가까운 사슴 스폰점까지 1,050cm 이상의 시작 거리를 확인했다. 현재 1102에는 Client 로컬 거리 20,000cm를 프로젝트 설정으로 적용한다. 이 수치는 기존 Host 설정과 같다. 직접 멀티플레이 판정은 수동 확인 절차를 따른다.

**완료 장면**은 일반 플레이어가 Client로 접속해 서버에 존재하는 사슴과 늑대에 접근했을 때, 해당 개체의 몸체와 움직임을 실제 게임 창에서 보는 것이다. 두 플레이어가 같은 개체를 보면 같은 `StableAgentId`와 서버 행동을 관찰하고, 서로 떨어지면 각자의 관찰 범위와 표시 예산을 적용받는다. 개체 생성·판단·이동·피해·죽음·지역 장부는 서버가 소유한다.

## 1. 이번 증상에서 확인한 범위

| 근거 | 확인한 사실과 다음 판단 |
| --- | --- |
| 11월 25일 14:32:34~14:32:48 KST, 로컬 `Saved/Logs/AdaptiveEcosystem.log` | Dedicated 서버와 두 Client가 최종적으로 `/Game/JWJ/1102_Integration_JWJ`에 진입했다. 두 번의 `Join succeeded`, 서버의 `Authority ready: Agents=80 Regions=5`, 같은 WorldEpoch에서 두 번의 `Mass ready: true`가 기록됐다. 최초 Browse에 찍힌 `Integration_JwJ`와 실제 플레이 월드를 구분한다. |
| 같은 세션의 카메라·지역 로그 | 시작 직후 날씨 표현기가 카메라 약 `(2, 142, 65) m`에서 ‘지역 없음’을 기록했다. 이는 날씨 지역 판정이다. 플레이어의 실제 시작점에서 가장 가까운 살아 있는 개체까지의 **3차원 거리**와 그 연결의 서버 viewer 위치를 먼저 측정한다. |
| 템플릿 로그와 수신 코드 | 서버는 저장된 사슴·늑대 Config와 transient v3 Config를 기록했고, 두 Client에는 저장 Config의 `Validated=1`이 찍혔다. Client는 transient v3 두 종류도 별도로 사전 등록하며 이 루프에는 같은 성공 로그가 없다. 이름 차이만으로 실패 판정을 할 수 없다. 실제 Bubble 항목의 TemplateID로 해당 Client 월드 등록소를 조회한다. [등록 코드](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureTemplateValidation.cpp) |
| 아직 없는 증거 | 해당 세션에서 같은 ID의 연결별 Bubble 항목, 수신 proxy, 카메라 거리, 바인딩된 Actor, 렌더 화면을 한 시점에 수집한 자료가 없다. 과거 다른 맵의 Dedicated + Client 시험에서는 거리 제한에 따라 proxy를 유지한 채 Actor만 사라졌다가 다시 나타났다. 1102에서 동일하게 일어났는지는 재현 대상이다. [기존 거리 시험](CLIENT_VISUAL_DISTANCE_20261106.md) |

## 2. 기존 구현과 데이터 계약

| 생산자 → 소비자 | 현재 전달하는 값과 책임 |
| --- | --- |
| 서버 Mass → 연결별 Bubble | `UEcoMassReplicator`가 연결의 relevance·LOD·예산에 따라 `AEcoMassClientBubbleInfo`의 Fast Array에 Add/Change/Remove를 반영한다. 항목은 TemplateID, StableAgentId, SpeciesId, RegionId, 위치(cm)·yaw, 속도, 생존·활동·정규화된 체력/에너지와 외형 표본을 전달한다. [송신기](../../Source/AdaptiveEcosystem/Network/Mass/EcoMassReplicator.cpp) · [전송 구조](../../Source/AdaptiveEcosystem/Network/Mass/EcoMassReplicationTypes.h) |
| Bubble → Client Mass proxy | Add에서 **수신한 TemplateID**로 표현 템플릿을 찾고 없으면 ReplicationID로 보류한다. 등록 후 최신 항목으로 재시도하며, 대기 중 Remove는 취소한다. proxy는 ID·지역·표현 표본을 가진다. 이동 순번, 시각 상태 revision, 이동 불연속 revision으로 중복·과거 표본을 거르고 순간이동 시 보간 이력을 초기화한다. [수신 처리](../../Source/AdaptiveEcosystem/Network/Mass/EcoMassClientBubble.cpp) · [표본 버퍼](../../Source/AdaptiveEcosystem/Mass/EcoMassPresentationFragments.h) |
| Client proxy → 로컬 메시 | coordinator의 `SyncVisuals`가 유효 ID와 첫 표본을 확인하고, 로컬 카메라에서 보간 위치까지의 거리·Actor 예산으로 표시 대상을 고른다. 종에 맞는 Actor를 풀에서 꺼내거나 생성하고 `BindIdentity` 후 `ConsumeVisualState`로 몸체·애니메이션을 갱신한다. 표시 대상에서 빠지면 `ClearBinding` 후 풀로 돌린다. 표현 Actor는 복제와 ReplicateMovement가 꺼져 있다. [표시 선택과 바인딩](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreaturePresentation.cpp) · [표현 Actor](../../Source/AdaptiveEcosystem/Creature/Representation/EcoCreatureRepresentationActor.cpp) |

**판정 단위는 한 월드의 한 연결과 한 개체**다. `WorldEpoch + StableAgentId`로 같은 실행의 양끝을 대조하고 전송 항목은 Bubble/연결까지 지정한다. Client에는 표시용 proxy만 생성한다. 시뮬레이션 재스폰, 전체 개체 배열의 GameState 복제, 개체별 Multicast, 표현 Actor의 별도 이동 복제를 추가하면 위치와 생명주기의 소유자가 중복된다. [권위 계약](MASS_NETWORK_AUTHORITY_CONTRACT.md)

## 3. 첫 재현 절차

1. 1102를 직접 열고 **Play As Client / Number of Players = 2**로 실행한다. 첫 구간은 원래 PlayerStart, 카메라, 맵 설정으로 둔다. 서버와 Client A/B 각각에서 실제 월드 패키지·NetMode·WorldEpoch를 확인한다. coordinator의 실행 시점 `ClientVisualDistance`와 Actor 예산을 기록한다. C++ 초기값은 Client **1,000cm**, Host **20,000cm**이고 레벨 인스턴스가 덮어쓸 수 있다. [거리 설정](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureIntegrationSpawner.h) · [예산·LOD 프리셋](../../Source/AdaptiveEcosystem/Network/Mass/EcoMassNetworkSettings.h)
2. PIE의 **서버 월드**를 선택해 `eco.M4.Dump details`와 `eco.Integration.Dump details`를 실행한다. 시작점에 가장 가까운 살아 있는 사슴·늑대의 ID와 좌표를 고르고, 각 ID가 A/B 중 어느 연결의 Bubble에 들어갔는지 기록한다. 필요할 때만 `eco.M4.TraceSamples 1`로 Add/Dirty/Remove를 캡처한 후 `0`으로 복구한다. 이 CVar는 PIE 프로세스 전체에 적용된다.
3. **각 Client 월드**에서 같은 두 Dump를 실행한다. `Role=ClientProxy`의 ID, Bubble의 `Entries/PendingAdds`, 수신 `Sequence`, coordinator의 `Ready/Actors/Pool`을 같은 ID의 서버 자료와 비교한다. `Integration.Dump`의 `Alive`는 coordinator가 소유한 권위 개체 수이므로 Client에 보이는 동물 수는 `M4.Dump`의 proxy와 바인딩을 사용해 판단한다. [M4 진단](../../Source/AdaptiveEcosystem/Debug/EcoMassNetworkDiagnostics.cpp) · [수신·Actor 진단](../../Source/AdaptiveEcosystem/Creature/Runtime/EcoCreatureDiagnostics.cpp)
4. Client A의 **플레이어 Pawn과 실제 카메라**를 대상의 약 3~5m 앞, 지형 위, 시야가 트인 위치에 두고 카메라를 대상으로 향하게 한다. B는 시작점에 둔다. 서버에서도 A의 viewer가 이동했는지 확인하고 두 번 이상 표본을 수집한다. 표적이 이동하거나 죽으면 좌표와 생존을 다시 확인한다. 다음에는 B를 접근시킨다.
5. 가까운 대상에 proxy가 있지만 Actor가 0이면, A의 PIE 인스턴스에서만 `ClientVisualDistance=0`으로 한 차례 비교한 뒤 원래 값으로 돌린다. 0은 로컬 거리 검사만 해제한다. 이 비교 중 서버 Bubble과 proxy가 그대로이고 Actor만 달라지는지 본다. 정상 플레이의 최종 거리값은 기본 시작점과 접근 구간에서 실제 플레이어가 봐야 할 거리를 측정한 뒤 정한다.

## 4. 최초로 실패한 단계별 구현 작업

아래 표를 위에서부터 적용한다. 각 행의 완료 조건은 **같은 ID**가 다음 단계에 도달했는지로 확인한다.

| 최초로 끊긴 지점 | 다음 작업 | 완료 증거 |
| --- | --- | --- |
| 서버에 살아 있는 대상이 없음 | 실제 맵·GameMode·coordinator, 활성 종/스폰 그룹, 서식지 스트리밍 완료와 권위 초기화를 확인한다. 실제 실패한 설정 또는 기동 경로를 수정한다. | 서버 Dump에 종·ID·지역·위치가 있다. |
| 대상은 있지만 가까운 연결의 서버 Bubble에 없음 | 해당 연결의 Pawn/viewer 등록·위치, Bubble 소유 연결, 템플릿의 복제 trait, 실제 LOD 거리·개수 및 누적 `BudgetDeferredAdds`를 확인한다. viewer 준비 지연 또는 잘못된 relevance 설정 등 재현된 지점을 고친다. | 대상이 가까워지면 그 연결에 Add가 생긴다. 두 연결의 진입·이탈을 독립적으로 설명할 수 있다. |
| 서버 Bubble에는 있고 해당 Client Bubble에 없음 | 소유 연결과 수신 월드, serializer 복제·초기 Fast Array 전달을 확인한다. 늦은 접속·월드 전환에서도 현재 관련 항목을 받을 수 있게 기존 Bubble 경로를 고친다. | 해당 Client에 같은 ID와 TemplateID의 첫 항목이 온다. |
| Client 항목은 있고 유효 proxy/첫 표본이 없음 | 실제 TemplateID의 로컬 등록·조합, `PendingAdds`의 해소, Add/Change/Remove 순서와 `Receive` 거부 사유를 확인한다. 등록 순서 문제이면 기존 사전 등록/보류 재시도를 보완한다. 지연 중 제거된 항목은 되살리지 않는다. | 준비 후 보류가 해소되고 ID당 proxy 하나에 `bHasSample=true`가 된다. |
| 유효 proxy가 있고 근거리 Actor가 없음 | `bReady`, 로컬 viewer 유무, 실제 3D 거리, 거리 통과 후보 수, Actor/Add 예산, 종별 클래스·풀 호환·Spawn/Bind 결과를 확인한다. 기본 시작점에서만 실패하면 담당 레벨의 PlayerStart·관찰 동선·표시 거리 중 원인으로 입증된 값을 조정한다. WP에서는 coordinator가 Client에 로드되어 있는지도 본다. | 같은 ID가 예산 내에서 Actor 하나에 바인딩된다. 거리 밖에서는 풀로 돌아가고 재진입하면 다시 표시된다. |
| Actor가 바인딩됐지만 몸체가 안 보임 | Client 창에서 실제 BP 클래스, Skeletal Mesh/컴포넌트 가시성, 숨김·스케일·bounds, 렌더 위치의 Z, 카메라 방향과 지형 가림을 확인한다. 잘못된 클래스 매핑·에셋 참조·풀 재사용 상태 등 확인된 원인을 수정한다. | 해당 Client의 일반 렌더 화면에서 몸체가 보이며, 서버가 보낸 이동 또는 활동 변화가 같은 몸체에 반영된다. |

**추가할 진단은 기존 Dump에 없는 값으로 한정한다.** 한 ID를 지정한 읽기 전용 출력으로 서버의 연결별 viewer/LOD와 Client의 카메라 XYZ·거리(cm)·적용 한도·표시 후보 여부·예산 탈락·최종 클래스·바인딩/숨김 상태를 남긴다. 기존 `Sequence/VisualRev/ResetRev`를 함께 기록하면 수신이 끊긴 경우와 표시만 제외된 경우를 같은 표본에서 구분할 수 있다. 실행 중인 월드에서만 조회하고, Mass 처리가 끝난 Game Thread 경계에서 수집한다.

## 5. 인수 시험과 제출 근거

| 시험 | 통과 기준 |
| --- | --- |
| 원래 시작점 → 사슴·늑대 접근 | 수정 전후 같은 시작점과 동선을 사용한다. 첫 화면이 비면 가장 가까운 대상의 거리·제외 이유가 기록된다. 시야와 표시 범위 안에서 예산이 남은 살아 있는 각 종의 대상이 Client 화면에 보인다. 플레이어가 평소 접근했을 때도 재현한다. |
| Dedicated + 두 Client / 늦은 접속 | A가 먼저 관찰 중 B가 접속한다. 같은 위치에서는 둘 모두 같은 ID의 최신 위치·종·활동을 표시한다. 다른 지역으로 이동하면 각 연결의 대상만 바뀐다. 중복·무효 ID는 0이다. |
| 거리 이탈·재진입 / 전송 범위 이탈 | 로컬 표시 거리만 벗어나면 proxy가 유지될 수 있고 Actor는 숨겨진다. 서버 relevance까지 벗어나 Remove가 오면 proxy와 바인딩이 정리된다. 재진입 시 최신 상태에서 1회 바인딩하며 풀에 남은 다른 개체의 외형·체력 표시가 섞이지 않는다. |
| 생명주기·움직임 | 서버 출생 시 관련 Client에 새 ID가 나타나고, 사망·최종 제거 시 서버 수명주기에 맞춰 표시가 종료된다. 연속 이동은 보간되고 순간이동·대상 교체는 이전 위치에서 끌려오지 않는다. Client의 권위 개체·Native Step·행동 실행은 0이다. |
| 회귀와 실화면 | Listen Host와 Standalone에서도 기존 개체가 표시된다. 구현에서 C++/빌드 입력을 변경하면 저장소 규칙에 따라 직접 UBT로 Editor 빌드 후, 수정한 경계의 회귀 시험과 1102의 렌더링 PIE를 수행한다. 기존 자동화의 거리/재진입 패턴은 재사용하되 해당 스크립트는 별도 이주 맵용이다. [거리 시험 코드](../../Tools/Editor/verify_client_visual_distance.py) |

제출물은 **같은 실행의 서버·Client A/B 로그, 대상 ID별 단계 대조표, 실제 Client 창 캡처**다. 대조표에는 맵·빌드·WorldEpoch·연결·시각·ID·서버 좌표·Bubble 포함 여부·proxy·카메라 거리·Actor/메시 결과를 남긴다. 구현자는 최초 실패 단계, 수정한 파일 또는 맵 설정, 수정 전후 결과를 함께 기록한다. 이 명세 작성 시에는 기존 로그와 Source를 대조했고 새 빌드·PIE는 수행하지 않았다.
