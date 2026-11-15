# OrbitLink_App 개발 계획 (경연 파트만)

## 범위
Emu 일정표의 경연 단계는 Sender/Receiver 태스크, 큐 통신, Emergency 우선 처리, 기본 송수신 테스트다.
XOR, Logger, Retry는 이 앱의 범위가 아니다. 다른 팀원이 이어서 붙일 자리만 주석으로 표시한다.

## 만드는 것
`OrbitLink_Queue`의 큐 모듈(`msg_queue.c`, 헤더)을 그대로 쓰고, 그 위에서 도는 기본 송수신 앱 하나.
- Sender 3종(Emergency, System, Normal), Receiver 1개
- 자동 판정 테스트. 모든 메시지 수신, 같은 유형 안의 순서, payload 무결성, 유형별 우선 처리
- 기존 `main_demo.c`는 큐 실험용 가짜 태스크 모음이라 그대로 두고, 새 파일 `src/app_main.c`만 추가한다.

## 순서
1. 큐 모듈 복사 후 빌드 확인
2. `app_main.c` 작성
3. 빌드와 실행, 자동 판정 통과 확인, 반복 실행
4. 이어받을 사람을 위한 설명(README)과 스크린샷
