# 하드웨어 핀 설정

이 문서는 S32K144 보드 데모에서 사용한 핀 설정과 주변장치 구성을 노드별로 정리합니다. 목적은 실제 보드 연결을 다시 확인하거나, 같은 구조를 재구성할 때 어떤 핀이 어떤 역할로 사용됐는지 빠르게 파악하는 것입니다.

이 저장소에는 S32DS 프로젝트 설정 파일과 SDK 생성 설정 파일을 포함하지 않습니다. 아래 표는 데모 소스의 board profile, SDK binding, runtime I/O 구성에서 사용한 핀과 주변장치 기준으로 정리했습니다.

---

## 1. 노드 이름 기준

| 내부 이름 | 공개 문서 역할명 | 설명 |
|---|---|---|
| `master` | LIN/CAN 조정 노드 | LIN 센서 상태를 확인하고 CAN 응답 노드로 명령을 전달하는 노드 |
| `slave1` | CAN 응답 노드 | CAN 명령을 받아 LED/button 상태로 응답하는 노드 |
| `slave2` | LIN 센서 노드 | ADC 값을 분류하고 LIN status frame으로 응답하는 센서 노드 |

---

## 2. LIN/CAN 조정 노드

소스 경로:

- `s32k_cooperative/S32K_LinCanBle_master`
- `s32k_rtos/RTOS_S32K_LinCanBle_master`

이 노드는 LIN master, CAN command sender, UART console 출력, 보조 UART 명령 입력 경로를 함께 사용합니다.

### 2.1 핀 설정

| 기능 | 주변장치 / 채널 | MCU 핀 | 방향 | 비고 |
|---|---|---|---|---|
| CAN RX | CAN0 RXD | PTE4 | 입력 | CAN transceiver 수신 경로 |
| CAN TX | CAN0 TXD | PTE5 | 출력 | CAN transceiver 송신 경로 |
| LIN transceiver enable | PORTE[9] GPIO | PTE9 | 출력 | LIN transceiver enable 제어 |
| LIN RX | LPUART2 RXD | PTD6 | 입력 | LIN master 수신 경로 |
| LIN TX | LPUART2 TXD | PTD7 | 출력 | LIN master 송신 경로 |
| Console UART RX | LPUART1 RXD | PTC6 | 입력 | PC console 입력 경로 |
| Console UART TX | LPUART1 TXD | PTC7 | 출력 | PC console 출력 경로 |
| Secondary UART RX | LPUART0 RXD | PTA2 | 입력 | 외부 UART peer 또는 BLE 모듈 명령 입력 경로 |
| Secondary UART TX | LPUART0 TXD | PTA3 | 출력 | 외부 UART peer 또는 BLE 모듈 응답 출력 경로 |

### 2.2 주변장치 설정 요약

| 주변장치 | 설정 요약 |
|---|---|
| FlexCAN | instance: CAN0<br>mode: `FLEXCAN_NORMAL_MODE`<br>nominal bitrate: 500 kbit/s<br>data bitrate: 1000 kbit/s<br>CAN FD: enabled<br>payload size: 16 bytes |
| LIN | instance: LPUART2<br>bitrate: 9600 bit/s<br>autobaud: disabled |
| Console UART | instance: LPUART1<br>baudrate: 115200 bit/s |
| Secondary UART bridge | instance: LPUART0<br>baudrate: 9600 bit/s |
| Timer | instance: LPTMR0 |
| FreeRTOS tick 기준 | CPU clock: 48 MHz |

---

## 3. LIN 센서 노드

소스 경로:

- `s32k_cooperative/S32K_Lin_slave`
- `s32k_rtos/RTOS_S32K_Lin_slave`

이 노드는 ADC 입력을 센서 상태로 분류하고, LIN slave status frame으로 조정 노드에 응답합니다.

### 3.1 핀 설정

