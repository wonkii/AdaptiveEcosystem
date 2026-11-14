# 05 — 기준선·검증·채택과 되돌리기

공통 경계와 실행 순서는 [README](README.md), 기존 정확 경로 oracle는 [공간 탐색 기록](../SPATIAL_SEARCH_20261106.md)을 따른다. 이 문서의 수치는 **시험 목표이며 달성 기록이 아니다**.

## 비교 모드와 결과물

LegacyFull, LegacyGrid25, ExactCandidateImproved, BoundedShadow, BoundedActive를 명확히 구분한다. 여기에 worker/LOD/새 조향은 각각 독립 설정으로 기록한다. 명칭은 제안이며 실제 콘솔 명령이 아니다. 활성 backend·brain 지문·profile·엔진 버전·Git SHA·설정·seed·context 수·실제 개체 수·하드웨어를 실행 manifest에 남긴다.

원시 로그/trace는 프로젝트 Saved의 새 날짜별 폴더에 저장하고 이전 증거를 덮어쓰지 않는다. 요약 CSV/JSON과 검증 범위 문서는 Docs/Performance에 보관한다. 개인 발표 산출물은 AGENTS의 외부 폴더 규칙을 따른다.

## 시험 행렬

- 지역1/4/8 × 지역당128/1024 사슴. 늑대 수는 고정값을 manifest에 명시하고 사슴 최적화 전후 동일하게 유지한다.
- 균일 분산, 16무리×64, 4무리×256, 한곳 밀집, 동일 좌표, 좁은 통로 양방향, 도주 중 합류, 여러 계획의 이주 교차.
- 최소3seed, warmup30초+측정180초, 실행 순서 교차. 메모리 수명 문제는 별도30분 soak로 본다. 모든 경우를 한 번에 실행하기 전에 작은 정확성 fixture를 통과시킨다.
- Native harness와 실제 owned Unreal fixture를 분리한다. 다지역 Native loop 결과를 실제 월드 스케줄/지형/렌더 비용으로 대체하지 않는다.

## 지표와 gate

| 종류 | 필수 기록·초기 채택 기준 |
|---|---|
| 정확 경로 | 관측/로짓 bytes, 기존 주요 상태·RNG·행동·자원 비교 차이0; 기존4조건×900스텝과 신규 clustered fixture |
| 제한 경로 | 셀/항목/반환/collider 예산 초과0, invalid/stale 결과 적용0, 반복 실행 선택 재현 |
| 비용 | build/query/sort/perception/steering/trace/commit p50/p95/p99, 전체 step, GT·worker critical path, alloc/memory, saturation/fallback/cache hit |
| 성능 제안 gate | 실제 clustered1024의 동족 처리 p95 20% 이상 감소, 전체 step p95 회귀5% 이내. 전체 frame budget은 사용 환경별 별도 확정 |
| 이동 품질 제안 gate | 비포획·비초기겹침 fixture의 중첩 pair-step 비율 baseline 대비 +1%p 이내, 완료 가능한 경로 도착률 하락1%p 이내, 경로시간 p95 증가10% 이내 |
| 경보 제안 gate | 직접 위협 처리 누락0, 느린 응집의 추가 반응 지연 최대1 decision step |
| 생태 계약 | 자원 수지·receipt 중복0, 인구 lifecycle 일관성. 근사 ON의 생존·포획·섭식 분포는 RL 담당과 허용 편차를 정하기 전 production 승인 불가 |

중첩은 두 몸 반경 합보다 실제 거리가 작은 pair로 정의하고 fixture의 체형을 기록한다. 정체·진동은 임계 변위/시간/각도 정의를 실행 전에 고정한다. gate를 실패 후 느슨하게 바꾸려면 원인과 새 기준을 별도 결정 기록으로 남긴다.

## 필수 통합 검증

AGENTS에 따라 실행 중 UBT/dotnet/Editor/LiveCoding/MSBuild/ShaderCompileWorker를 확인한다. C++ 변경 후 승인된 환경의 직접 `UnrealBuildTool.exe AdaptiveEcosystemEditor Win64 Development`를 완료까지 기다린다. 기존 Policy·HerdMigration·Life·표현 회귀 필터의 정확한 이름을 코드에서 확인해 실행한다.

Standalone 및 Dedicated+Client2/late join에서 Client NativeStep0, 이동 writer1, 기존 M4 수지, 슬롯 실패·도착 feedback, 실제 지역 인계를 확인한다. birth/death/reorder/월드 종료/pending worker 중 저장 요청도 포함한다. Game/cook/package는 변경된 엔진 의존·표현 에셋·빌드 입력에 맞게 실행하고 Editor 성공만으로 패키지 성공을 기록하지 않는다.

## rollout과 rollback

정확 경로 → shadow → owned fixture opt-in → 통합 검증 → 기본 적용 판단 순서다. 실험 중 backend/profile 변경은 경계에서만 허용하고 pending 작업 drain·캐시 invalidation 후 적용한다. 중간 스텝 토글은 거절한다.

기존 경로를 보존하고 실패 시 해당 실험 flag를 OFF로 재시작한다. 새 persistent 설정이 있으면 checkpoint 호환성을 검사하고 지원하지 않는 슬롯은 명시적으로 거절한다. rollback은 코드만 되돌리고 호환되지 않는 저장을 조용히 복원하는 절차가 아니다.

완료 보고에는 구현/실험/보류, 실제 켠 설정, 실패와 한계, source·manifest·trace 위치를 기재한다. 후보 수 감소만으로 전체 성능 향상을 주장하지 않는다.
