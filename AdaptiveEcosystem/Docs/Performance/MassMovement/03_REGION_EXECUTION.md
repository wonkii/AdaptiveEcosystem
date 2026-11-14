# 03 — 다지역 snapshot·데이터 처리·실행 스케줄

상태·공통 경계는 [README](README.md)를 따른다. Processor 개수를 늘리거나 QueryExecutor API를 사용하는 것만으로 병렬화/성능 향상을 주장하지 않는다.

## 현재 근거

`EcoPolicyV3IntegratedProcessors.cpp`의 통합 Processor는 GameThread 실행을 요구하며 Mass Query로 수집한 view를 지역별 Native Step에 전달한다. life 활성 시 adapter의 FamilyRows/BodyBuckets 구성은 전체 Deer를 훑는 경로가 있어 여러 context에서 반복될 수 있다. 정확한 횟수와 변경 시점은 계측으로 확정한다.

## E0 — 접근·수명 감사

`EcoWorldV3::Step` 및 모든 callback을 순수 계산, UObject/Subsystem 읽기, 지형 trace, Mass fragment 쓰기, M4/생명주기 commit으로 분류한다. 월드 RNG, threat 차폐 scratch, 개체 row 재배열, shared buffer의 소유권을 표기한다. GT flag를 바로 제거하지 않는다.

## E1 — 중복 구축 제거

- context/step별 Build 횟수와 비용을 기록하고, 동일 입력 snapshot의 family lookup·공간 bucket·ID mapping은 공용 읽기 전용 데이터로 재사용한다.
- 캐시 키는 World epoch + 완료 경계 sequence + 구조 generation이다. 위치만 바뀌는 경우에도 공간 bucket의 generation을 갱신한다.
- 현재 지역 순회가 앞 지역의 갱신 위치를 뒤 지역에서 관찰하는지 먼저 확인한다. 전지역을 한 번 freeze하면 의미가 바뀔 수 있으므로 동일성 증명 없이 재사용하지 않는다.
- 누적 할당, 빈 지역, birth/death/migration/reorder/restore·World 종료를 검사한다. raw Mass 포인터를 작업 수명 밖으로 보관하지 않는다.

## E2 — 순수 단계 추출

GT gather → immutable snapshot → 순수 계산 → GT 검증/commit의 경계를 설계한다. 초기에는 같은 스레드에서 실행해 기존 결과와 정확 비교한다. native step 전체를 UObject hook과 함께 worker에 던지지 않는다. 순수 지각 또는 한정된 조향처럼 입력/출력이 명확한 단계부터 분리한다.

제안 작업 token: WorldEpoch, RegionId, ContextGeneration, StepSequence, InputRevision. 결과 token이 현재와 다르면 폐기하고 같은 경계에서 기존 동기 경로를 실행한다. 실패/취소가 부분 이동·부분 자원 commit으로 남지 않게 한다.

## E3 — 제한된 worker 실행

E2 동일성 통과 후에만 독립 작업을 병렬 dispatch한다. snapshot은 완료 barrier까지 유지하고 결과는 안정된 context/StableAgentId 순서로 반영한다. 지역별 RNG를 다른 순서로 소비하지 않는다. 이동/포획/자원처럼 순서 의존 단계는 우선 직렬로 남긴다.

GT 지형 질의는 별도 prepare/validate 단계로 유지한다. 사전 지형 snapshot이 실제 이동 검증을 대체할 수 있는지 검증되지 않으면 최종 trace를 보존한다. 이 비용 때문에 전체 가속이 제한될 수 있다. pending 작업이 있는 중 birth/death/여행 context 이전·저장·월드 teardown 시 취소/barrier 규칙을 둔다.

## 완료 조건

지역1/4/8×1,024에서 GT/worker 시간·barrier wait·복사/할당·전체 critical path를 기록한다. worker 수1/2/4와 다른 완료 순서에서도 동일 결과를 비교한다. 잘못된 token commit0, dangling pointer0, 중복 receipt0. thread sanitizer 등 도구 지원 여부는 환경별 명시하며 실행하지 않은 검사를 기록하지 않는다.

E1만 채택하고 E3를 보류할 수 있다. 현재 서버 목표 frame budget은 미확정이므로 [05](05_VALIDATION.md) 상대 gate와 실제 서비스 예산을 구분한다. Mass 처리 순서 문서는 실제 구현이 확정된 후 갱신한다.
