#include "v4l2_utils.h"
#include <cstdio>
#include <fcntl.h>

/**
 * @brief V4L2 디바이스의 센서 컨트롤 값을 설정한다.
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param id    설정할 컨트롤 ID (V4L2_CID_* 상수)
 * @param value 설정할 값
 * @param name  로그 출력에 사용할 컨트롤 이름 문자열
 * @return     성공 시 true, ioctl 실패 시 false
 */
bool setSensorCtrl(int fd, int id, int value, const char* name) {
    v4l2_control ctrl{};
    ctrl.id = id;
    ctrl.value = value;
    if (::ioctl(fd, VIDIOC_S_CTRL, &ctrl) < 0) {
        perror(name);
        return false;
    }
    printf("  [SET] %s=%d\n", name, ctrl.value);
    return true;
}

/**
 * @brief V4L2 디바이스의 센서 컨트롤 현재 값을 읽어온다.
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param id    읽을 컨트롤 ID (V4L2_CID_* 상수)
 * @param name  로그 출력에 사용할 컨트롤 이름 문자열
 * @return      성공 시 컨트롤 현재 값, ioctl 실패 시 -1
 */
int getSensorCtrl(int fd, int id, const char* name) {
    v4l2_control ctrl{};
    ctrl.id = id;
    if (::ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        perror(name);
        return -1;
    }
    printf("  [GET] %s=%d\n", name, ctrl.value);
    return ctrl.value;
}

/**
 * @brief V4L2 디바이스 unicam의 이미지 설정을 함
 *
 * @param fd             V4L2 디바이스 파일 디스크립터
 * @param w              설정할 이미지 너비
 * @param h              설정할 이미지 높이
 * @param out_stride     한 줄(Row)을 저장하는 데 사용되는 실제 메모리 폭(바이트 단위)
 * @param out_sizeimage  이미지 전체 한 장을 저장하기 위해 필요한 총 메모리 용량(Total Frame Size = Stride * Height[byte]) 
 * @return               성공 시 true, ioctl 실패 시 false
 */
bool setCaptureFormat(int fd, unsigned w, unsigned h,
                      uint32_t pixfmt,
                      unsigned* out_stride, unsigned* out_sizeimage)
{
    v4l2_format fmt{};
    fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width       = w;
    fmt.fmt.pix.height      = h;
    fmt.fmt.pix.pixelformat = pixfmt;
    fmt.fmt.pix.field       = V4L2_FIELD_NONE;

    if (::ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("VIDIOC_S_FMT");
        return false;
    }
    // 드라이버가 실제 적용한 값 읽기
    if (::ioctl(fd, VIDIOC_G_FMT, &fmt) < 0) {
        perror("VIDIOC_G_FMT");
        return false;
    }

    printf("  [UNICAM] %ux%u fmt=%.4s stride=%u sizeimage=%u\n",
           fmt.fmt.pix.width, fmt.fmt.pix.height,
           reinterpret_cast<char*>(&fmt.fmt.pix.pixelformat),
           fmt.fmt.pix.bytesperline,
           fmt.fmt.pix.sizeimage);

    if (out_stride)    *out_stride    = fmt.fmt.pix.bytesperline;
    if (out_sizeimage) *out_sizeimage = fmt.fmt.pix.sizeimage;

    return true;
}