| 기능 | 주변장치 / 채널 | MCU 핀 | 방향 | 비고 |
|---|---|---|---|---|
| ADC sensor input | ADC0_SE12 | PTC14 | 입력 | 센서 상태 분류에 사용하는 analog input |
| GPIO output | PORTD[0] GPIO | PTD0 | 출력 | 보드 동작 확인용 GPIO 출력 |
| LIN transceiver enable | PORTE[9] GPIO | PTE9 | 출력 | LIN transceiver enable 제어 |
| LIN RX | LPUART2 RXD | PTD6 | 입력 | LIN slave 수신 경로 |
| LIN TX | LPUART2 TXD | PTD7 | 출력 | LIN slave 송신 경로 |
| Red LED / PWM output | FTM0_CH0 | PTD15 | 출력 | 센서 상태 표시용 red LED, active-low |
| Green LED / PWM output | FTM0_CH1 | PTD16 | 출력 | 센서 상태 표시용 green LED, active-low |

### 3.2 주변장치 설정 요약

| 주변장치 | 설정 요약 |
|---|---|
| ADC | instance: ADC0<br>channel: `ADC_INPUTCHAN_EXT12`<br>resolution: 12-bit<br>trigger: software trigger<br>sample time: 255 |
| LIN | instance: LPUART2<br>bitrate: 9600 bit/s<br>autobaud: enabled |
| FTM PWM | instance: FTM0<br>mode: edge-aligned PWM<br>frequency: 1000 Hz<br>channels: CH0, CH1<br>initial duty setting: 4000 |
| Timer | instance: LPTMR0 |
| FreeRTOS tick 기준 | CPU clock: 48 MHz |

---

## 4. CAN 응답 노드

소스 경로:

- `s32k_cooperative/S32K_Can_slave`
- `s32k_rtos/RTOS_S32K_Can_slave`

이 노드는 CAN command를 수신하고, LED와 버튼으로 emergency/OK 흐름을 확인합니다.

### 4.1 핀 설정

| 기능 | 주변장치 / 채널 | MCU 핀 | 방향 | 비고 |
|---|---|---|---|---|
| CAN RX | CAN0 RXD | PTE4 | 입력 | CAN transceiver 수신 경로 |
| CAN TX | CAN0 TXD | PTE5 | 출력 | CAN transceiver 송신 경로 |
| GPIO output | PORTD[0] GPIO | PTD0 | 출력 | 보드 동작 확인용 GPIO 출력 |
| Red LED | PORTD[15] GPIO | PTD15 | 출력 | emergency/ACK 표시용 red LED, active-low |
| Green LED | PORTD[16] GPIO | PTD16 | 출력 | normal/ACK 표시용 green LED, active-low |
| OK button | PORTC[12] GPIO | PTC12 | 입력 | active-low OK request button |

### 4.2 주변장치 설정 요약

| 주변장치 | 설정 요약 |
|---|---|
| FlexCAN | instance: CAN0<br>mode: `FLEXCAN_NORMAL_MODE`<br>nominal bitrate: 500 kbit/s<br>data bitrate: 1000 kbit/s<br>CAN FD: enabled<br>payload size: 16 bytes |
| GPIO button | active level in code: active-low |
| LED output | active level in board profile: active-low |
| Timer | instance: LPTMR0 |
| FreeRTOS tick 기준 | CPU clock: 48 MHz |

---

## 5. 실제 보드 연결 확인 항목

| 항목 | 확인 내용 |
|---|---|
| CAN transceiver wiring | CAN0 TX/RX가 transceiver 및 CANH/CANL bus에 연결되어 있는지 확인 |
| LIN transceiver wiring | LPUART2 TX/RX와 PTE9 enable 핀이 LIN transceiver에 연결되어 있는지 확인 |
| UART console | LPUART1 PTC6/PTC7의 TX/RX 방향이 USB-UART adapter와 맞는지 확인 |
| 보조 UART 브리지 | LPUART0 PTA2/PTA3의 TX/RX 방향이 외부 UART peer 또는 BLE 모듈과 맞는지 확인 |
| ADC input | PTC14에 의도한 analog input range가 들어오는지 확인 |
| Button | PTC12 active-low 입력이 보드 pull-up/pull-down 구성과 맞는지 확인 |
| LEDs | PTD15/PTD16 active-low LED 동작이 실제 표시 의도와 맞는지 확인 |
