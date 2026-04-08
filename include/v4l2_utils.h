#pragma once

#include <cstdint>
#include <vector>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>

// ── RAII fd wrapper ─────────────────────────────────────────
class FileDescriptor {
public:
    explicit FileDescriptor(int fd) noexcept : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }

    FileDescriptor(FileDescriptor&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int  get()   const noexcept { return fd_; }
    bool valid() const noexcept { return fd_ >= 0; }

private:
    int fd_;
};

// ── mmap 버퍼 정보 ──────────────────────────────────────────
struct MmapBuffer {
    void*  start  = nullptr;
    size_t length = 0;
};

/**
 * @brief V4L2 디바이스의 센서 컨트롤 값을 설정한다.
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param id    설정할 컨트롤 ID (V4L2_CID_* 상수)
 * @param value 설정할 값
 * @param name  로그 출력에 사용할 컨트롤 이름 문자열
 * @return      성공 시 true, ioctl 실패 시 false
 */
bool setSensorCtrl(int fd, int id, int value, const char* name);

/**
 * @brief V4L2 디바이스의 센서 컨트롤값과 기대값을 비교하여 검증한다.
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param id    읽을 컨트롤 ID (V4L2_CID_* 상수)
 * @param value 기대하는 값 (실제 읽은 값과 비교)
 * @param name  로그 출력에 사용할 컨트롤 이름 문자열
 * @return      일치 시 컨트롤 현재 값, 불일치 또는 ioctl 실패 시 -1
 */
int  validSensorCtrl(int fd, int id, int value, const char* name);

/**
 * @brief V4L2 디바이스의 센서 컨트롤 현재 값을 읽어온다.
 *
 * @param fd           V4L2 디바이스 파일 디스크립터
 * @param id           읽을 컨트롤 ID (V4L2_CID_* 상수)
 * @param target_name  로그 출력에 사용할 컨트롤 이름 문자열
 * @return             성공 시 컨트롤 현재 값, ioctl 실패 시 -1
 */
int  getSensorCtrl(int fd, int id, const char* target_name);

/**
 * @brief V4L2 디바이스 unicam의 이미지 포맷을 설정한다.
 *
 * @param fd             V4L2 디바이스 파일 디스크립터
 * @param w              설정할 이미지 너비
 * @param h              설정할 이미지 높이
 * @param pixfmt         픽셀 포맷 (V4L2_PIX_FMT_* 상수)
 * @param out_stride     한 줄(Row)을 저장하는 데 사용되는 실제 메모리 폭(바이트 단위), NULL 가능
 * @param out_sizeimage  한 프레임 전체 크기 (Stride * Height, 바이트 단위), NULL 가능
 * @return               성공 시 true, ioctl 실패 시 false
 */
bool setCaptureFormat(int video_fd, unsigned w, unsigned h,
                      uint32_t pixfmt,
                      unsigned* out_stride = nullptr,
                      unsigned* out_sizeimage = nullptr);

/**
 * @brief V4L2 디바이스의 DMA 버퍼를 할당하고 mmap 매핑 후 큐에 등록한다.
 *
 * @param fd     V4L2 디바이스 파일 디스크립터
 * @param count  요청할 버퍼 개수
 * @param type   버퍼 타입 (V4L2_BUF_TYPE_VIDEO_CAPTURE 등)
 * @return       성공 시 MmapBuffer 벡터, 실패 시 빈 벡터
 */
std::vector<MmapBuffer> allocBuffers(int fd, unsigned count,
                                     v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE);

/**
 * @brief ISP 입력용 DMABUF 슬롯을 할당한다.
 *
 * @param fd     V4L2 디바이스 파일 디스크립터
 * @param count  요청할 슬롯 개수
 * @return       성공 시 true, ioctl 실패 시 false
 */
bool allocDmabufSlots(int fd, unsigned count);

/**
 * @brief 스트리밍을 시작하고 워밍업 프레임을 소비한다.
 *
 * @param fd     V4L2 디바이스 파일 디스크립터
 * @param count  워밍업 시 캡처할 프레임 수
 * @return       성공 시 true, ioctl 실패 시 false
 */
bool startAndWarmup(int fd, unsigned count);

/**
 * @brief 캡처 완료된 프레임 버퍼를 큐에서 꺼낸다(dequeue).
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @return      dequeue된 v4l2_buffer. VIDIOC_DQBUF 실패 시 type/memory 필드만 설정된 빈 버퍼 반환
 */
v4l2_buffer grabFrame(int fd);

/**
 * @brief 처리 완료된 버퍼를 V4L2 큐에 반환한다(enqueue).
 *
 * @param fd    V4L2 디바이스 파일 디스크립터
 * @param buf   반환할 v4l2_buffer (grabFrame으로 얻은 버퍼)
 */
void        releaseFrame(int fd, v4l2_buffer& buf);

/**
 * @brief 지정한 버퍼 인덱스의 DMABUF fd를 export한다.
 *
 * @param fd     V4L2 디바이스 파일 디스크립터
 * @param index  export할 버퍼 인덱스
 * @return       성공 시 DMABUF 파일 디스크립터, 실패 시 -1
 */
int exportDmabufFd(int fd, unsigned index);

/**
 * @brief 제로카피 방식으로 ISP에 프레임을 입력하고 처리 완료된 출력 버퍼를 반환한다.
 *
 * @param isp_in_fd   ISP 입력 V4L2 디바이스 파일 디스크립터
 * @param isp_out_fd  ISP 출력 V4L2 디바이스 파일 디스크립터
 * @param ucam_buf    unicam에서 캡처한 입력 버퍼
 * @param dmabuf_fd   ISP 입력에 사용할 DMABUF 파일 디스크립터
 * @return            ISP 출력 큐에서 꺼낸 v4l2_buffer
 */
v4l2_buffer processISP(int isp_in_fd, int isp_out_fd,
                       const v4l2_buffer& ucam_buf, int dmabuf_fd);

/**
 * @brief 스트리밍을 중지하고 mmap 버퍼를 해제한다.
 *
 * @param fd      V4L2 디바이스 파일 디스크립터
 * @param buffers 해제할 mmap 버퍼 목록 (호출 후 비워짐)
 * @param type    버퍼 타입 (V4L2_BUF_TYPE_VIDEO_CAPTURE 등)
 */
void stopAndCleanup(int fd, std::vector<MmapBuffer>& buffers,
                    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE);