/**
 * @brief V4L2 디바이스 DMA 버퍼 생성
 *
 * @param fd             V4L2 디바이스 파일 디스크립터
 * @param count          버퍼 사이즈
 * @param type           버퍼 타입 (V4L2_BUF_TYPE_VIDEO_CAPTURE 등)
 * @return               성공 시 true, ioctl 실패 시 false
 */
 std::vector<MmapBuffer> allocBuffers(int fd, unsigned count,
                                     v4l2_buf_type type){
    v4l2_requestbuffers reqbuf{};
    reqbuf.count  = count;
    reqbuf.type   = type;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    if (::ioctl(fd, VIDIOC_REQBUFS, &reqbuf) < 0) {
        perror("VIDIOC_REQBUFS");
        return {};
    }
    printf("  [BUF] allocated %u buffers\n", reqbuf.count);

    std::vector<MmapBuffer> buffers(reqbuf.count);

    for (unsigned i = 0; i < reqbuf.count; ++i) {
        v4l2_buffer buf{};
        buf.type   = type;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index  = i;

        if (::ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("VIDIOC_QUERYBUF");
            return {};
        }

        buffers[i].length = buf.length;
        buffers[i].start  = ::mmap(nullptr, buf.length,
                                   PROT_READ | PROT_WRITE, MAP_SHARED,
                                   fd, buf.m.offset);
        if (buffers[i].start == MAP_FAILED) {
            perror("mmap");
            return {};
        }

        if (::ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            perror("VIDIOC_QBUF");
            return {};
        }
    }

    return buffers;
}

/**
 * @brief 본격 캡쳐이전에 워밍업
 *
 * @param fd             V4L2 디바이스 파일 디스크립터
 * @param count          웜업시 캡쳐 횟수
 * @return               성공 시 true, ioctl 실패 시 false
 */
 bool startAndWarmup(int fd, unsigned count) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }
    printf("\n  [STREAM] started\n");

    printf("  warming up (%u frames)...\n", count);
    for (unsigned i = 0; i < count; ++i) {
        v4l2_buffer wbuf{};
        wbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        wbuf.memory = V4L2_MEMORY_MMAP;

        ::ioctl(fd, VIDIOC_DQBUF, &wbuf);
        ::ioctl(fd, VIDIOC_QBUF, &wbuf);
    }
    printf("  warmup done\n");
    return true;
}

/**
 * @brief 캡처 완료된 프레임 버퍼를 큐에서 꺼낸다(dequeue).
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @return      dequeue된 v4l2_buffer. VIDIOC_DQBUF 실패 시 type/memory 필드만 설정된 빈 버퍼 반환
 */
 v4l2_buffer grabFrame(int fd) {
    v4l2_buffer buf{};
    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (::ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        perror("VIDIOC_DQBUF");
    }
    return buf;
}

/**
 * @brief DMA버퍼로 데이터 가져오기
 *
 * @param fd             V4L2 디바이스 파일 디스크립터
 * @param index          버퍼에서 가져올 인덱스
 * @return buf           
 */
 int exportDmabufFd(int fd, unsigned index){
    v4l2_exportbuffer expbuf{};
    expbuf.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    expbuf.index = index;
    expbuf.flags = O_RDONLY;

    if (::ioctl(fd, VIDIOC_EXPBUF, &expbuf) < 0) {
        perror("VIDIOC_EXPBUF");
        return -1;
    }
    return expbuf.fd;
}


/**
 * @brief 처리 완료된 버퍼를 V4L2 큐에 반환한다(enqueue).
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param buf   반환할 v4l2_buffer (grabFrame으로 얻은 버퍼)
 */
void releaseFrame(int fd, v4l2_buffer& buf) {
    if (::ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
    }
}

/**
 * @brief 스트리밍을 중지하고 mmap 버퍼를 해제한다.
 *
 * @param fd      V4L2 디바이스 파일 디스크립터
 * @param buffers 해제할 mmap 버퍼 목록 (호출 후 비워짐)
 * @param type    버퍼 타입 (V4L2_BUF_TYPE_VIDEO_CAPTURE 등)
 */
void stopAndCleanup(int fd, std::vector<MmapBuffer>& buffers,
                    v4l2_buf_type type){
    ::ioctl(fd, VIDIOC_STREAMOFF, &type);
    printf("  [STREAM] stopped\n");

    for (auto& b : buffers) {
        if (b.start && b.start != MAP_FAILED)
            ::munmap(b.start, b.length);
    }
    buffers.clear();
}

