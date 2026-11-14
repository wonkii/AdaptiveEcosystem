# 04 — 표현 LOD·계산 주기·사건 기반 갱신

상태·공통 경계는 [README](README.md)를 따른다. Epic MassGameplay의 Representation/Simulation/Replication LOD 구분을 참고한다. 엔진의 LOD를 모두 사용하는 것은 구현 목표가 아니며 기존 체계와 중복을 먼저 조사한다.

## L0 — 기존 경로 감사

Creature/Runtime의 실제 표현 선택·풀 소유자를 검색하고 `Creature/Representation/EcoCreatureRepresentationActor.*`, `EcoCreatureAnimInstance.*`, `Network/Mass/EcoMassClientBubble.*`까지 추적한다. client 카메라·복제 관심 범위·Actor tick/animation·IK·collision·Native simulation의 제어 변수를 표로 작성한다. 기존 기능이 없다고 가정해 새 manager를 추가하지 않는다.

## L1 — 표현 비용 우선

가까운 Actor, 저비용 먼 표현, 숨김의 역할을 정의한다. ISM/VAT는 후보이며 사슴 체형·털색·성장·애니메이션 아트 지원을 확인한 뒤 채택한다. 새 표현이 준비되지 않으면 기존 Actor 수/animation·IK 예산 개선을 먼저 비교한다.

viewer별 거리/가시성/최대 개체 수, hysteresis, 최소 유지 시간, 전환 frame 예산을 둔다. 수치는 target machine 측정으로 확정한다. 표시 OFF나 Actor pooling이 논리 entity 사망/생태 중단을 일으키지 않아야 한다. client마다 표현 LOD가 달라도 authority 인구·먹이는 동일해야 한다. 사냥 판정에 필요한 권위 collision과 화면 표현 제거를 연결해 공격을 무효화하지 않는다.

## L2 — 사건과 느린 응집

기존 Herd Alarm과 Migration request/feedback의 증거 시각·수신 시각·TTL을 재사용한다. Mass Signal 자체를 증거 payload 저장소로 사용하지 않는다. payload는 기존 상태에 두고 signal은 갱신 필요 알림으로만 사용한다.

응집 갱신 간격은 실험적으로1/2/4 decision step을 비교한다. 캐시에 ID·snapshot token·최종 계산 simulation time·만료·변위 한도를 포함한다. Herd/Plan 변경·대상 사망·context 이전·직접 위협·유효 경보 시 invalidation/우선 갱신한다. 동일 사건 반복은 병합하고 priority·deadline·age로 starvation을 방지한다.

직접 위협/포획·현재 물리 이동을 느린 응집 예산으로 지연하지 않는다. 모든 개체가 동시에 경보를 받는 경우의 최고 비용을 별도 측정한다. 불가피한 deadline 미달은 통계에 남기고 안전한 기존 경로 사용 비용도 포함한다.

## L3 — 생태 시뮬레이션 주기

관찰자 거리만으로 Native step을 끄거나 dt를 늘리지 않는다. 먹이 경쟁·포획·도주·대사·RNG 결과가 바뀐다. 현재 요청의 첫 구현에는 생태 timestep 변경을 포함하지 않는다. 필요하면 시간 적분·이벤트 순서·자원 보존·offscreen interaction의 별도 coarse simulation 계약을 작성한다.

## 완료 조건

Client2의 서로 다른 카메라, late join, 반복 relevance 진입/이탈, pooling 재바인딩, 죽음·성장·이주 중 전환을 검사한다. 표현 OFF에도 동일 authority 궤적/장부를 유지한다. 거리 threshold 진동·Actor spawn spike·CPU/GPU/메모리·복제 bytes를 분리 계측한다.

느린 응집은 stale 적용0·사건 누락0을 검사하고 추가 반응 지연은 기본 경로 대비 최대1 decision step 이내를 초기 제안 gate로 둔다. 품질/학습 계약에서 수용하지 않으면 활성화하지 않는다. 이는 실측 통과 수치가 아니다.
