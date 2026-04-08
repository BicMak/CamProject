/**
 * unicam_capture.cpp
 *
 * unicam → raw Bayer SBGGR10P → 파일 덤프
 *
 * 파이프라인:
 *   /dev/video0  (unicam, CAPTURE)  raw Bayer SBGGR10P
 *       ↓  mmap
 *   .raw 바이너리 파일로 저장
 *
 * 사용법:
 *   ./unicam_capture [output.raw]
 */

#include "v4l2_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <vector>

#include <fcntl.h>

int main(int argc, char* argv[])
{
    const char* unicam_dev  = "/dev/video0";
    const char* output_file = "output.raw";

    if (argc > 1) unicam_dev  = argv[1];
    if (argc > 2) output_file = argv[2];

    const unsigned WIDTH  = 1640;
    const unsigned HEIGHT = 1232;
    const unsigned NBUF   = 4;

    printf("=== unicam raw capture ===\n\n");

    // ── 1) 디바이스 열기 ────────────────────────────────────
    int raw_fd = ::open(unicam_dev, O_RDWR);
    if (raw_fd < 0) { perror("open unicam"); return 1; }
    FileDescriptor fd_unicam(raw_fd);
    printf("[OK] %s opened\n", unicam_dev);

    // ── 2) 센서 설정 ─────────────────────────────────────────
    {
        int raw_sfd = ::open("/dev/v4l-subdev0", O_RDWR);
        FileDescriptor fd_sensor(raw_sfd);
        if (raw_sfd >= 0) {
            setSensorCtrl(fd_sensor.get(), V4L2_CID_EXPOSURE,      1500, "exposure");
            setSensorCtrl(fd_sensor.get(), V4L2_CID_ANALOGUE_GAIN, 200,  "analogue_gain");
            setSensorCtrl(fd_sensor.get(), V4L2_CID_VFLIP,         0,    "vflip");
        } else {
            printf("[WARN] /dev/v4l-subdev0 open failed, skipping sensor ctrl\n");
        }
        validSensorCtrl(fd_sensor.get(), V4L2_CID_EXPOSURE,      1500, "exposure");
        validSensorCtrl(fd_sensor.get(), V4L2_CID_ANALOGUE_GAIN, 200,  "analogue_gain");
        validSensorCtrl(fd_sensor.get(), V4L2_CID_VFLIP,         0,    "vflip");
    }

    // ── 3) 포맷 설정 ────────────────────────────────────────
    unsigned stride = 0, frame_size = 0;
    if (!setCaptureFormat(fd_unicam.get(), WIDTH, HEIGHT,
                          V4L2_PIX_FMT_SBGGR10P, &stride, &frame_size))
        return 1;


    // ── 4) 버퍼 할당 ──────────────────────────────────────
    auto bufs = allocBuffers(fd_unicam.get(), NBUF);
    if (bufs.empty()) return 1;

    // ── 5) 스트리밍 시작 ──────────────────────────────────
    enum v4l2_buf_type cap_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd_unicam.get(), VIDIOC_STREAMON, &cap_type) < 0) {
        perror("VIDIOC_STREAMON"); return 1;
    }
    printf("[OK] streaming started\n");

    // ── 6) 워밍업 (AE 안정화) ─────────────────────────────
    printf("  warming up (5 frames)...\n");
    for (int i = 0; i < 5; ++i) {
        auto buf = grabFrame(fd_unicam.get());
        releaseFrame(fd_unicam.get(), buf);
    }
    printf("  warmup done\n\n");

    // ── 7) 프레임 캡처 ────────────────────────────────────
    printf("  capturing 1 frame...\n");
    auto buf = grabFrame(fd_unicam.get());
    printf("  [UNICAM] index=%u bytesused=%u\n", buf.index, buf.bytesused);

    // ── 8) raw 파일 저장 ──────────────────────────────────
    {
        auto* data = static_cast<uint8_t*>(bufs[buf.index].start);
        unsigned save_size = buf.bytesused ? buf.bytesused : frame_size;

        FILE* fp = fopen(output_file, "wb");
        if (!fp) { perror("fopen output"); return 1; }

        // 헤더: width / height / stride / pixfmt (간단한 메타데이터)
        uint32_t hdr[4] = { WIDTH, HEIGHT, stride, V4L2_PIX_FMT_SBGGR10P };
        fwrite(hdr, sizeof(hdr), 1, fp);

        fwrite(data, 1, save_size, fp);
        fclose(fp);

        printf("  [SAVE] %s (%u bytes, %ux%u stride=%u)\n",
               output_file, save_size, WIDTH, HEIGHT, stride);
    }

    releaseFrame(fd_unicam.get(), buf);

    // ── 9) 정리 ──────────────────────────────────────────
    stopAndCleanup(fd_unicam.get(), bufs, V4L2_BUF_TYPE_VIDEO_CAPTURE);

    printf("\n=== Done. ===\n");
    return 0;
}