# MIPI CSI 카메라 캡처 파이프라인 (V4L2 + 소프트웨어 ISP)

라즈베리파이 5 + IMX219 센서 환경에서, Ubuntu libcamera IPA 버그를 우회하여
**V4L2 unicam raw 캡처 + 소프트웨어 디모자이킹**으로 실제 이미지를 얻는 과정.

---

## 1. 하드웨어 구조

```
IMX219 센서 ──MIPI CSI-2 레인──▶ Unicam (BCM2835 CSI 수신기) ──DMA──▶ 메모리
```

| 구성 요소 | 역할 |
|-----------|------|
| **IMX219** | 8MP CMOS 이미지 센서. 10-bit Bayer raw 출력 |
| **MIPI CSI-2** | 센서 ↔ SoC 사이 고속 직렬 인터페이스 (2-lane) |
| **Unicam** | BCM2835의 CSI 수신기. raw 데이터를 DMA로 메모리에 적재 |

---

## 2. 소프트웨어 구조

### 2.1 디바이스 노드

| 노드 | 용도 |
|------|------|
| `/dev/video0` | Unicam V4L2 캡처 디바이스 (raw Bayer 데이터) |
| `/dev/v4l-subdev0` | IMX219 센서 서브디바이스 (노출/게인 등 센서 제어) |
| `/dev/media4` | Media Controller (파이프라인 토폴로지) |

### 2.2 사용하는 헤더/시스템콜

| 헤더 | 제공하는 것 |
|------|------------|
| `<unistd.h>` | `open()`, `close()`, `read()`, `write()` — 범용 파일 I/O |
| `<sys/ioctl.h>` | `ioctl()` — 디바이스에 특수 명령을 내리는 채널 |
| `<sys/mman.h>` | `mmap()` — DMA 버퍼를 유저스페이스에 매핑 |
| `<linux/videodev2.h>` | V4L2 명령 코드 + 구조체 (`VIDIOC_*`, `v4l2_*`) |

---

## 3. 캡처 흐름 (단계별)

### Step 1: 센서 제어 — 노출/게인 설정

```cpp
int sfd = open("/dev/v4l-subdev0", O_RDWR);

// 노출 설정 (4~1759, default=1600)
struct v4l2_control ctrl{};
ctrl.id = V4L2_CID_EXPOSURE;
ctrl.value = 1600;
ioctl(sfd, VIDIOC_S_CTRL, &ctrl);

// 아날로그 게인 (0~232)
ctrl.id = V4L2_CID_ANALOGUE_GAIN;
ctrl.value = 100;
ioctl(sfd, VIDIOC_S_CTRL, &ctrl);
```

> unicam은 raw 드라이버라 ISP 자동노출(AE)이 없음.
> 센서의 I2C 레지스터를 직접 설정해야 밝기가 적절해짐.

---

### Step 2: V4L2 디바이스 열기 + 포맷 설정

```cpp
int fd = open("/dev/video0", O_RDWR);

struct v4l2_format fmt{};
fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
fmt.fmt.pix.width       = 1640;
fmt.fmt.pix.height      = 1232;
fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SBGGR10P;  // 10-bit Bayer packed

ioctl(fd, VIDIOC_S_FMT, &fmt);
```

> 센서 native 해상도(1640x1232)에서 10-bit Bayer packed 포맷으로 수신.
> `SBGGR` = Bayer 패턴 시작이 Blue-Green / Green-Red 순서.

---

### Step 3: DMA 버퍼 할당 + mmap 매핑

```cpp
// 커널에 버퍼 4개 요청
struct v4l2_requestbuffers reqbuf{};
reqbuf.count  = 4;
reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
reqbuf.memory = V4L2_MEMORY_MMAP;
ioctl(fd, VIDIOC_REQBUFS, &reqbuf);

// 각 버퍼를 유저스페이스에 매핑
for (each buffer) {
    ioctl(fd, VIDIOC_QUERYBUF, &buf);      // 버퍼 정보 조회
    mmap(buf.length, fd, buf.m.offset);     // 유저스페이스에 매핑
    ioctl(fd, VIDIOC_QBUF, &buf);          // 큐에 넣기
}
```

> `mmap` 방식: 커널 DMA 버퍼를 유저스페이스에서 직접 접근.
> 데이터 복사 없이 제로카피(zero-copy)로 프레임 접근 가능.

---

### Step 4: 스트리밍 시작 + 워밍업

```cpp
ioctl(fd, VIDIOC_STREAMON, &type);    // 캡처 시작

// 워밍업: 30프레임 버림 (센서 AE 안정화)
for (int i = 0; i < 30; i++) {
    ioctl(fd, VIDIOC_DQBUF, &buf);    // 프레임 꺼내기
    ioctl(fd, VIDIOC_QBUF, &buf);     // 바로 반납
}
```

> 센서 내부 자동노출이 안정화되려면 수십 프레임이 필요.
> DQBUF(dequeue) ↔ QBUF(queue) 핑퐁 구조.

---

### Step 5: 프레임 캡처

```cpp
ioctl(fd, VIDIOC_DQBUF, &buf);
// → buffers[buf.index].start 에 raw Bayer 데이터 (2,562,560 bytes)
```

---

