

# CamProject — Raspberry Pi IMX219 Raw Capture

Capturing real images via **V4L2 unicam raw capture + software demosaicing**.

Because of a libcamera IPA bug on Ubuntu 24.04, the ISP pipeline is unusable — so we go through the V4L2 raw path, pull Bayer data directly, and perform the ISP role in user space.

## 1. Hardware Structure
```
IMX219 sensor ──MIPI CSI-2 lanes──▶ Unicam (BCM2835 CSI receiver) ──DMA──▶ Memory
```

| Component      | Role                                                         |
| -------------- | ------------------------------------------------------------ |
| **IMX219**     | 8MP CMOS image sensor. Outputs 10-bit Bayer raw              |
| **MIPI CSI-2** | High-speed serial interface between sensor and SoC (2-lane)  |
| **Unicam**     | BCM2835's CSI receiver. Pushes raw data into memory via DMA  |


## 2. Software Structure

### 2.1 Device Nodes

| Node                 | Purpose                                              |
| -------------------- | ---------------------------------------------------- |
| `/dev/video0`        | Unicam V4L2 capture device (raw Bayer data)          |
| `/dev/v4l-subdev0`   | IMX219 sensor sub-device (exposure/gain sensor ctrl) |


### 2.2 Headers / Syscalls Used

| Header                | What it provides                                             |
| --------------------- | ------------------------------------------------------------ |
| `<unistd.h>`          | `open()`, `close()`, `read()`, `write()` — generic file I/O  |
| `<sys/ioctl.h>`       | `ioctl()` — channel for issuing special commands to a device |
| `<sys/mman.h>`        | `mmap()` — maps DMA buffers into user space                  |
| `<linux/videodev2.h>` | V4L2 command codes + structs (`VIDIOC_*`, `v4l2_*`)          |

## 3. Capture Flow (Step by Step)

### Step 1: Open Unicam

```cpp
const char* unicam_dev  = "/dev/video0";
int raw_fd = ::open(unicam_dev, O_RDWR);
FileDescriptor fd_unicam(raw_fd);
```
- Unicam is opened via a file descriptor so we can talk to the raw driver directly.

### Step 2: Open V4L2 Sub-device + Configure Sensor
```cpp
int raw_sfd = ::open("/dev/v4l-subdev0", O_RDWR);
FileDescriptor fd_sensor(raw_sfd);
struct v4l2_format fmt{};
if (raw_sfd >= 0) {
	setSensorCtrl(fd_sensor.get(), V4L2_CID_EXPOSURE,      1500, "exposure");
	setSensorCtrl(fd_sensor.get(), V4L2_CID_ANALOGUE_GAIN, 200,  "analogue_gain");
	setSensorCtrl(fd_sensor.get(), V4L2_CID_VFLIP,         0,    "vflip");
	validSensorCtrl(fd_sensor.get(), V4L2_CID_EXPOSURE,      1500, "exposure");
	validSensorCtrl(fd_sensor.get(), V4L2_CID_ANALOGUE_GAIN, 200, "analogue_gain");
	validSensorCtrl(fd_sensor.get(), V4L2_CID_VFLIP,         0,    "vflip");
}
else {
	printf("[WARN] /dev/v4l-subdev0 open failed, skipping sensor ctrl\n");
}
```
- Sensor parameters are set up ahead of actual image capture.

### Step 3: Allocate DMA Buffers + mmap

```cpp
// Request 4 buffers from the kernel
unsigned stride = 0, frame_size = 0;
if (!setCaptureFormat(fd_unicam.get(), WIDTH, HEIGHT,
                      V4L2_PIX_FMT_SBGGR10P, &stride, &frame_size))
```
- Configure Unicam's output pixel format and frame dimensions.
> stride: size including buffer padding
> frame_size: actual size needed to store the image

### Step 4: Start Streaming + Warm-up
```cpp
ioctl(fd, VIDIOC_STREAMON, &type);    // start capture

// Warm-up: throw away 30 frames (let sensor AE settle)
for (int i = 0; i < 30; i++) {
    ioctl(fd, VIDIOC_DQBUF, &buf);    // pull a frame
    ioctl(fd, VIDIOC_QBUF, &buf);     // hand it right back
}
```

> The sensor's internal auto-exposure needs a few dozen frames to stabilize.
> DQBUF (dequeue) ↔ QBUF (queue) forms a ping-pong structure.


### Step 5: Capture a Frame

```cpp
ioctl(fd, VIDIOC_DQBUF, &buf);
// → buffers[buf.index].start holds the raw Bayer data (2,562,560 bytes)
```

### Step 6: Save to File

```
RGB data → PPM (uncompressed image, 3-line header + raw RGB)
         → convert to PNG with ImageMagick's `convert`
```


## 6. Relevant Files

| File | Description |
|------|-------------|
| [src/unicam_capture.cpp](src/unicam_capture.cpp) | Unicam raw Bayer capture entry point — open / format / mmap / streaming / software ISP |
| [src/v4l2_utils.cpp](src/v4l2_utils.cpp) | V4L2 utilities (sensor control, format setup, buffer request/queue wrappers) |
| [include/v4l2_utils.h](include/v4l2_utils.h) | V4L2 utility header + `FileDescriptor` RAII wrapper |
| `output.raw` | Captured raw Bayer dump (10-bit packed SBGGR10P) |

---

## 7. Build & Run

```bash
g++ -std=c++17 -Iinclude src/unicam_capture.cpp src/v4l2_utils.cpp -o unicam_capture
./unicam_capture
# → output.raw (raw Bayer) + output.ppm (demosaiced result)
convert output.ppm output.png
```

### Prerequisites
- Raspberry Pi with an IMX219 camera module attached
- `dtoverlay=imx219` set in `/boot/firmware/config.txt`
- `/dev/video0` and `/dev/v4l-subdev0` nodes present
- Your user must belong to the `video` group (`sudo usermod -aG video $USER`)

