# 01 — 이동용 이웃 탐색과 국소 회피

상태·공통 경계는 [README](README.md)를 따른다. 근거는 [Epic Avoidance 및 엔진 확인 위치](REFERENCES.md)다.

## 현재 문제와 범위

`EcoSpatialIndexV3.h`는 후보25% 초과 시 전체 순회하며 원본 row 순서로 계산한다. `EcoObservationV3.h::PerceiveKin`은 KinCount/FleeCount/KinAlarm/ToCentroid/Separation을 같이 만든다. 이 결과를 하나의 제한 표본으로 대체하면 관측·조향이 바뀐다.

정확 관측 경로와 이동용 근사 경로를 별도 출력으로 다룬다. 초기 정책 지각은 기존 의미를 유지한다. 이 상태에서는 회피만 빨라져도 전체 동족 지각은 여전히 병목일 수 있으므로 end-to-end 결과를 함께 측정한다.

## 입출력 계약 — 제안

- 입력 snapshot: RegionId, context generation, simulation step, StableAgentId, row, XY 위치, 실행 속도, 몸 반경, 생존 여부. HerdId/PlanId는 응집/여행 필터에만 사용하며 다른 무리도 물리적 간격 대상이다.
- 반환: 후보 ID/row와 snapshot token, 방문 셀 수, 읽은 레코드 수, 유효 후보 수, 기여 collider 수, saturation/invalid 이유. stale token은 소비 전에 거절한다.
- 생존/동일 층/자기 제외 등 필터를 명시한다. 현재2.5D backend에서 겹친 동굴층 지원을 가정하지 않는다. 지역 경계 이웃의 범위는 현재 통합 계약을 보존하고 별도 cross-region query로 몰래 확장하지 않는다.

## 단계별 구현

1. 정확 경로: 기존 FullScan/Grid25를 oracle로 두고 k-way merge·bitset 순회·동일 snapshot 셀 범위 캐시를 비교한다. 세부는 [기존 P1](../BOUNDED_NEIGHBOR_SEARCH_SPEC_20261110.md)을 따른다.
2. 제한 경로: 방문 셀 C, 읽은 항목 B, 반환 후보 K의 세 예산을 둔다. 반경만 제한해도 밀집 셀 순회는 커질 수 있다. 초기 시험은 B={24,48,96}, K={6,12,24}; C는 기준선의 셀 점유 분포로 정하고 기록한다. Epic 상수를 프로젝트 기본값으로 즉시 복사하지 않는다.
3. 셀은 가까운 순서, 동점은 셀 좌표/level로 처리한다. 항목은 안정된 ID 기반 순서로 결정성을 보존하고 방향별 quota/유지 hysteresis를 비교한다. 월드 RNG를 소비하지 않는다. 매번 cell 전체를 정렬해 예산을 우회하지 않도록 Build와 query 비용을 모두 계측한다.
4. B/C 소진 시 제한된 결과와 saturation을 반환한다. 정상 밀집 질의를 전역 순회로 되돌리지 않는다. invalid snapshot은 신규 출력을 적용하지 않고 기존 경로/중지 정책 중 사전에 정한 모드를 실행·기록한다. 이 예외 비용도 예산 측정에 포함한다.
5. shadow에서 현재 Separation과 제한 분리의 방향·크기 차이를 수집한다. 이후 실험 profile에서 상대 위치/속도·몸 반경·예측 시간으로 회피를 계산한다. 좌표는 Native1unit=200cm, 속도는 cm/s인지 Native 변위/step인지 경계에서 명시 변환한다.

예측 회피는 기존 세계와 다른 알고리즘이다. Static obstacle/terrain 검사, 늑대 포획, 정책 위협 지각을 후보 K로 자르지 않는다. 모든 충돌 방지·정확한 최근접 K를 보장한다고 설명하지 않는다. ORCA는 비교 자료이며 이번 구현의 필수 패키지가 아니다.

## 검증과 완료 조건

동일 좌표1,024개에서도 셀/항목/출력 예산 준수. 제외 후보로 예산이 소진되는 경우와 자기만 남는 경우를 검사한다. 출력 순서·동점·반복 실행 재현성을 확인한다. birth/death/reorder/migration/load 후 잘못된 row 소비0. 정면 교차·압축된 무리·좁은 통로에서 최소 간격·겹침·진동·정체를 기록한다.

정확 경로는 기존 관측/결과 비교 통과가 필수다. 근사 경로는 shadow 성공과 실제 행동 품질 통과를 구분한다. [05](05_VALIDATION.md)의 품질 gate와 RL 계약이 없으면 production 적용 완료가 아니다.

대상 파일: `AI/Policy/EcoSpatialIndexV3.h`, `EcoObservationV3.h`, `EcoWorldV3.h`, `EcoRepertoire.h`, `Tools/EcoSpatialBenchmark/`. 필요 시 엔진 비의존 helper를 같은 모듈에 추가한다.
