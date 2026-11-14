# 공식 문서·원문 기술자료 읽기 안내

2026-11-12 [09 통합 개발 명세](09_AVOIDANCE_INTEGRATION_SPEC_20261112.md) 후속으로 Epic Mass Avoidance/MassGameplay, Reynolds 원문, 아래 게임 개발 기술자료와 ORCA 안내를 재확인했다. 설치 UE5.8.3의 회피 후보와 movement custom tag 제외도 대조했다. 웹 기본 문서는 5.9로 표시됐으며 5.8 지정 URL 일부는 조회 도구에서 열리지 않았다.

2026-11-10 확인. 공식 웹 문서는 기본 버전이 바뀔 수 있으므로 프로젝트의 UE5.8을 선택하고 설치 소스와 대조한다. 아래 설계 문서는 참고 원리를 우리 시스템에 적용한 제안이며 Epic이 우리 생태계 동작을 검증한 것은 아니다.

## 추천 순서

| 순서 | 자료 | 읽을 내용·우리 문서와의 연결 |
|---|---|---|
| 1 | [Epic MassEntity Overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-entity-in-unreal-engine?lang=en-US) | Fragment·Archetype·Chunk·Query·Processor. [03](03_REGION_EXECUTION.md)의 데이터 접근 경계 |
| 2 | [Epic MassGameplay Overview](https://dev.epicgames.com/documentation/unreal-engine/overview-of-mass-gameplay-in-unreal-engine?lang=en-US) | Movement, StateTree/Signals, Simulation·Representation·Replication LOD를 구분. [02](02_STEERING_INTEGRATION.md), [04](04_LOD_AND_SIGNALS.md) |
| 3 | [Epic Mass Avoidance Overview](https://dev.epicgames.com/documentation/unreal-engine/mass-avoidance-overview-in-unreal-engine?lang=en-US) | 공간 해시·가까운 후보·분리와 미래 충돌 회피. [01](01_NEIGHBOR_AVOIDANCE.md) 핵심 자료. 문서상 Experimental 표시도 확인 |
| 4 | [Epic Query Executor](https://dev.epicgames.com/documentation/en-us/unreal-engine/simplified-mass-processor) | Fragment 접근을 명시하는 선택적 API. 기존 API도 지원되며 전환 자체가 성능 최적화는 아님 |
| 5 | [Epic Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine) | CPU/GPU timing·memory·network 추적. [05](05_VALIDATION.md)의 전체 비용 확인. 조회 당시 기본 페이지는5.9이므로5.8 UI/옵션은 설치 버전과 대조 |
| 6 | [Craig Reynolds, Steering Behaviors for Autonomous Characters, GDC 1999](https://www.red3d.com/cwr/papers/1999/gdc99steer.html) | 목표 선택·조향·locomotion 계층과 조향 조합을 이해할 원문. [PDF](https://www.red3d.com/cwr/papers/1999/gdc99steer.pdf) |
| 선택 | [ORCA 연구 원문 안내](https://gamma-web.iacs.umd.edu/ORCA/) | 상대 속도 기반 상호 회피와 선형계획 접근의 비교 자료. Epic의 force 기반 Mass Avoidance와 동일 알고리즘이 아니며 도입은 Deferred |
| 7 | [Graham Pentheny, Game AI Pro 2, Chapter 17 — Advanced Techniques for Robust, Efficient Crowds](https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter17_Advanced_Techniques_for_Robust_Efficient_Crowds.pdf) | §17.2~17.4의 혼잡과 경로 비용. 09의 병목 fixture·통행 시간 평가에 연결 |
| 8 | [Owen McCarthy, Creating believable crowds in Planet Coaster, Game Developer, 2017-01-04](https://www.gamedeveloper.com/audio/game-design-deep-dive-creating-believable-crowds-in-i-planet-coaster-i-) | Frontier 개발자 기고의 flow field·gridlock·표현 개발 경험. 09의 밀집 정체와 겹침 동시 평가에 연결 |

09의 구체적인 회피 계수, snapshot 계약, 양보 방식과 채택 수치는 프로젝트 설계다. 외부 사례의 군중 규모·성능·충돌 가정을 우리 사슴/늑대 실행의 달성 수치로 사용하지 않는다.

## 설치 UE5.8에서 확인한 세부

엔진 루트 `C:/Program Files/Epic Games/UE_5.8/Engine` 기준:

- `Plugins/AI/MassAI/Source/MassNavigation/Private/MassAvoidanceProcessors.cpp`: FindCloseObstacles는 셀 거리순 수집, MaxObstacleResults=24. 이동 회피 경로는 수집 후보 정렬 후 MaxColliders=6으로 collider 목록을 제한한다. 이는 설치 버전의 상수이며 문서의 일반 계약이 아니다. collider 수와 개체 수는 반드시 같지 않고 전체의 정확한 최근접6 보장도 아니다. 셀 열거/정렬 비용은 여전히 존재한다.
- 같은 디렉터리 `MassSteeringProcessors.cpp`: 목표 steering이 Avoidance보다 먼저 실행된다.
- `Plugins/Runtime/MassGameplay/Source/MassMovement/Private/MassMovementProcessors.cpp`: Force→DesiredMovement→Movement 경계와 custom movement 제외 조건. Native와 중복 writer를 만들지 않는 설계에 참고한다.
- `Plugins/AI/MassAI/Source/MassNavigation/Public/Avoidance/MassAvoidanceFragments.h`: 회피 파라미터의 실제 정의.

우리와 비교할 파일: `Source/AdaptiveEcosystem/AI/Policy/EcoWorldV3.h`, `EcoRepertoire.h`, `EcoObservationV3.h`, `EcoSpatialIndexV3.h`, `EcoPolicyV3IntegratedProcessors.cpp`. Epic의 회피는 생태 정책 관측·먹이/인구 장부를 대신하지 않는다.

외부 라이브러리 도입·엔진 코드 복제·패키지 추가는 이 자료 안내의 범위가 아니다. 실제 적용 단계에서 버전과 라이선스 및 기존 엔진 기능 재사용 가능성을 확인한다.
