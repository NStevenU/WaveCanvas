<div align="center">

<img src="logo.png" alt="WaveCanvas Logo" width="480"/>

# WaveCanvas
### ESP32-S3 SoundFont2 (SF2) & Roland LA-32 Hybrid MIDI Synthesizer

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build%20Passed-orange?logo=platformio)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-N16R8%20(8MB%20Octal%20PSRAM)-blue?logo=espressif)](https://www.espressif.com/)
[![SoundFont2](https://img.shields.io/badge/Audio-SoundFont2%20(SF2)%20%2B%20LA--32-brightgreen)](https://github.com/schellingb/TinySoundFont)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**WaveCanvas**는 ESP32-S3(N16R8)의 240MHz 듀얼 코어와 8MB Octal SPI PSRAM을 기반으로 구축된 **독립형 임베디드 MIDI 신디사이저 및 사운드 모듈**입니다.  
구형 DOS 노트북 및 레트로 PC의 **SoftMPU / RS-232 시리얼 직결**을 지원하며, **32 동시 발음수(32-Voice Polyphony)**, **SoundFont2 뱅크 라우팅**, **Roland LA-32 커스텀 음색 소프트웨어 합성 엔진**, **32-bit Float DSP 파이프라인**, **1990년대 후반 레트로 웹 GUI(Nexisson Tech 1998 컨셉)** 를 탑재하고 있습니다.

[면책 안내](#-프로젝트-성격-및-면책-안내-project-disclaimer) • [주요 특징](#-주요-특징-key-features) • [하드웨어 갤러리](#-3d-프린팅-케이스--하드웨어-갤러리) • [제작 & 조립 가이드](#-만능기판-회로-설계--본체-조립-가이드) • [스피커 제작 가이드](#-외장-스피커-모듈-제작--음향-밀폐-노하우) • [핀맵](#-하드웨어-핀맵-hardware-pinout) • [조작 가이드](#-oled-ui-및-로터리-엔코더-조작법) • [드라이버 설정](#-레트로-pc-연결-및-필수-드라이버-설정-가이드) • [웹 관리자](#-1990년대-후반-클래식-웹-관리자-web-gui) • [빌드 가이드](#-빌드-및-업로드-방법-build--flashing)

---

</div>

<br>

> [!IMPORTANT]
> ### ⚠️ 프로젝트 성격 및 면책 안내 (Project Disclaimer)
> * **개인 연구 및 취미 프로젝트**: 본 펌웨어는 상용 수준의 완제품이나 완전무결한 하드웨어 클론(ASIC 에뮬레이터)이 아니며, 개인적인 연구와 취미 목적으로 설계된 독립형 사운드 모듈입니다.
> * **호환성 및 버그 발생 가능성**: MIDI 시퀀스 및 파일 구성, 비표준 SysEx 패킷 등에 따라 예기치 못한 오동작, 모드 판정 오차, 버그 등이 발생할 수 있습니다.
> * **문서와 코드 간의 차이**: 지속적인 기능 추가 및 최적화 과정에서 실제 소스코드와 문서 간에 일부 세부 설명의 차이가 존재할 수 있습니다.

<br>

## 🚀 주요 특징 (Key Features)

### 1. 🎼 하이브리드 사운드 엔진 (SoundFont2 + Roland LA-32 합성) & 3대 규격 지원 현황

| 규격 (Standard) | 구현도 & 별점 | 구현된 핵심 기능 (Implemented) | 미구현 및 플랫폼/시대적 한계 (Limitations & Context) |
| :--- | :---: | :--- | :--- |
| **`[GM]`<br>General MIDI<br>(Level 1 / 2 일부)** | **88 / 100**<br>★★★★☆ | • **Bank 0 128종 GM 표준 악기** 완벽 지원<br>• **Ch 10 Standard Drum Kit** 및 온음(2.0f) 피치벤드<br>• CC 7(Volume), CC 11(Expression), CC 10(Pan), CC 64(Hold1)<br>• RPN 00 00 (피치 벤드 감도 제어)<br>• **GM2 마스터 볼륨 SysEx** (`7F 7F 04 01`)<br>• **GM2 12음 Scale / Octave Tuning SysEx** (`7E 7F 08 08`)<br>• GM Reset (`7E 7F 09 01`) 및 채널 컨트롤러 일괄 복구 | • ESP32-S3 하드웨어 리소스 기반 32 동시 발음수 한계<br>• **GM2 전용 사운드 컨트롤러(CC 70~79 일부) 미구현**<br>*(※ 1998년 기기 컨셉 상 1999년에 제정된 GM2 풀 스펙 대신 1998년 당시 산업 표준인 정통 GM1 / Roland GS 규격 중심 설계)* |
| **`[GS]`<br>Roland GS<br>(SC-55 호환)** | **78 / 100**<br>★★★★☆ | • **CC 0 Bank Select 128종 변형 악기(Variation)** 스와핑<br>• **8종 Roland GS 리버브 매크로** (Room 1~3, Hall 1~2, Plate, Delay, Pan Delay)<br>• **8종 Roland GS 코러스 매크로** (Chorus 1~4, FB, Flanger 등)<br>• **10종 Roland 전용 드럼 킷** (TR-808, Power, Room, SFX 1/2 등)<br>• **Dual Drum Kit** (Part Mode 0x15, 드럼 채널 2개 동시 발음)<br>• **Drum Key NRPN** (0x18 피치, 0x1A 볼륨, 0x1C 팬, 0x1D 컷오프)<br>• **Drum Choke Group** (Hi-Hat, Triangle, Cuica 음소거 연동)<br>• **SC-55 전면 LCD 16자 텍스트 Display SysEx** (`41 .. 45 12 10`)<br>• **Roland GS 2-Band 파라메트릭 마스터 EQ SysEx** 제어<br>• Roland SC-55 하이브리드 단일 채널 Bank 127 라우팅<br>• RPN/NRPN 수신 시 Null-RPN(`0x7F7F`) 자동 초기화 (피치 왜곡 방지) | • SC-88/Pro 전용 다중 EFX 인서션 이펙트 (하드웨어 DSP 한계)<br>• Roland SC-55 롬 리비전별(v1.00/v1.20/mkII) 미세 폴백 에뮬레이션 (SoundFont 프리셋 대체 방식)<br>• 100% ASIC 하드웨어 클론이 아닌 사운드폰트 기반 실용 호환 |
| **`[MT-32]`<br>Roland MT-32<br>(CM-32L 호환)** | **80 / 100**<br>★★★★☆ | • 스마트 MT-32 모드 자동 감지 (SysEx $\to$ Note-On 채널 분석)<br>• **Bank 127 128종 MT-32 음색 맵** 및 Ch 10 MT-32 Rhythm Set<br>• **12반음(1옥타브) MT-32 고유 피치 벤드 레인지**<br>• **Roland LA-32 소프트웨어 합성 엔진 탑재** ([la32_synth.cpp](file:///Users/kimtaeheon/Documents/GitHub/WaveCanvas/src/la32_synth.cpp))<br>• **Custom Timbre Dump SysEx** (`41 .. 16 12 04/08`) 4-파셜(Square, Saw, PCM) + TVF/TVA 5단계 소프트웨어 엔벨로프 실시간 합성<br>• MT-32 전용 벨로시티 매핑 LUT (`MT32_VELO_LUT`)<br>• **MT-32 전면 LCD 20자 텍스트 Display SysEx** (`41 .. 16 12 20`) OLED 토스트 표출<br>• **Roland CM-32L Presence 고역대 쉐이핑 Biquad 필터**<br>• MT-32 Reset SysEx (`41 .. 16 12 7F`) | • Roland LA32 원본 ASIC 1:1 회로 에뮬레이션 아님 (SF2 + SW 하이브리드)<br>• **LA-32 커스텀 팀버 소프트웨어 발음수 최대 4보이스(16파셜) 제한** (CPU 부하 최적화)<br>• 표준 프리셋은 SoundFont Bank 127 샘플로 발음 (메모리 절약형 고음질 하이브리드) |

### 2. 🎹 32 동시 발음수 (32-Voice Polyphony) & 가중치 기반 보이스 스틸링
* **32-Voice Polyphony**: ESP32-S3의 연산 성능 한계 내에서 최적화된 32보이스 동시 발음 관리.
* **Gervill & FluidSynth 가중치 기반 보이스 스틸링**:
  $$\text{KillScore} = 1.5 \times \text{ReleaseProgress} + 1.0 \times (1.0 - \text{VolumeDecay})$$
  - 릴리즈 단계에 진입하여 감쇠 중인 보이스를 우선적으로 회수.
  - 서스테인이 유지되는 활성 지속음(패드/스트링/리드)을 최대한 보호하여 자연스러운 화음 전환 유지.

### 3. 🔌 DOS SoftMPU / 구형 레트로 PC 직결 & FreeRTOS 시퀀서
* **RS-232 to TTL UART 연동 (MAX3232)**: 구형 노트북 9핀 COM 포트 직결 (SoftMPU Roland MPU-401 에뮬레이션 호환).
* **Baud Rate 지원**: `38,400 bps` (SoftMPU / DOS Serial 기본값), `31,250 bps` (표준 MIDI In), `115,200 bps` (고속 시리얼).
* **일괄 락(Batch Mutex Locking) 시퀀서**:
  - `midi_sequencer.cpp`에서 틱당 개별 뮤텍스 획득 방식을 일괄 락(Batch Lock)으로 구성하여 FreeRTOS 코어 간 락 경합 오버헤드 절감 및 안정적인 연주 템포 유지.

### 4. 🏛️ 32-bit Float DSP 파이프라인 & 음향 처리
* **32-bit Float 내부 연산 파이프라인**: 렌더링 $\to$ 코러스 $\to$ 리버브 $\to$ 마스터 EQ $\to$ 스피커 EQ 전 과정을 32비트 부동소수점으로 처리하여 연산 오차 및 클리핑 왜곡 최소화.
* **Freeverb 기반 스테레오 스튜디오 리버브**: 80Hz 하이패스 필터와 15ms 프리딜레이를 내장하여 저음 타격감을 보존하면서 공간감 형성.
* **Roland GS 8종 스테레오 코러스**: 위상 간섭을 최소화한 모듈레이션 음장감 구현.
* **단조 유리수 마스터 리미터 (Monotonic Rational Limiter)**:
  - 디지털 풀스케일 0.75f 초과 피크 신호를 0.98f로 완만하게 점근 수렴시켜 하드 클리핑 방지.
* **무음 감지 Auto-Bypass (CPU 절전)**:
  - 무음 지속 시 Core 1 DSP 연산을 바이패스하여 불필요한 CPU 연산 방지.
* **8-Band 어쿠스틱 파라메트릭 EQ (Speaker EQ) & 모노 다운믹스**:
  - 외장 도킹 스피커 장착(`GPIO 47 ➔ GND`, Active LOW) 시 자동으로 모노 다운믹스 및 하우징 음향 보정 필터(85Hz HPF, 430Hz 노치, 1.5k~2.5kHz 보컬 부스트) 적용.

### 5. 📊 실시간 성능 계측 & 디버그 텔레메트리 (Zero-Overhead Toggle)
* **컴파일 타임 제로 오버헤드 스위치**:
  * [include/debug_monitor.h](file:///Users/kimtaeheon/Documents/GitHub/WaveCanvas/include/debug_monitor.h)의 `#define ENABLE_DEBUG_METRICS` 매크로로 활성화/비활성화 전환.
  * **비활성화 시 (`// #define ENABLE_DEBUG_METRICS`)**: 모든 계측 매크로가 no-op(`do {} while(0)`)으로 치환되고 웹 디버그 탭이 컴파일에서 100% 제거되어 **0 바이트 / 0 CPU 사이클**의 완전한 제로 오버헤드 달성 (배포/커밋 기본 모드).
  * **활성화 시**: Core 0/1 CPU 로드(%), 내부 온도(°C), 11.6ms 렌더링 블록 소요시간(us), 오버런 카운터, I2S DMA 버퍼 상태, 오버런 블랙박스 로그(최근 10건), PSRAM 파편화율 실시간 계측 및 웹 진단 대시보드(`/?tab=debug`) 활성화.

### 6. 🖥️ OLED UI & 1990년대 클래식 웹 관리자 (Nexisson Tech 1998)
* **800kHz Fast-mode Plus I2C OLED (SSD1306)**: Core 0 백그라운드 태스크로 UI 렌더링 분리.
* **Flash I/O 목록 메모리 캐싱**: 메뉴 진입 시 플래시 접근을 방지하여 재생 중 메뉴 조작 시 오디오 버퍼 언더런 방지.
* **1990s 레트로 Web GUI**: HTML 3.2 / 4.0 감성 디자인, 플레이어, 사운드폰트 관리자, Wi-Fi 설정, 시스템 설정, 가상 피아노(Easter Egg), 실시간 디버그 모니터 지원.
* **8비트 아케이드 레트로 미니게임 5종**: 가상 피아노, 핑퐁, 블록 쌓기(코로베이니키), 벽돌깨기, 스네이크 내장.

---

<br>

## 🎨 3D 프린팅 케이스 & 디자인 리소스 ([Case/](Case/))

[Case/](Case/) 디렉토리에는 3D 프린터로 직접 외형을 출력하고 커스텀할 수 있는 모델링 파일 및 라벨/로고 그래픽 에셋이 포함되어 있습니다:

| 분류 | 파일명 | 설명 |
| :--- | :--- | :--- |
| **본체 3D STL** | `Main Upper.stl` | 본체 상판 케이스 |
| | `Main Under.stl` | 본체 하판 케이스 |
| | `Main Knob.stl` | 로터리 엔코더 전용 다이얼 노브 |
| | `Main SPK Socket.stl` | 본체 측면 스피커 도킹 소켓 브라켓 |
| **스피커 3D STL** | `SPK Upper.stl` | 외장 스피커 모듈 상판 케이스 |
| | `SPK Under.stl` | 외장 스피커 모듈 하판 케이스 |
| | `SPK Port.stl` | 저역 강화용 베이스 리플렉스 포트 덕트 |
| | `SPK Connector.stl` | 본체 체결용 5핀 스피커 커넥터 브라켓 |
| **라벨 & 로고 에셋** | `Label.ai` | 본체 및 스피커 후면 단자 라벨 인쇄 원본 (Adobe Illustrator) |
| | `WaveCanvas Nano RS Logo.svg` | 본체 상판 레이저 각인용 벡터 로고 |
| | `WaveCanvas Nano Spk Logo.svg` | 스피커 상판 레이저 각인용 벡터 로고 |

---

<br>

## 🖼️ 하드웨어 외형 및 스피커 사진

### 1. 본체 외형 (WaveCanvas Nano RS)
| 전면 (Front) | 윗쪽 사선 (Top Isometric) |
| :---: | :---: |
| <img src="image/6.jpeg" alt="본체 전면" width="360"/> | <img src="image/7.jpeg" alt="본체 윗쪽 사선" width="360"/> |
| **아랫쪽 사선 (Bottom Isometric)** | **후면 단자부 (Rear Panel)** |
| <img src="image/8.jpeg" alt="본체 아랫쪽 사선" width="360"/> | <img src="image/9.jpeg" alt="본체 후면 단자부" width="360"/> |

### 2. 외장 스피커 모듈 (WaveCanvas Nano SPK)
| 전면 (Front) | 윗쪽 사선 (Top Isometric) | 후면 (Rear) |
| :---: | :---: | :---: |
| <img src="image/10.jpeg" alt="스피커 전면" width="240"/> | <img src="image/11.jpeg" alt="스피커 윗쪽 사선" width="240"/> | <img src="image/12.jpeg" alt="스피커 후면" width="240"/> |

### 3. 본체 + 스피커 도킹 시스템 (Docking System)
| 결합 전 전면 (Before Docking) | 결합 후 전면 (Docked Front) | 결합 후 후면 (Docked Rear) |
| :---: | :---: | :---: |
| <img src="image/13.jpeg" alt="결합 전 전면" width="240"/> | <img src="image/14.jpeg" alt="결합 후 전면" width="240"/> | <img src="image/15.jpeg" alt="결합 후 후면" width="240"/> |

---

<br>

## 🛠️ 만능기판 회로 설계 & 본체 조립 설명

저는 표준 만능기판(Perfboard)을 활용하여 제작했습니다:

| 만능기판 제작 전면 | OLED / PCM5102 연결 상태 |
| :---: | :---: |
| <img src="image/16.jpeg" alt="만능기판 전면" width="360"/> | <img src="image/17.jpeg" alt="OLED 및 PCM5102 연결 상태" width="360"/> |
| **만능기판 후면 배선** | **케이스 내부 조립 완료** |
| <img src="image/18.jpeg" alt="만능기판 후면 배선" width="360"/> | <img src="image/19.jpeg" alt="케이스 내부 조립 완료" width="360"/> |

### 💡 내부 설계 및 조립 순서 노하우
1. **모듈 결합 방식**:
   * 로터리 엔코더는 만능기판에 직접 납땜(Direct Solder)하여 기계적 강도를 확보합니다.
   * PCM5102A DAC, 0.96" OLED, MAX3232 모듈은 핀헤더 소켓 방식으로 구성하여 유지보수 및 탈부착이 용이하게 제작합니다.
2. **케이스 내부 장착 방법**:
   * **PCM5102A DAC 장착**: 약 2mm 두께의 폼 양면테이프를 케이스 바닥에 부착하고 그 위에 DAC 모듈을 완충 안착시킵니다.
   * **0.96" OLED 고정 (Heat Staking)**: OLED를 전면 베젤 홈에 끼운 후, 나사 홀 쪽에 튀어나온 플라스틱 기둥을 인두기 등으로 가열하여 넓게 퍼트려 단단히 고정합니다.
   * **만능기판 & ESP32-S3 장착**: OLED 위에 만능기판을 얹고, 최상단 핀헤더에 ESP32-S3를 결합합니다.
   * **USB-C 단자 고정**: USB-C 커넥터 부위를 케이스에 레진 또는 에폭시 접착제로 견고하게 고정합니다.
   * **기판 일체형 고정**: 전면 로터리 엔코더 나사 너트를 체결하면 만능기판 전체가 케이스에 완벽하게 일체형으로 고정됩니다.

---

<br>

## 🔊 외장 스피커 모듈 제작

외장 스피커는 소형 인클로저에서도 단단하고 풍부한 저음을 낼 수 있도록 베이스 리플렉스 포트와 음향 밀폐 튜닝이 적용되었습니다:

| 스피커 내부 부품 배치 | 스피커 연결 핀맵 (Rear Pinout) |
| :---: | :---: |
| <img src="image/20.jpeg" alt="스피커 내부 부품 배치" width="360"/> | <img src="image/21.jpeg" alt="스피커 연결 핀맵" width="360"/> |

### 💡 인클로저 제작 및 밀폐 팁
1. **유닛 및 커넥터 고정**:
   * 스피커 풀레인지 유닛과 5핀 커넥터 브라켓을 케이스에 레진 또는 에폭시 접착제로 견고하게 접착합니다.
   * **완전 밀폐(Acoustic Sealing)**: 케이블 최종 납땜/연결 후, 커넥터 뒷면 틈새를 글루건으로 두껍게 도포하여 공기 누출 및 잡음을 원천 차단합니다.
2. **저역 덕트(포트) 필터 장착**:
   * `SPK Port.stl` 배출 구멍에 마스크 부직포(내부 필터층)를 적당한 크기로 잘라 부착합니다.
3. **앰프 모듈 및 상/하판 결합**:
   * 앰프 모듈은 인클로저 내부 벽면에 절연 부착합니다.
   * 상판과 하판 케이스 결합 부위에 순간접착제(순접) 또는 에폭시를 둘러 **완전 밀폐(Sealed Enclosure)** 결합합니다.
4. **도킹 핀맵 & 자동 음향 보정 (SPK_DET)**:
   * **핀맵 배열 (Image 21)**: `5V`, `GND`, `Spk_Detect`, `L Out`, `AGND`
   * 본체에 스피커를 결합하면 `Spk_Detect` 핀이 GND로 접촉(`GPIO 47 Active LOW`)되어 0.05초 만에 자동으로 **8-Band Speaker Parametric EQ 및 모노 다운믹스**가 활성화됩니다.

---

<br>

## 📐 하드웨어 핀맵 (Hardware Pinout)

**기본 보드**: ESP32-S3 DevKitC-1 / YD-ESP32-S3 (N16R8: 16MB Flash, 8MB Octal PSRAM)

```
                       +------------------------+
                       |      ESP32-S3 N16R8    |
                       +------------------------+
                                |   |   |   |
         +----------------------+   |   |   +----------------------+
         | (I2S DAC)                |   |             (RS-232 MIDI)|
         |                          |   |                          |
+------------------+                |   |                +------------------+
|  PCM5102A Module |                |   |                |  MAX3232 Module  |
|  BCLK   -> GPIO15|                |   |                |  RX     -> GPIO18|
|  LRC    -> GPIO16|                |   |                |  TX     -> GPIO17|
|  DOUT   -> GPIO7 |                |   |                |  VCC/GND         |
|  SCK/GND-> GND   |                |   |                +------------------+
+------------------+                |   |                          |
                                    |   |                 [DB9 COM / SoftMPU]
         +--------------------------+   +--------------------------+
         | (I2C OLED)                                (Rotary & SW) |
         |                                                         |
+------------------+                                    +------------------+
| SSD1306 OLED     |                                    | EC11 Encoder     |
| SDA     -> GPIO8 |                                    | S1 (CLK)-> GPIO4 |
| SCL     -> GPIO9 |                                    | S2 (DT) -> GPIO5 |
| Addr    :  0x3C  |                                    | KEY(SW) -> GPIO6 |
| VCC/GND          |                                    | VCC/GND          |
+------------------+                                    +------------------+
```

### 📋 상세 연결 표

| 구분 | 모듈 / 부품 | 핀 이름 | ESP32-S3 GPIO | 비고 |
| :--- | :--- | :--- | :--- | :--- |
| **I2S DAC** | PCM5102A | **BCK (BCLK)** | `GPIO 15` | I2S 비트 클럭 |
| | | **LCK (LRCK)** | `GPIO 16` | I2S 좌/우 워드 클럭 |
| | | **DIN (DOUT)** | `GPIO 7` | I2S 직렬 오디오 데이터 |
| | | **SCK / SCL** | `GND` | PCM5102A 내부 PLL 활성화 |
| | | **XMT** | `3.3V` | Soft Mute 해제 (소리 출력) |
| **MIDI UART** | MAX3232 | **RXD** | `GPIO 18` | 노트북 TX(Pin 3) $\to$ ESP32 RX |
| | | **TXD** | `GPIO 17` | ESP32 TX $\to$ 노트북 RX(Pin 2) |
| **OLED (I2C)** | SSD1306 0.96" | **SDA** | `GPIO 8` | I2C 데이터 (800kHz Fast-mode Plus) |
| | | **SCL** | `GPIO 9` | I2C 클럭 |
| | | **VCC / GND** | `3.3V / GND` | OLED 전원 |
| **로터리 엔코더** | EC11 Encoder | **S1 (CLK)** | `GPIO 4` | 볼륨 조절 / 메뉴 이동 |
| | | **S2 (DT)** | `GPIO 5` | 회전 방향 판정 |
| | | **KEY (SW)** | `GPIO 6` | 클릭(온스크린 메뉴 진입), 롱클릭(Panic) |
| | | **VCC / GND** | `3.3V / GND` | 엔코더 전원 및 접지 |
| **상태 인디케이터**| 온보드 WS2812 | **RGB LED** | `GPIO 48` | 대기(오렌지), AP모드(블루), 연주(화이트 펄스) |
| **스피커 감지** | 외장 모듈 핀 | **SPK_DET** | `GPIO 47` | **Active LOW** (GND 접촉 시 모노 전환 & 8밴드 PEQ) |

---

<br>

## 🖥️ OLED UI 및 로터리 엔코더 조작법

### 📺 1. 실시간 메인 디스플레이 (Main Display)

<div align="center">
  <img src="image/22.jpeg" alt="WaveCanvas OLED 실시간 메인 디스플레이" width="480"/>
</div>

* **상단 헤더 (Yellow 영역)**: 
  * **실시간 시계**: SNTP 인터넷 동기화 시간 표출 (`PM 11:35`)
  * **신디사이저 모드 뱃지**: 작동 모드 및 정책 표출
    * **스마트 자동 (Auto)**: 미디 시퀀스 및 SysEx에 따른 자동 전환 (`[A: GM]`, `[A: GS]`, `[A: MT-32]` 또는 `[MT-32]`)
    * **수동 고정 (Manual)**: 사용자가 지정한 서브 모드로 강제 고정 (`[M: GM]`, `[M: GS]`, `[M: MT-32]`)
  * **마스터 볼륨**: 현재 출력 음량 퍼센트 (`V:20%`)
* **가로 구분선**: 상태 헤더와 메인 연주 영역 분리선
* **채널 및 악기 정보 (Blue 영역)**: 
  * 최근 연주된 활성 채널 번호 및 로드된 악기 프리셋명 (`Ch03 Honkytonk`)
  * 연주 중인 채널이 순차적으로 자동 전환되며 실시간 표출
* **16채널 실시간 VU 미터 (Blue 영역)**: 
  * 1번~16번 채널의 발음 벨로시티 강도에 따라 실시간 미터 바 상승
  * 무음 채널은 바닥선(`_`) 대기, 35ms 주기의 부드러운 감쇠(Decay) 애니메이션 적용

*(※ 5분 이상 무음 및 입력이 없을 경우 자동으로 화면 보호 절전 모드로 전환됩니다.)*


### 📋 2. 온스크린 설정 메뉴
엔코더 스위치를 클릭하여 온스크린 메뉴로 진입합니다 (8초간 미입력 시 자동 복귀):
* **`1. MIDI 보관함 (MIDI Library)`**: Flash에 저장된 `.mid` 파일 탐색, 선택/재생/정지
* **`2. 신스 모드 설정 (Synth Mode)`**: 스마트 자동 모드(Auto) / 수동 고정 모드(Manual: GM, GS, MT-32) 전환
* **`3. 사운드폰트 선택 (SoundFont Select)`**: *(고급 관리 모드 활성화 시 표시)* `.sf2` 목록 조회 및 로드
* **`4. Wi-Fi 정보 & IP (Wi-Fi Info & IP)`**: 작동 모드(AP/STA), SSID, IP 주소 확인
* **`5. MIDI 전송 속도 (MIDI Baud Rate)`**: 시리얼 MIDI 통신 속도 전환 (38400 / 31250 / 115200 bps)
* **`6. 오디오 테스트 (Audio Test)`**: 피아노 화음, 기타 아르페지오, 드럼 키트 음향 테스트
* **`7. 소리 출력 설정 (Audio Output)`**: 스테레오(Stereo) $\leftrightarrow$ 모노 다운믹스(Mono) 전환
* **`8. LED 상태표시등 (LED Status)`**: 온보드 RGB LED 켜기/끄기 설정
* **`9. 언어 설정 (Language)`**: 한국어 (Korean) / English 지원
* **`10. 긴급 리셋 (Panic)`**: 16채널 강제 무음화 및 컨트롤러 리셋
* **`11. 기기 정보 (About)`**: 펌웨어 버전, 시스템 상태(온도, 가용 Heap, PSRAM, LittleFS) **(※ 이 화면에서 1.0초 롱프레스 시 🎮 레트로 게임실 진입)**

### 🎮 3. 이스터에그: 8비트 아케이드 레트로 게임실
`10. 기기 정보 (About)` 화면에서 엔코더 버튼을 **1.0초간 길게 누르면** 미니게임 5종을 실행할 수 있습니다:
1. **`1. 가상 피아노 (Virtual Piano)`**: 건반 선택 및 발음
2. **`2. 핑퐁 (Pong)`**: 충돌 사운드 및 패들 조작
3. **`3. 블록 쌓기 (Block Stack)`**: 코로베이니키 MIDI BGM + 효과음
4. **`4. 벽돌깨기 (Brick Breaker)`**: 높이별 주파수 파괴음 및 승리 팡파레
5. **`5. 스네이크 (Snake)`**: 방향 조작 및 먹이 섭취 사운드  
*(※ 플레이 중 **엔코더 버튼을 5초간 길게 누르면 게임을 종료**하고 메뉴로 복귀합니다.)*

---

<br>

## 🔌 레트로 PC 연결 및 필수 드라이버 설정 가이드

WaveCanvas는 구형 노트북 및 레트로 PC의 9핀 RS-232 COM 포트 직결을 완벽하게 지원합니다. 연결하는 운영체제(OS) 환경에 맞추어 아래의 드라이버 및 유틸리티를 설정해 주세요.

### 1. 🕹️ MS-DOS 게임 환경 (SoftMPU)
DOS 게임에서 Roland MT-32 또는 General MIDI / Sound Canvas BGM을 출력할 때 **SoftMPU** 유틸리티를 사용합니다.

* **권장 통신 속도 (Baud Rate)**: `38,400 bps` (WaveCanvas OLED 메뉴 `7. 통신 속도` $\to$ `38400` 설정)
* **SoftMPU 구동 명령어 예시**:
  ```bat
  REM COM1 포트로 MPU-401 MIDI 신호를 38,400bps로 전송
  SOFTMPU.EXE /MPU:330 /OUTPUT:COM1

  REM 사운드 블래스터 호환 인터럽트 연동 (MT-32 지능형 모드 지원 게임용)
  SOFTMPU.EXE /SB:220 /IRQ:5 /MPU:330 /OUTPUT:COM1
  ```
* **게임 내 사운드 설정**: 사운드 설정 메뉴(Setup/Install)에서 `Roland Sound Canvas (SC-55/SCC-1)`, `General MIDI`, 또는 `Roland MT-32 (Port 330h)`를 선택합니다.

---

### 2. 🪟 Windows 95 / 98 / ME / 2000 / XP 레트로 환경 (Roland Serial MIDI Driver)
Windows 레트로 PC의 시리얼(COM) 포트를 표준 MIDI 출력 장치로 사용하려면 **Roland 공식 시리얼 MIDI 드라이버**가 필요합니다.

* **필수 드라이버**: **Roland Serial MIDI Driver v3.2**
* **드라이버 설정 방법**:
  1. `Roland Serial MIDI Driver v3.2`를 설치합니다.
  2. 제어판의 드라이버 설정에서 연결된 **COM 포트 (예: `COM1`)**를 지정합니다.
  3. 통신 속도(Baud Rate)를 **`38,400 bps`**로 맞춥니다.
  4. 제어판 `멀티미디어(사운드)` $\to$ `MIDI 출력 장치`를 **`Roland Serial MIDI Out`**으로 지정합니다.
  5. WaveCanvas 본체의 통신 속도도 **`38400 bps`**로 일치시킵니다.

---

<br>

## 🌐 1990년대 후반 클래식 웹 관리자 (Web GUI)

* **기본 AP 모드 접속 정보**:
  - **SSID**: `WaveCanvas-NanoRS`
  - **비밀번호**: *(비밀번호 없음 / 오픈 AP)*
  - **접속 주소**: `http://192.168.4.1` (포트 80)

### 📑 웹 탭 구성 및 스크린샷

| 1. MIDI 플레이어 (Player) | 2. 와이파이 설정 (Wi-Fi Setup) |
| :---: | :---: |
| <img src="image/1.png" alt="MIDI 플레이어" width="360"/> | <img src="image/2.png" alt="와이파이 설정" width="360"/> |
| **3. 시스템 설정 (Settings)** | **4. 사운드폰트 관리자 (SoundFonts)** |
| <img src="image/3.png" alt="시스템 설정" width="360"/> | <img src="image/4.png" alt="사운드폰트 관리자" width="360"/> |

| 5. 가상 피아노 (Virtual Piano - Easter Egg) |
| :---: |
| <img src="image/5.png" alt="가상 피아노" width="540"/> |

1. **MIDI 플레이어 (Player - `image/1.png`)**:
   - 파일 업로드(`.mid`), 라이브러리 관리, 재생/일시정지/정지, 실시간 마스터 볼륨 슬라이더, 원클릭 오디오 테스트.
2. **와이파이 설정 (Wi-Fi Setup - `image/2.png`)**:
   - 2.4GHz 무선 공유기 검색 및 연결 관리.
3. **시스템 설정 (Settings - `image/3.png`)**:
   - 마스터 볼륨, MIDI 통신 속도, 오디오 모드(Stereo/Mono), LED 점등 모드, NTP 시간 동기화, 고급 모드 토글, MIDI Panic.
4. **사운드폰트 관리자 (SoundFonts - `image/4.png`)**:
   - *(고급 모드 활성화 시 표시)* 플래시 용량 게이지, `.sf2` 업로드/삭제/로드.
5. **가상 피아노 (Virtual Piano - `image/5.png`)**:
   - 25건반 인터랙티브 피아노, 악기 프리셋 변경, 옥타브 쉬프트, PC 키보드 연주 지원.
6. **실시간 디버그 모니터 (Debug Telemetry - `ENABLE_DEBUG_METRICS` 활성화 시)**:
   - Core 0/1 부하율, 칩 온도, 11.6ms 렌더링 블록 소요시간 및 오버런 로그, PSRAM 파편화율 실시간 대시보드 (`/?tab=debug`).

---

<br>

## 🛠️ 빌드 및 업로드 방법 (Build & Flashing)

이 프로젝트는 [PlatformIO](https://platformio.org/) 환경에서 빌드됩니다.

### 1. 사전 준비 (Prerequisites)
* [VS Code](https://code.visualstudio.com/) 및 [PlatformIO IDE 확장](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) 설치
* [Python 3.x](https://www.python.org/) 설치 (MT-32 ROM 데이터 변환용)
* ESP32-S3 N16R8 보드를 USB로 PC에 연결

### 2. 프로젝트 클론
```bash
git clone https://github.com/your-username/WaveCanvas.git
cd WaveCanvas
```

### 3. 저작권 바이너리 준비 및 소스코드 생성 (필수)
저작권 보호를 위해 SoundFont 및 MT-32 PCM ROM 바이너리는 저장소에 포함되어 있지 않으며 `.gitignore`에 등록되어 있습니다. 빌드 전 아래 2가지 작업을 반드시 수행해야 합니다:

1. **MT-32 PCM 데이터 생성**:
   - `data/` 디렉토리에 **`MT32_PCM.ROM` (512KB)** 파일을 배치합니다.
   - 변환 스크립트를 실행하여 `src/mt32_pcm_data.cpp`를 생성합니다:
     ```bash
     python3 generate_mt32_pcm.py
     ```
2. **사운드폰트 배치**:
   - `data/` 디렉토리에 기본 사운드폰트 **`CT4MGM.SF2`**를 배치합니다.

### 4. LittleFS 파일시스템 업로드
사운드폰트(`.SF2`)를 ESP32-S3 Flash 파티션으로 전송합니다:
```bash
pio run --target uploadfs
```

### 5. (선택) 디버그 텔레메트리 스위치 설정
* 기본 배포/커밋 상태에서는 제로 오버헤드를 위해 [include/debug_monitor.h](file:///Users/kimtaeheon/Documents/GitHub/WaveCanvas/include/debug_monitor.h)의 디버그 스위치가 비활성화되어 있습니다:
  ```cpp
  // include/debug_monitor.h 13번째 라인
  // #define ENABLE_DEBUG_METRICS   <-- 주석 처리 시 0바이트/0오버헤드 (기본값)
  #define ENABLE_DEBUG_METRICS      <-- 실시간 성능 계측 및 웹 디버그 탭 활성화 시 주석 해제
  ```

### 6. 펌웨어 빌드 및 업로드
```bash
# 펌웨어 컴파일 및 업로드
pio run --target upload

# 시리얼 모니터 확인 (115200 bps)
pio device monitor
```

---

<br>

## 📂 프로젝트 구조 (Directory Structure)

```
WaveCanvas/
├── Case/                      # 3D 프린팅 STL 모델링 및 라벨/로고 디자인 리소스
│   ├── Main Upper.stl         # 본체 상판 케이스 3D 모델
│   ├── Main Under.stl         # 본체 하판 케이스 3D 모델
│   ├── Main Knob.stl          # 로터리 엔코더 전용 다이얼 노브
│   ├── Main SPK Socket.stl    # 본체 측면 스피커 도킹 소켓 브라켓
│   ├── SPK Upper.stl          # 외장 스피커 상판 케이스 3D 모델
│   ├── SPK Under.stl          # 외장 스피커 하판 케이스 3D 모델
│   ├── SPK Port.stl           # 스피커 저역 강화용 베이스 리플렉스 포트 덕트
│   ├── SPK Connector.stl      # 스피커 5핀 커넥터 브라켓
│   ├── Label.ai               # 본체/스피커 후면 단자 라벨 디자인 원본 (Illustrator)
│   ├── WaveCanvas Nano RS Logo.svg   # 본체 상판 레이저 각인 벡터 로고
│   └── WaveCanvas Nano Spk Logo.svg  # 스피커 상판 레이저 각인 벡터 로고
├── include/
│   ├── config.h               # 하드웨어 핀맵, 32보이스, 512 버퍼 크기, 기본 설정
│   ├── audio_engine.h         # 오디오 엔진 & 32-bit Float DSP 파이프라인 인터페이스
│   ├── tsf.h                  # TinySoundFont 코어 (PSRAM 적재, Biquad 필터, 보이스 스틸링)
│   ├── la32_synth.h           # Roland LA-32 소프트웨어 합성 엔진 (MT-32 커스텀 음색)
│   ├── debug_monitor.h        # 실시간 성능 계측 & 제로 오버헤드 텔레메트리 스위치
│   ├── chorus_fx.h            # Roland GS 8종 스테레오 코러스 엔진
│   ├── reverb_fx.h            # Freeverb 3D 스테레오 스튜디오 리버브 & 8종 GS/MT-32 매크로
│   ├── master_eq.h            # Roland GS 2-Band 파라메트릭 마스터 EQ
│   ├── speaker_eq.h           # 8-Band 어쿠스틱 PEQ (외장 도킹 스피커 음향 보정 & 다운믹스)
│   ├── midi_parser.h          # UART2 시리얼 MIDI 파서 & MT-32/GS/GM 3대 규격 엔진
│   ├── midi_sequencer.h       # SMF Type 0/1 시퀀서 & 일괄 락(Batch Lock) 디스패처
│   ├── display_ui.h           # SSD1306 OLED UI & 16ch VU 미터 렌더러
│   ├── game_engine.h          # 8비트 아케이드 레트로 미니게임 5종 엔진
│   ├── encoder_input.h        # EC11 로터리 엔코더 인터럽트 디바운스
│   ├── led_indicator.h        # WS2812 RGB LED 상태 인디케이터
│   ├── wifi_manager.h         # SoftAP / STA Wi-Fi 비동기 연결 관리
│   ├── web_manager.h          # 1990s 클래식 Web GUI & REST API 매니저
│   ├── time_manager.h         # SNTP 인터넷 시간 동기화 및 RTC 관리
│   ├── mt32_pcm_data.h        # MT-32 PCM 데이터 인터페이스 (Flash PROGMEM)
│   ├── mt32_prog_data.h       # MT-32 128종 Timbre/Patch 프리셋 정의 데이터
│   ├── tetris_midi_data.h     # 미니게임용 내장 코로베이니키(Korobeiniki) MIDI 데이터
│   ├── u8g2_font_galmuri9.h   # OLED 한글 출력을 위한 갈무리9 비트맵 폰트
│   ├── icon_data.h            # Web UI 및 OLED 인덱스 아이콘 비트맵
│   ├── logo_data.h            # 부팅 화면용 고해상도 로고 비트맵
│   └── oled_logo_data.h       # OLED 128x64 전용 부팅 로고
├── src/
│   ├── main.cpp               # 시스템 초기화 및 메인 루프 (외장 스피커 도킹 감지)
│   ├── audio_engine.cpp       # FreeRTOS Core 1 오디오 태스크 & 단조 유리수 리미터
│   ├── la32_synth.cpp         # LA-32 파셜 합성, TVF 필터 및 엔벨로프 구현
│   ├── debug_monitor.cpp      # 실시간 성능 계측, CPU 부하, 오버런 블랙박스 로그
│   ├── midi_parser.cpp        # MIDI 수신, GS 리셋, MT-32 LCD 토스트, SysEx 디코딩
│   ├── midi_sequencer.cpp     # FreeRTOS Core 0 시퀀서 태스크
│   ├── display_ui.cpp         # 800kHz Fast I2C UI 루프 & 파일 목록 스마트 캐싱
│   ├── game_engine.cpp        # 아케이드 게임 5종 로직 (테트리스, 벽돌깨기 등)
│   ├── encoder_input.cpp      # 로터리 엔코더 이벤트 처리
│   ├── led_indicator.cpp      # RGB LED 색상 제어
│   ├── wifi_manager.cpp       # Wi-Fi 이벤트 핸들러
│   ├── web_manager.cpp        # HTTP 웹서버 라우팅, 가상 피아노, 파일 매니저, 디버그 API
│   ├── time_manager.cpp       # 시간 포맷팅 및 NTP 동기화
│   └── mt32_pcm_data.cpp      # (.gitignore 대상: generate_mt32_pcm.py로 자동 생성)
├── data/                      # LittleFS Flash 파일시스템 루트 (.gitignore 대상)
│   ├── CT4MGM.SF2             # (사용자 준비) SoundFont2 사운드폰트
│   ├── MT32_PCM.ROM           # (사용자 준비) Roland MT-32 512KB PCM 롬
│   └── logo.png               # Web GUI 로고 이미지 리소스
├── icon/                      # Web UI 소스 아이콘 리소스 (.png)
├── image/                     # 하드웨어 외형, 조립 사진, Web GUI 스크린샷 (1~22)
├── generate_mt32_pcm.py       # MT-32 ROM -> C++ 소스코드 자동 디코더/생성기
├── fix_roland_checksum.py     # MIDI 파일 내 Roland SysEx 체크섬 검증 및 보정 스크립트
├── partitions_16MB.csv        # 16MB Flash 파티션 테이블 (LittleFS / App / OTA)
├── platformio.ini             # PlatformIO 빌드 설정 (-O3, -ffast-math, ESP32-S3)
└── README.md                  # 프로젝트 문서
```

---

<br>

## 📜 라이선스 및 크레딧 (License & Credits)

* **WaveCanvas Core & Firmware**: [MIT License](LICENSE) © 2026 NSteven.
  - *Custom Audio Pipeline*: Roland LA-32 소프트웨어 합성 엔진, SC-55 하이브리드 Bank 127 라우터, 32-bit Float DSP 체인(리버브, 코러스, 8-Band 스피커 PEQ, 단조 유리수 마스터 리미터), 제로 오버헤드 성능 계측 텔레메트리, 8비트 칩튠 신스 독자 설계.
* **SF2 Parsing Core**: [TinySoundFont](https://github.com/schellingb/TinySoundFont) by **Bernhard Schelling** (MIT License)
  - *Modifications*: ESP32-S3 8MB Octal PSRAM 스트리밍 로더 이식, Biquad 필터 복원, 32-Voice Polyphony 확장.
* **Synthesis & Voice Allocation References**:
  - **[Munt (MT-32 Emulator)](https://github.com/munt/munt)** by **Sergey V. Mikayev & Munt Team** (LGPL): Roland LA-32 소프트웨어 합성 구조 및 MT-32 벨로시티 매핑 LUT 참조.
  - **[Gervill (OpenJDK)](https://github.com/openjdk/jdk/tree/master/src/java.desktop/share/classes/com/sun/media/sound)** by **Karl Helgason**: 보이스 릴리즈 감쇠 가중치 스틸링 공식 및 마스터 게인 커브 참고.
  - **[FluidSynth](https://www.fluidsynth.org/)** by **Peter Hanappe & Team** (LGPL): 사운드폰트 엔벨로프 감쇠 및 보이스 라이프사이클 참조.
* **DSP Reverb Engine**: **Freeverb** by **Jezar at Dreampoint** (Public Domain).
* **Display & Graphics Driver**: [U8g2](https://github.com/olikraus/u8g2) by **olikraus** (BSD 2-Clause License).
* **Korean Bitmap Font**: **[Galmuri9 (갈무리9)](https://github.com/quiple/galmuri)** by **Lee Minseo** (SIL Open Font License 1.1).
* **LED Driver**: [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) by **Adafruit** (LGPL-3.0 License).
* **Async Web Server**: [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) by **me-no-dev** & **AsyncTCP** (LGPL-3.0 License).
* **JSON Parser**: [ArduinoJson](https://arduinojson.org/) by **Benoît Blanchon** (MIT License).

<br>

### 🏷️ 가상 브랜드 및 로고 안내 (Fictional Branding Notice)
* **Nexisson Tech Co., Ltd.** 및 `Nexisson`, `WaveCanvas`의 명칭과 로고는 1990년대 후반 고전 음향기기 분위기를 내기 위해 창작된 **100% 가상의 브랜드(Fictional Entity)**입니다.
* 현실의 특정 실존 기업이나 단체와 무관하며, 프로젝트에 포함된 **로고, 아이콘, 텍스트, 디자인 등 모든 브랜드 에셋은 누구나 제약 없이 자유롭게 수정, 사용, 배포**하실 수 있습니다.

<br>

### 🎵 음원 저작권 및 출처 고지 (SoundFont & ROM Attribution)
* 본 펌웨어의 기본/권장 사운드폰트로 명시된 **`CT4MGM.SF2`**는 **Creative Technology Ltd. / E-mu Systems SoundFont 2.0 포맷**을 기반으로 고전 사운드 모듈 재생을 위해 커뮤니티에서 널리 활용되는 뱅크입니다.
* 저작권 및 배포 라이선스 준수를 위해 **사운드폰트(`.SF2`) 및 MT-32 PCM ROM 바이너리는 본 GitHub 저장소에 포함되어 있지 않으며**, 사용자가 직접 준비하여 기기에 업로드하거나 변환 스크립트를 실행해야 합니다.
* 본 프로젝트는 **구형 DOS/레트로 PC 환경에서 시리얼 MIDI 연주 및 고전 게임 사운드, 미디파일을 즐기기 위한 비상업적 개인 취미 목적**으로 제작되었으며, 사운드폰트 및 MT-32 ROM 내 샘플/음색 데이터의 모든 저작권과 지식재산권은 해당 원저작권자(Creative / E-mu / Roland Corp.)에게 귀속됩니다.

<br>

### 🤖 AI 어시스턴트 활용 고지 (AI-Assisted Development)
본 프로젝트의 펌웨어 구현, 성능 최적화 및 문서 작성 과정에서 아래의 AI 모델들을 활용하여 페어 프로그래밍 및 교차 검증을 수행했습니다:
* **Gemini 3.7 Flash (High)**: 시스템 전체 아키텍처 설계, 주요 기능 구현, 소스코드 생성 및 수정
* **Claude 4.6 Opus (Thinking)**: 정밀 코드 전수 검사, 엣지 케이스 및 버그 탐색, 문제 해결 및 성능 최적화 분석
* **GitHub Copilot Auto**: 실시간 코드 작성 보조 및 교차 검증 (더블 체크)

<br>

<div align="center">

**WaveCanvas**로 고전 게임과 미디 명곡들의 감동적인 사운드를 생생하게 경험해 보세요! 🎹✨

</div>
