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

// ── mmap buffer info ────────────────────────────────────────
struct MmapBuffer {
    void*  start  = nullptr;
    size_t length = 0;
};

/**
 * @brief Set a sensor control value on a V4L2 device.
 *
 * @param fd    V4L2 device file descriptor
 * @param id    Control ID to set (V4L2_CID_* constant)
 * @param value Value to set
 * @param name  Control name string used for log output
 * @return      true on success, false if ioctl fails
 */
bool setSensorCtrl(int fd, int id, int value, const char* name);

/**
 * @brief Verify a V4L2 sensor control value against an expected value.
 *
 * @param fd    V4L2 device file descriptor
 * @param id    Control ID to read (V4L2_CID_* constant)
 * @param value Expected value (compared against what was read)
 * @param name  Control name string used for log output
 * @return      Current control value on match, -1 on mismatch or ioctl failure
 */
int  validSensorCtrl(int fd, int id, int value, const char* name);

/**
 * @brief Read the current value of a V4L2 sensor control.
 *
 * @param fd           V4L2 device file descriptor
 * @param id           Control ID to read (V4L2_CID_* constant)
 * @param target_name  Control name string used for log output
 * @return             Current control value on success, -1 if ioctl fails
 */
int  getSensorCtrl(int fd, int id, const char* target_name);

/**
 * @brief Configure the image format on the unicam V4L2 device.
 *
 * @param fd             V4L2 device file descriptor
 * @param w              Image width to set
 * @param h              Image height to set
 * @param pixfmt         Pixel format (V4L2_PIX_FMT_* constant)
 * @param out_stride     Actual memory width in bytes used to store one row (may be NULL)
 * @param out_sizeimage  Total size of one frame (Stride * Height, bytes; may be NULL)
 * @return               true on success, false if ioctl fails
 */
bool setCaptureFormat(int video_fd, unsigned w, unsigned h,
                      uint32_t pixfmt,
                      unsigned* out_stride = nullptr,
                      unsigned* out_sizeimage = nullptr);

/**
 * @brief Allocate DMA buffers on a V4L2 device, mmap them, and enqueue them.
 *
 * @param fd     V4L2 device file descriptor
 * @param count  Number of buffers to request
 * @param type   Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE, etc.)
 * @return       Vector of MmapBuffer on success, empty vector on failure
 */
std::vector<MmapBuffer> allocBuffers(int fd, unsigned count,
                                     v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE);

/**
 * @brief Allocate DMABUF slots for ISP input.
 *
 * @param fd     V4L2 device file descriptor
 * @param count  Number of slots to request
 * @return       true on success, false if ioctl fails
 */
bool allocDmabufSlots(int fd, unsigned count);

/**
 * @brief Start V4L2 streaming (VIDIOC_STREAMON).
 *
 * @param fd  V4L2 device file descriptor
 * @return    true on success, false if ioctl fails
 */
bool startStream(int fd);

/**
 * @brief Warm-up: drain `count` frames via dequeue → enqueue.
 *        Requires startStream to have been called first. A successful first
 *        DQBUF also serves as verification that the stream is running.
 *
 * @param fd     V4L2 device file descriptor
 * @param count  Number of frames to drain
 * @return       true on success, false if ioctl fails
 */
bool warmup(int fd, unsigned count);

/**
 * @brief Dequeue a completed frame buffer from the queue.
 *
 * @param fd    V4L2 device file descriptor
 * @return      The dequeued v4l2_buffer. On VIDIOC_DQBUF failure, returns an
 *              empty buffer with only type/memory fields set.
 */
v4l2_buffer grabFrame(int fd);

/**
 * @brief Return a processed buffer back to the V4L2 queue (enqueue).
 *
 * @param fd    V4L2 device file descriptor
 * @param buf   v4l2_buffer to return (previously obtained via grabFrame)
 */
void        releaseFrame(int fd, v4l2_buffer& buf);

/**
 * @brief Export the DMABUF fd for a given buffer index.
 *
 * @param fd     V4L2 device file descriptor
 * @param index  Buffer index to export
 * @return       DMABUF file descriptor on success, -1 on failure
 */
int exportDmabufFd(int fd, unsigned index);

/**
 * @brief Zero-copy feed a frame into the ISP and return the processed output buffer.
 *
 * @param isp_in_fd   ISP input V4L2 device file descriptor
 * @param isp_out_fd  ISP output V4L2 device file descriptor
 * @param ucam_buf    Input buffer captured from unicam
 * @param dmabuf_fd   DMABUF file descriptor used as ISP input
 * @return            v4l2_buffer dequeued from the ISP output queue
 */
v4l2_buffer processISP(int isp_in_fd, int isp_out_fd,
                       const v4l2_buffer& ucam_buf, int dmabuf_fd);

/**
 * @brief Stop streaming and release mmap buffers.
 *
 * @param fd      V4L2 device file descriptor
 * @param buffers List of mmap buffers to release (emptied on return)
 * @param type    Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE, etc.)
 */
void stopAndCleanup(int fd, std::vector<MmapBuffer>& buffers,
                    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE);

