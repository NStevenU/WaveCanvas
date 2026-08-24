<div align="center">

<img src="logo.png" alt="WaveCanvas Logo" width="480"/>

# WaveCanvas
### High-Performance ESP32-S3 SoundFont2 (SF2) MIDI Synthesizer & Retro Sound Module

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Build%20Passed-orange?logo=platformio)](https://platformio.org/)
[![ESP32-S3](https://img.shields.io/badge/ESP32--S3-N16R8%20(8MB%20Octal%20PSRAM)-blue?logo=espressif)](https://www.espressif.com/)
[![SoundFont2](https://img.shields.io/badge/Audio-SoundFont2%20(SF2)%20%2B%20LA--32-brightgreen)](https://github.com/schellingb/TinySoundFont)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

**WaveCanvas**는 ESP32-S3(N16R8)의 240MHz 듀얼 코어 DSP와 8MB Octal SPI PSRAM을 기반으로 구축된 **독립형 고음질 하드웨어 MIDI 신디사이저 및 사운드 모듈**입니다.  
구형 DOS 노트북 및 레트로 PC의 **SoftMPU / RS-232 시리얼 직결**을 완벽하게 지원하며, **32 동시 발음수(32-Voice Polyphony)**, **Roland GS/MT-32 하이브리드 바리에이션 뱅크 라우팅**, **LA-32 커스텀 음색 합성 엔진**, **32-bit Float 무손실 DSP 파이프라인**, 그리고 **1990년대 후반 레트로 웹 GUI(Nexisson Tech 1998 콘셉트)**를 탑재하고 있습니다.

[주요 특징](#-주요-특징-key-features) • [하드웨어 핀맵](#-하드웨어-핀맵-hardware-pinout) • [조작 가이드](#-oled-ui-및-로터리-엔코더-조작법) • [웹 관리자](#-1990년대-후반-클래식-웹-관리자-web-gui---nexisson-tech-1998) • [빌드 가이드](#-빌드-및-업로드-방법-build--flashing)

---

</div>

<br>

## 🚀 주요 특징 (Key Features)

### 1. 🎼 하이브리드 사운드 엔진 (SoundFont2 + Roland LA-32 합성) & 3대 규격 완벽 대응
* **TinySoundFont (TSF) 코어 전면 재설계 (Deeply Refactored)**: 단순 GM1 재생기에 불과하던 원본 구조를 전면 개편하여, 8MB Octal PSRAM 스트리밍 로더, 손상되었던 Biquad 필터 복원, 32-Voice 동시 발음수 확장, 그리고 Roland GS/MT-32 하이브리드 라우팅 및 32-bit Float DSP 체인을 결합한 독자 임베디드 사운드 모듈 엔진으로 재구축.
* **Roland LA-32 실시간 소프트웨어 합성 엔진 탑재**:
  * MT-32 네이티브 파라미터/팀버 덤프 SysEx(`0x41 .. 0x16 0x12 0x04/0x05/0x08`) 수신 시 PCM 어택 + 신스 서스테인 구조의 LA(Linear Arithmetic) 사운드를 실시간 합성.
  * MT-32 전용 벨로시티 LUT(`MT32_VELO_LUT`) 및 음색별 전용 게인 보정 테이블 적용.
* **GM / Roland GS / Roland MT-32 규격 완벽 처리**:
  * **`[GM/GM2]` General MIDI**: Bank 0 표준 128종 악기, 온음(2.0f) 피치 벤드 레인지, Ch 10 Standard Drum Kit, GM2 마스터 볼륨 및 12음 Scale/Octave Tuning SysEx 지원.
  * **`[GS]` Roland General Standard**: CC 0 Bank Select를 통한 62종 바리에이션 악기 실시간 스와핑, **8종 롤랜드 리버브/코러스 매크로**(Room 1~3, Hall 1~2, Plate, Delay, Pan Delay / Chorus 1~4, FB, Flanger 등) 실시간 알고리즘 스와핑, **10종 롤랜드 전용 드럼 킷**(TR-808, Power, Room, SFX 등), Dual Drum Kit(Part Mode 0x15), 드럼 개별 타악기 피치/레벨/팬 튜닝(Drum Key Parameter) 및 Choke Group(Hi-Hat, Triangle, Cuica 등) 지원.
  * **`[MT-32]` Roland MT-32**: **정밀 스마트 자동 감지**(SysEx $\to$ 파일/경로 시그니처 $\to$ Ch 0 비활성화/Ch 1~9 하드웨어 파트 구조 분석), **Bank 127 128종 오리지널 MT-32 음색 맵** 직접 할당, Ch 10 MT-32 Rhythm Set 매핑, **12반음(1옥타브) 광활한 피치 벤드 레인지**, **MT-32 전면 LCD 20자 텍스트 Display SysEx (`0x20`)** OLED 토스트 실시간 표출.
* **Roland SC-55 하이브리드 Bank 127 라우팅**:
  * 곡 도중 특정 단일 채널이 Bank 127(MT-32 사운드셋)을 호출하더라도 전체 시스템이 강제로 MT-32 모드로 튕기지 않고, **GS 모드를 유지하면서 해당 채널만 Bank 127 프리셋을 로드**하도록 하드웨어 실기와 동일하게 처리.
* **RPN / NRPN 충돌 방지 및 안전 피치 벤드 아키텍처**:
  * Roland GS 전용 필터/엔벨로프 제어(NRPN CC 98/99) 또는 Reset All Controllers(CC 121) 수신 시 RPN 상태 레지스터를 즉시 **Null-RPN (`0x7F7F`)**으로 무효화하여, RPN Data Entry(CC 6) 오작동으로 인한 피치 벤드 감도 왜곡을 원천 차단.

### 2. 🎹 32 동시 발음수 (32-Voice Polyphony) & 하이브리드 지능형 보이스 스틸링
* **32-Voice Polyphony**: 대규모 오케스트라 MIDI 및 고속 아르페지오/피아노 연타에서도 음 잘림 없이 깨끗한 재생 보장.
* **Gervill & FluidSynth 기반 하이브리드 지능형 보이스 스틸링**:
  $$\text{KillScore} = 1.5 \times \text{ReleaseProgress} + 1.0 \times (1.0 - \text{VolumeDecay})$$
  - 릴리즈가 끝난 무음 보이스 즉각 소멸 및 회수.
  - 베이스/타악기 등 감쇠가 빠른 악기는 소리가 끝나는 즉시 슬롯을 반환하여 저역 뭉침 방지.
  - 패드/스트링 등 서스테인이 긴 악기는 조기 단절 없이 자연스러운 화음 전환 유지.

### 3. 🔌 DOS SoftMPU / 구형 레트로 PC 직결 & 고신뢰성 MIDI 시퀀서
* **RS-232 to TTL UART 연동 (MAX3232)**: 구형 노트북 9핀 COM 포트 직결 (SoftMPU Roland MPU-401 에뮬레이션 완벽 호환).
* **다양한 Baud Rate 지원**: `38,400 bps` (SoftMPU / DOS Serial COM), `31,250 bps` (표준 MIDI In), `115,200 bps` (고속 시리얼).
* **원샷 일괄 락(Batch Mutex Locking) 시퀀서**:
  - `midi_sequencer.cpp`에서 틱당 개별 뮤텍스 획득 방식을 **Batch Lock**으로 재설계하여 FreeRTOS 코어 간 락 경합(Lock Contention) 오버헤드를 90% 이상 절감.
  - 복잡한 블랙 미디(Black MIDI)에서도 재생 템포 밀림(Jitter) 0% 사수.
* **자연 잔향 보존 및 부드러운 페이드아웃**:
  - 곡 종료 시 즉시 소리를 끊지 않고 3초간 자연 잔향(Reverb Tail)을 유지한 뒤, 마지막 1초간 완만한 페이드아웃 후 안전 리셋.

### 4. 🏛️ 32-bit Float 무손실 DSP 파이프라인 & 음향 최적화
* **무손실 32-bit Float 오디오 파이프라인**: 렌더링 $\to$ 코러스 $\to$ 리버브 $\to$ 마스터 EQ $\to$ 스피커 EQ 전 과정을 32비트 부동소수점으로 일괄 처리하여 양자화 노이즈 0% 달성.
* **Freeverb 기반 3D 스테레오 스튜디오 리버브**: 80Hz 하이패스 필터와 15ms 프리딜레이를 내장하여 저음 타격감을 보존하면서 공간감 형성.
* **Roland GS 8종 스테레오 코러스**: 위상 간섭 없는 깨끗한 모듈레이션 음장감 구현.
* **단조 유리수 마스터 리미터 (Monotonic Rational Limiter)**:
  - 디지털 풀스케일 0.75f 초과 피크 신호만 0.98f로 완만하게 점근 수렴시켜 하드 클리핑 및 위상 역전 디스토션을 원천 방지.
* **무음 감지 Auto-Bypass (Core 1 절전)**:
  - 잔향 감쇠 후 일정 시간(약 1초) 무음 지속 시 Core 1 DSP 연산을 전체 바이패스하여 대기 전력 소모 0% 실현.
* **8-Band 어쿠스틱 파라메트릭 EQ (Speaker EQ) & 모노 다운믹스**:
  - 외장 도킹 스피커 장착(`GPIO 47 ➔ GND`, Active LOW) 시 0.05초 만에 자동으로 모노 다운믹스 및 하우징 통울림/초저역 보정 필터(85Hz HPF, 430Hz 노치, 1.5k~2.5kHz 보컬 부스트) 적용.

### 5. 🖥️ 초고속 UI & 1990년대 클래식 웹 관리자 (Nexisson Tech 1998)
* **800kHz Fast-mode Plus I2C OLED (SSD1306)**: UI 전송 지연 시간을 50% 단축하고, Core 0 백그라운드 태스크로 렌더링 분리.
* **0ms 파일 목록 스마트 캐싱**: 메뉴 진입 시 플래시(SPI) I/O 접근을 0%로 줄여 **음악 재생 중 메뉴를 조작해도 오디오 끊김(Audio Stutter) 100% 방지**.
* **사운드폰트 보호 & 고급 관리 모드 스위치**:
  - 시스템 표준 폰트인 `CT4MGM.SF2`의 최적화 유지를 위해 사운드폰트 관리 탭은 기본 숨김 처리되며, 웹/OLED 설정에서 고급 모드 활성화 시에만 노출.
* **1990s 레트로 Web GUI**: HTML 3.2 / 4.0 고증 디자인, 실시간 볼륨 슬라이더, 원클릭 오디오 테스트, PC 키보드 연주 지원 가상 피아노(Easter Egg) 내장.
* **8비트 아케이드 레트로 미니게임 5종**: 60 FPS 고주사율 렌더링 (가상 피아노, 퐁, 테트리스 블록쌓기, 벽돌깨기, 스네이크).

---

<br>

## 📐 하드웨어 핀맵 (Hardware Pinout)

**기본 보드**: ESP32-S3 DevKitC-1 / YD-ESP32-S3 (N16R8: 16MB Flash, 8MB Octal PSRAM)

```
                       +------------------------+
                       |      ESP32-S3 N16R8    |
                       +------------------------+
                               |   |   |   |
         +---------------------+   |   |   +---------------------+
         | (I2S DAC)               |   |            (RS-232 MIDI)|
         |                         |   |                         |
+------------------+               |   |               +------------------+
|  PCM5102A Module |               |   |               |  MAX3232 Module  |
|  BCLK   -> GPIO15|               |   |               |  RX     -> GPIO18|
|  LRC    -> GPIO16|               |   |               |  TX     -> GPIO17|
|  DOUT   -> GPIO7 |               |   |               |  VCC/GND         |
|  SCK/GND-> GND   |               |   |               +------------------+
+------------------+               |   |                         |
                                   |   |                [DB9 COM / SoftMPU]
         +-------------------------+   +-------------------------+
         | (I2C OLED)                               (Rotary & SW)|
         |                                                       |
+------------------+                                   +------------------+
| SSD1306 OLED     |                                   | EC11 Encoder     |
| SDA     -> GPIO8 |                                   | S1 (CLK)-> GPIO4 |
| SCL     -> GPIO9 |                                   | S2 (DT) -> GPIO5 |
| Addr    :  0x3C  |                                   | KEY(SW) -> GPIO6 |
| VCC/GND          |                                   | VCC/GND          |
+------------------+                                   +------------------+
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
| | | **VCC / GND** | `3.3V / GND` | 엔코더 모듈 전원 및 접지 |
| **상태 인디케이터**| 온보드 WS2812 | **RGB LED** | `GPIO 48` | 부팅/대기(오렌지), AP모드(블루), 재생(화이트) |
| **스피커 감지** | 외장 모듈 핀 | **MONO_DET** | `GPIO 47` | **Active LOW** (GND 접촉 시 모노 자동 전환 & 8밴드 PEQ) |

---

<br>

## 🖥️ OLED UI 및 로터리 엔코더 조작법

### 📺 1. 실시간 메인 디스플레이
```
+--------------------------------+
| AM12:34          [GS]  V:85%  | <- 상단 헤더: 실시간 시계 & 신디사이저 뱃지([GM]/[GS]/[MT-32]) & 볼륨
|--------------------------------| <- 가로 구분선
| Ch01  Acoustic Grand Piano     | <- 5초 주기 활성 채널 (Ch01~16) 자동 순환 & 악기 이름
| 1-16ch MIDI Level              | <- 16채널 레벨 안내 라벨
| █ ▄ █ ▆ ▂ █ ▄ █ ▆ ▂ █ ▄ █ ▆ ▂ █ | <- 16채널 실시간 VU 미터 바 (Roland SC-55 감성)
+--------------------------------+
```
*(※ 5분 이상 무음 및 입력이 없을 경우 자동으로 화면 보호 절전 모드로 전환됩니다.)*

### 📋 2. 온스크린 설정 메뉴
엔코더 스위치를 클릭하여 온스크린 메뉴로 진입합니다 (0ms 스마트 캐싱 탑재, 8초간 미입력 시 자동 복귀):
* **`1. MIDI 보관함 (MIDI Library)`**: Flash에 저장된 `.mid` 파일 목록 탐색, 실시간 선택/재생/정지
* **`2. 사운드폰트 선택 (SoundFont Select)`**: *(고급 관리 모드 활성화 시 표시)* `.sf2` 목록 조회 및 비동기 로드
* **`3. Wi-Fi 정보 & IP (Wi-Fi Info & IP)`**: 작동 모드(AP/STA), SSID, IP 주소, 웹 포트(80) 확인
* **`4. MIDI 전송 속도 (MIDI Baud Rate)`**: 시리얼 MIDI 통신 속도 전환 (38400 / 31250 / 115200 bps)
* **`5. 오디오 테스트 (Audio Test)`**: 피아노 C화음, 기타 아르페지오, 드럼 키트, 15초 스테레오 음향 테스트
* **`6. 소리 출력 설정 (Audio Output)`**: 스테레오(Stereo) $\leftrightarrow$ 모노 다운믹스(Mono) 전환
* **`7. LED 상태표시등 (LED Status)`**: 온보드 RGB LED 켜기/끄기 설정
* **`8. 언어 설정 (Language)`**: 한국어 (Korean) / English 지원
* **`9. 긴급 리셋 (Panic)`**: 16채널 강제 무음화 및 컨트롤러/사운드 올 리셋
* **`10. 기기 정보 (About)`**: 펌웨어 버전, 시스템 모니터링(CPU 온도, 가용 Heap, PSRAM, LittleFS) **(※ 이 화면에서 1.0초 롱프레스 시 🎮 레트로 게임실 진입!)**

### 🎮 3. 이스터에그: 8비트 아케이드 레트로 게임실 (Retro Games)
`10. 기기 정보 (About)` 화면에서 엔코더 버튼을 **1.0초간 길게 누르면** 60 FPS 고주사율 **미니게임 5종**을 즐길 수 있습니다:
1. **`1. 가상 피아노 (Virtual Piano)`**: 건반 선택 및 발음
2. **`2. 핑퐁 (Pong)`**: 1972년 아타리 오리지널 충돌 사운드 및 패들 조작
3. **`3. 블록 쌓기 (Block Stack)`**: 테트리스 코로베이니키 멀티트랙 MIDI BGM 루프 + 회전/착지 SFX
4. **`4. 벽돌깨기 (Brick Breaker)`**: 높이별 주파수 피치 파괴음 및 승리 팡파레
5. **`5. 스네이크 (Snake)`**: 90도 회전 방향 조작 및 먹이 섭취 칩튠 사운드
*(※ 플레이 중 **엔코더 버튼을 5초간 길게 누르면 게임을 즉시 종료**하고 메뉴로 탈출합니다.)*

---

<br>

## 🌐 1990년대 후반 클래식 웹 관리자 (Web GUI)

* **기본 AP 모드 접속 정보**:
  - **SSID**: `WaveCanvas-NanoRS`
  - **비밀번호**: *(비밀번호 없음 / 오픈 AP)*
  - **접속 주소**: `http://192.168.4.1` (포트 80)

### 📑 5대 웹 탭 구성
1. **MIDI 플레이어 (Player)**:
   - 파일 업로드(`.mid`), 라이브러리 관리, 재생/일시정지/정지, 실시간 마스터 볼륨 슬라이더.
   - 원클릭 오디오 테스트 (피아노 C화음, 기타 아르페지오, 드럼 키트, 15초 스테레오 공간감 테스트).
2. **사운드폰트 관리자 (SoundFonts)**:
   - *(시스템 설정에서 고급 모드 활성화 시 표시)* 플래시 용량 게이지, `.sf2` 업로드/삭제/비동기 로드.
3. **와이파이 설정 (Wi-Fi Setup)**:
   - 2.4GHz 무선 공유기 비동기 검색 및 접속.
4. **시스템 설정 (Settings)**:
   - 마스터 볼륨, MIDI 통신 속도, 오디오 모드(Stereo/Mono), LED 점등 모드, NTP 시간 동기화(UTC+9 한국), 사운드폰트 고급 모드 활성화 토글, MIDI Panic.
5. **가상 피아노 (Virtual Piano - Easter Egg)**:
   - 25건반 인터랙티브 피아노, GM 악기 프리셋 변경, 옥타브 쉬프트, PC 키보드 연주 지원.

---

<br>

## 🛠️ 빌드 및 업로드 방법 (Build & Flashing)

이 프로젝트는 [PlatformIO](https://platformio.org/) 환경에서 빌드됩니다.

### 1. 사전 준비 (Prerequisites)
* [VS Code](https://code.visualstudio.com/) 및 [PlatformIO IDE 확장](https://marketplace.visualstudio.com/items?itemName=platformio.platformio-ide) 설치
* [Python 3.x](https://www.python.org/) 설치 (MT-32 ROM 데이터 생성 스크립트 실행용)
* ESP32-S3 N16R8 보드를 USB로 PC에 연결

### 2. 프로젝트 클론 (Clone)
```bash
git clone [https://github.com/your-username/WaveCanvas.git](https://github.com/your-username/WaveCanvas.git)
cd WaveCanvas
```

### 3. 사운드폰트 및 MT-32 ROM 준비 (`data/` 폴더)
저작권 보호 및 배포 라이선스 준수를 위해 음원 바이너리 파일은 저장소에 포함되어 있지 않습니다. 빌드 전 `data/` 디렉토리에 필수 파일을 직접 준비해 배치합니다:
```
WaveCanvas/
└── data/
    ├── CT4MGM.SF2        <-- 기본 필수 사운드폰트 (Creative / E-mu SoundFont 2.0 규격)
    └── MT32_PCM.ROM      <-- Roland MT-32 512KB PCM 사운드 롬
```
파일 배치 후, 펌웨어 빌드에 필요한 MT-32 PCM 데이터 코드를 생성합니다:
```bash
python3 generate_mt32_pcm.py
```
*(※ 스크립트 실행을 통해 src/mt32_pcm_data.cpp가 생성되어야 펌웨어 컴파일이 정상 진행됩니다.)*

### 4. LittleFS 파일시스템 업로드
사운드폰트를 ESP32-S3의 Flash 파티션으로 전송합니다:
```bash
# PlatformIO CLI
pio run --target uploadfs
```

### 5. 펌웨어 빌드 및 업로드
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
├── include/
│   ├── config.h               # 하드웨어 핀맵, 32보이스, 512 오디오 버퍼, 기본 설정
│   ├── audio_engine.h         # 오디오 엔진 & 32-bit Float DSP 파이프라인 인터페이스
│   ├── tsf.h                  # TinySoundFont 코어 (PSRAM 적재, Biquad 복원, 보이스 스틸링)
│   ├── la32_synth.h           # Roland LA-32 실시간 소프트웨어 합성 엔진 (MT-32 커스텀 음색)
│   ├── chorus_fx.h            # Roland GS 8종 스테레오 코러스 엔진
│   ├── reverb_fx.h            # Freeverb 3D 스테레오 스튜디오 리버브 & 8종 GS/MT-32 매크로
│   ├── master_eq.h            # Roland GS 2-Band 파라메트릭 마스터 EQ
│   ├── speaker_eq.h           # 8-Band 어쿠스틱 PEQ (외장 도킹 스피커 음향 보정 & 다운믹스)
│   ├── midi_parser.h          # UART2 시리얼 MIDI 파서 & MT-32/GS/GM 3대 규격 엔진
│   ├── midi_sequencer.h       # SMF Type 0/1 시퀀서 & 고속 배치 락 디스패처
│   ├── display_ui.h           # SSD1306 OLED UI & 16ch VU 미터 렌더러
│   ├── game_engine.h          # 60 FPS 8비트 아케이드 레트로 미니게임 5종 엔진
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
│   ├── la32_synth.cpp         # LA-32 부분음 합성, TVF 필터 및 링 모듈레이션 구현
│   ├── midi_parser.cpp        # MIDI 수신, GS 리셋, MT-32 LCD 토스트, SysEx 디코딩
│   ├── midi_sequencer.cpp     # FreeRTOS Core 0 시퀀서 태스크 (잔향 보존 & 페이드아웃)
│   ├── display_ui.cpp         # 800kHz Fast I2C UI 루프 & 파일 목록 스마트 캐싱
│   ├── game_engine.cpp        # 아케이드 게임 5종 로직 (테트리스, 벽돌깨기 등)
│   ├── encoder_input.cpp      # 로터리 엔코더 이벤트 처리
│   ├── led_indicator.cpp      # RGB LED 색상 제어
│   ├── wifi_manager.cpp       # Wi-Fi 이벤트 핸들러
│   ├── web_manager.cpp        # HTTP 웹서버 라우팅, 가상 피아노, 파일 매니저
│   ├── time_manager.cpp       # 시간 포맷팅 및 NTP 동기화
│   └── mt32_pcm_data.cpp      # (※ build artifact: generate_mt32_pcm.py로 자동 생성)
├── data/                      # LittleFS Flash 파일시스템 루트
│   ├── CT4MGM.SF2             # (사용자 준비) SoundFont2 사운드폰트
│   ├── MT32_PCM.ROM           # (사용자 준비) Roland MT-32 512KB PCM 롬
│   └── logo.png               # Web GUI 로고 이미지 리소스
├── icon/                      # Web UI 소스 아이콘 리소스 (.png)
├── generate_mt32_pcm.py       # MT-32 ROM -> C++ 소스코드 자동 디코더/생성기
├── partitions_16MB.csv        # 16MB Flash 파티션 테이블 (LittleFS / App / OTA)
├── platformio.ini             # PlatformIO 빌드 설정 (-O3, -ffast-math, ESP32-S3)
└── README.md                  # 프로젝트 문서
```

---

<br>

## 📜 라이선스 및 크레딧 (License & Credits)

* **WaveCanvas Core & Firmware**: [MIT License](LICENSE) © 2026 NSteven.
  - *Custom Synth & Audio Pipeline*: Roland LA-32 소프트웨어 합성 엔진, SC-55 하이브리드 Bank 127 라우터, 32-bit Float 무손실 DSP 체인(3D 리버브, 코러스, 8-Band 스피커 PEQ, 단조 유리수 마스터 리미터), 8비트 칩튠 신스 독자 설계.
* **SF2 Parsing Core**: [TinySoundFont](https://github.com/schellingb/TinySoundFont) by **Bernhard Schelling** (MIT License)
  - *Modifications*: ESP32-S3 8MB Octal PSRAM 스트리밍 로더 이식, Biquad 필터 복원, 32-Voice Polyphony 확장.
* **Synthesis & Voice Allocation References**:
  - **[Munt (MT-32 Emulator)](https://github.com/munt/munt)** by **Sergey V. Mikayev & Munt Team** (LGPL): Roland LA-32 소프트웨어 합성 구조, MT-32 벨로시티 매핑 LUT 및 파라미터 아키텍처 참조.
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
* **Nexisson Tech Co., Ltd.** 및 `Nexisson`, `WaveCanvas`의 명칭과 로고는 1990년대 후반 고전 음향기기 전성기 시절의 분위기를 내기 위해 창작된 **100% 가상의 브랜드(Fictional Entity)**입니다.
* 현실의 특정 실존 기업이나 단체와 무관하며, 프로젝트에 포함된 **로고, 아이콘, 텍스트, 디자인 등 모든 브랜드 에셋은 누구나 제약 없이 자유롭게 수정, 사용, 배포**하실 수 있습니다.

<br>

### 🎵 음원 저작권 및 출처 고지 (SoundFont & ROM Attribution)
* 본 펌웨어의 기본/권장 사운드폰트로 명시된 **`CT4MGM.SF2`**는 **Creative Technology Ltd. / E-mu Systems SoundFont 2.0 포맷**을 기반으로 고전 사운드 모듈 재생을 위해 커뮤니티에서 널리 활용되는 뱅크입니다.
* 저작권 및 배포 라이선스 준수를 위해 **사운드폰트(`.SF2`) 및 MT-32 PCM ROM 바이너리는 본 GitHub 저장소에 포함되어 있지 않으며**, 사용자가 직접 준비하여 기기에 업로드하거나 변환 스크립트를 실행해야 합니다.
* 본 프로젝트는 **구형 DOS/레트로 PC 환경에서 시리얼 MIDI 연주 및 고전 게임 사운드, 미디파일을 즐기기 위한 비상업적 개인 취미 목적**으로 제작되었으며, 사운드폰트 및 MT-32 ROM 내 샘플/음색 데이터의 모든 저작권과 지식재산권은 해당 원저작권자(Creative / E-mu / Roland Corp.)에게 귀속됩니다.

<br>

<div align="center">

**WaveCanvas**로 고전 게임과 미디 명곡들의 감동적인 사운드를 생생하게 경험해 보세요! 🎹✨

</div>