### Step 6: 소프트웨어 이미지 처리 (ISP 대체)

보통 라즈베리파이 ISP가 하드웨어로 처리하는 과정을 소프트웨어로 직접 수행:

#### (a) 10-bit packed 언패킹

```
메모리 레이아웃 (5 bytes = 4 pixels):
  byte0 = P0[9:2]
  byte1 = P1[9:2]
  byte2 = P2[9:2]
  byte3 = P3[9:2]
  byte4 = P3[1:0] | P2[1:0] | P1[1:0] | P0[1:0]

→ 각 픽셀을 16-bit 값으로 복원 (0~1023)
```

#### (b) Bayer 디모자이킹 (Bilinear 보간)

```
센서 원본 (각 픽셀은 R/G/B 중 하나만 가짐):
  B G B G B G ...
  G R G R G R ...
  B G B G B G ...

디모자이킹 후 (각 픽셀이 R, G, B 모두 가짐):
  - B 위치: B는 그대로, G는 상하좌우 평균, R은 대각선 평균
  - R 위치: R은 그대로, G는 상하좌우 평균, B는 대각선 평균
  - G 위치: G는 그대로, 나머지는 인접 픽셀에서 보간
```

#### (c) 10-bit → 8-bit 변환

```
pixel_8bit = pixel_10bit >> 2    (1023 → 255)
```

#### (d) 히스토그램 스트레칭 (밝기 보정)

```
실제 픽셀 범위: 0 ~ 47  (어두움)
     ↓ 스트레칭
보정 후 범위:   0 ~ 255  (전체 활용)

공식: new = (old - min) * 255 / (max - min)
```

#### (e) 화이트밸런스 — Gray World 알고리즘

```
원리: "세상의 평균 색은 회색이다"

1. R/G/B 채널 평균 계산
   avg_R=18.5, avg_G=20.3, avg_B=17.0

2. 전체 평균 = (18.5 + 20.3 + 17.0) / 3 = 18.6

3. 각 채널 게인 계산
   gain_R = 18.6 / 18.5 = 1.00
   gain_G = 18.6 / 20.3 = 0.92  ← 초록 줄임
   gain_B = 18.6 / 17.0 = 1.09  ← 파랑 올림

4. 모든 픽셀에 게인 적용 → 초록빛 편향 제거
```

---

### Step 7: 파일 저장

```
RGB 데이터 → PPM (비압축 이미지, 헤더 3줄 + raw RGB)
          → ImageMagick `convert` 로 PNG 변환
```

---

## 4. 전체 파이프라인 다이어그램

```
                        하드웨어
┌──────────┐    MIPI    ┌──────────┐    DMA    ┌──────────┐
│  IMX219  │──────────▶│  Unicam  │─────────▶│  메모리   │
│  센서    │  CSI-2     │ CSI 수신  │          │ DMA 버퍼  │
└──────────┘            └──────────┘          └─────┬────┘
     ▲                                              │
     │ I2C (노출/게인)                               │ mmap
     │                                              ▼
┌────┴─────────────────────────────────────────────────────┐
│                    유저스페이스 프로그램                     │
│                                                          │
│  /dev/v4l-subdev0 ──▶ 센서 노출=1600, 게인=100 설정       │
│  /dev/video0      ──▶ V4L2 캡처 (ioctl + mmap)          │
│                                                          │
│  ┌─────────────────────────────────────────────────┐     │
│  │          소프트웨어 ISP (원래 HW가 하는 일)        │     │
│  │                                                 │     │
│  │  Raw Bayer 10-bit packed                        │     │
│  │    ↓ 언패킹                                     │     │
│  │  Bayer 16-bit                                   │     │
│  │    ↓ 디모자이킹 (bilinear)                       │     │
│  │  RGB (어두움)                                    │     │
│  │    ↓ 히스토그램 스트레칭                           │     │
│  │  RGB (밝기 보정)                                 │     │
│  │    ↓ Gray World AWB                             │     │
│  │  RGB (색 보정)                                   │     │
│  └─────────────────────────────────────────────────┘     │
│                         ↓                                │
│                    PPM → PNG 저장                         │
└──────────────────────────────────────────────────────────┘
```

---

## 5. 일반적인 경우 (libcamera 정상 작동 시)

```
IMX219 → Unicam → ISP (HW가 디모자이킹/AWB/AE 전부 처리) → RGB 출력
                   ↑
              libcamera가 ISP 파이프라인 제어
```

우리가 소프트웨어로 한 (a)~(e) 과정을 **라즈베리파이 ISP가 하드웨어로** 처리.
Ubuntu 24.04의 libcamera 0.2.0 IPA 버그로 인해 ISP 사용 불가 → V4L2 raw 우회.

---

## 6. 관련 파일

| 파일 | 설명 |
|------|------|
| `src/bayer_capture.cpp` | Bayer raw 캡처 + 소프트웨어 디모자이킹 + AWB |
| `src/capture_png.cpp` | libcamera API 캡처 (현재 IPA 버그로 사용 불가) |
| `src/v4l2_open_example.cpp` | V4L2 기본 사용법 예제 (open/ioctl/mmap) |
| `build.sh` | 빌드 + 실행 자동화 스크립트 |
