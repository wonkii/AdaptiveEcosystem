# 02 — 행동 의도·조향·이주 통합

상태·공통 경계는 [README](README.md)를 따른다. Epic의 목표→조향→회피→이동 분리를 참고하되 기존 Native가 위치를 최종 갱신한다.

## 현재 Source와 선행 감사

`EcoWorldV3.h`: DecideHerb/Commit → Control → WildFlight 보정 → HerbControl → 약화/날씨 → 이동·지형 검증.
`EcoPolicyV3IntegratedProcessors.cpp::IntegratedControlHook`: Journey 또는 opt-in Social → life 기억·LifeLocomotion·NaturalShelterControl·체형 배수 순으로 적용된다. 이주 자체에도 같은 계획 개체를 찾는 별도 전체 순회가 있다. Native KinGrid만 바꾸고 이 비용을 누락하지 않는다.

먼저 모든 Velocity/Gait/EatMult/DrainMult/Behavior writer를 표로 만든다. 함수별 조건·우선순위·단위·관측 시점·소비 시점·실제 위치 writer를 기록한다. 같은 동작을 재현하는 함수 추출부터 시작하며 우선순위 변경은 별도 diff로 둔다.

## 목표 인터페이스 — 제안

제어 의도에는 StableAgentId, step/generation, 출처(Policy/Flight/Journey/Social/Life), 목표 또는 선호 방향, 희망 속도, 정지 의도, 유효기간을 담는다. 결과에는 최종 변위/step, 보행·섭식·대사 배수, 적용·거절 이유를 둔다. Raw Request와 실행 결과를 따로 유지한다.

계산 순서:

1. 현재 세계 규칙이 확정한 실행 행동과 유효한 여행·슬롯 목표를 읽는다.
2. 기존 먹이/도주/은신/여행 방향으로 preferred movement를 만든다.
3. [01](01_NEIGHBOR_AVOIDANCE.md)의 이동 이웃으로 분리/회피를 계산한다. 현재 Separation과 신규 분리를 중복 가산하지 않도록 profile별 하나의 소유자를 둔다.
4. 기존 약화·체형·날씨 속도 제한과 행동별 정지 조건을 적용한다. 약화 등을 두 번 곱하지 않는다.
5. 기존 지형·장애물 제약으로 실제 이동을 검증하고 기존 Native 단계에서 한 번 적분한다.
6. 실제 위치로 슬롯 도착·이주·장부/표현에 feedback한다. 의도 위치로 도착 처리하지 않는다.

단계3의 삽입 위치와 속도 제한 순서가 현재 결과를 바꿀 수 있다. 실험 profile 계약에서 확정하고 OFF는 기존 호출 순서를 그대로 보존한다. 회피 출력이 Freeze/Sleep/도착 crouch를 임의로 이동시키거나 피난 목표를 취소하지 않게 한다. 정지 개체도 다른 이동 개체에게는 장애물 후보다.

## Herd와 Journey

무리 응집은 같은 Persistent Herd의 제한된 이웃/기존 요약을 비교한다. 현재 보이는 사슴 전체 중심과 의미가 다르므로 별도 flag·RL 계약이 필요하다. 동일 Herd이더라도 간격 유지를 제외하지 않는다. 이주 중 후보는 PlanId로 분리하되 충돌 후보는 다른 계획도 포함할 수 있도록 물리/논리 필터를 나눈다.

Plan 조회 키와 실제 map 키를 감사한다. 현재 지역 순차 이주와 legacy 계획의 인계 차이를 새 helper에서 숨기지 않는다. 기존 Social 예약 lease·안전방향·개인 긴급 도주 우선과 실패 feedback을 보존한다.

## 완료 조건

OFF/함수 추출은 기존 행동·관측·난수·장부 비교 통과. ON은 도주 중 통로 교차, 무리 합류, 다른 계획 교차, 은신처 제거/lease 만료, 잠든 개체 주변 이동을 검증한다. 1 step당 위치 적분1회, invalid 의도 적용0, 반복 보정의 이중 속도 배수0. Target 도착률·위협 방향 역주행·겹침을 [05](05_VALIDATION.md)로 평가한다.

Epic `UMassApplyMovementProcessor`와 Native를 같은 entity에 동시 연결하지 않는다. 향후 backend 교체는 이 문서 밖의 별도 migration 계약이다.
