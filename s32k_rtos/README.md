# S32K FreeRTOS Version

이 디렉터리는 cooperative 버전에서 검증한 동일한 LIN/CAN 통신 정책을 FreeRTOS task 기반 실행 구조로 전환한 소스를 포함합니다.

| 디렉터리 | 역할 |
|---|---|
| `RTOS_S32K_LinCanBle_master` | LIN/CAN 조정 노드 |
| `RTOS_S32K_Lin_slave` | LIN 센서 노드 |
| `RTOS_S32K_Can_slave` | CAN 응답 노드 |

---

## Conversion goal

FreeRTOS 버전의 목적은 기능을 새로 만드는 것이 아니라, cooperative dispatcher에서 동작하던 통신 정책을 task ownership 기준으로 분리하는 것입니다.

핵심 전환 방향은 다음과 같습니다.

- 기존 application/service/driver 계층은 최대한 유지합니다.
- runtime 계층에서 FreeRTOS task 생성과 주기 실행을 담당합니다.
- 각 task는 기존 `AppCore_Task*` entry를 호출합니다.
- task 주기는 `vTaskDelayUntil()` 기반으로 유지합니다.
- task context의 시간 기준은 FreeRTOS tick에서 계산합니다.

---

## Task ownership

FreeRTOS 버전은 cooperative 버전의 주기 정책을 크게 바꾸지 않고,
각 주기 작업의 실행 주체를 FreeRTOS task 단위로 분리한 구조입니다.

| 노드 | Task 구성 | Ownership 기준 |
|---|---|---|
| LIN/CAN 조정 노드 | UART: 1 ms<br>LIN: 1 ms / 20 ms<br>CAN: 10 ms<br>render: 100 ms<br>heartbeat: 1000 ms | console 입출력<br>보조 UART bridge 처리<br>LIN 상태 확인 및 OK token 요청<br>CAN 명령/결과 처리<br>console snapshot rendering |
| LIN 센서 노드 | LIN fast: 1 ms<br>ADC: 20 ms<br>LED: 100 ms<br>heartbeat: 1000 ms | LIN slave 상태기계 처리<br>ADC sampling 및 zone classification<br>emergency latch 갱신<br>sensor/latch 상태 LED 표시 |
| CAN 응답 노드 | CAN: 10 ms<br>button: 10 ms<br>LED: 100 ms<br>heartbeat: 1000 ms | CAN 명령 수신 및 응답 처리<br>로컬 OK request 입력 처리<br>button debounce<br>emergency/ACK/normal LED pattern 표시 |

LIN/CAN 조정 노드에서는 LIN fast 처리와 LIN poll 처리를 하나의 LIN task에서 다룹니다.
이렇게 하면 LIN driver와 LIN module ownership이 여러 task로 흩어지지 않고,
LIN 관련 상태 변경 지점이 단순해집니다.

UART task는 console 입력과 보조 UART bridge 응답성을 위해 다른 주기 작업보다 한 단계 높은 priority로 설정했습니다.
나머지 task는 짧게 실행되는 주기 작업이므로 같은 scheduling group으로 묶었습니다.

각 task는 `vTaskDelayUntil()`을 사용해 FreeRTOS tick 기준으로 주기를 유지하며,
실행 후 대부분 block 상태로 돌아갑니다.

---

## What changed from the cooperative version

| 항목 | Cooperative version | FreeRTOS version |
|---|---|---|
| 실행 주체 | 직접 구현한 dispatcher | FreeRTOS scheduler |
| 주기 유지 | runtime tick 기반 due check | `vTaskDelayUntil()` |
| 모듈 호출 | 하나의 main loop에서 순차 호출 | task별 entry 분리 |
| 시간 기준 | board tick/runtime tick | FreeRTOS tick 기반 task context |
| 정책 | 동일 | 동일 |
| protocol | 동일 | 동일 |

FreeRTOS로 전환하면서 CAN/LIN protocol이나 recovery policy를 새로 만든 것은 아닙니다. 같은 시나리오를 더 명확한 task 단위로 나누어 실행 구조를 비교하기 위한 버전입니다.

---

## Notes

- 이 구조는 완전한 event-driven architecture가 아니라, 주기 task 기반 구조입니다.
- CAN/LIN 송수신, ADC sampling, console rendering처럼 주기가 다른 작업을 task 단위로 분리했습니다.
- task 간 직접 공유 상태가 생기는 부분은 service/module ownership을 기준으로 범위를 제한하는 것이 중요합니다.
- 일부 runtime에서는 stack high-water mark와 heap 상태를 관찰할 수 있도록 구성했습니다.
- 실제 제품 구조에서는 queue, event group, notification 등을 더 적극적으로 사용해 event-driven 구조로 확장할 수 있습니다.

자세한 비교는 `../docs/cooperative_vs_rtos.md`를 참고합니다.
