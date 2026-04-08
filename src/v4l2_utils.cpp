#include "v4l2_utils.h"
#include <cstdio>
#include <fcntl.h>

/**
 * @brief Set a sensor control value on a V4L2 device.
 *
 * @param fd    V4L2 device file descriptor
 * @param id    Control ID to set (V4L2_CID_* constant)
 * @param value Value to set
 * @param name  Control name string used for log output
 * @return      true on success, false if ioctl fails
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
 * @brief Verify that a V4L2 sensor control matches the expected value.
 *
 * @param fd    V4L2 device file descriptor
 * @param id    Control ID to read (V4L2_CID_* constant)
 * @param value Expected value (compared against what was read)
 * @param name  Control name string used for log output
 * @return      Current control value on match, -1 on mismatch or ioctl failure
 */
int validSensorCtrl(int fd, int id,
                    int value, const char* name) {
    v4l2_control ctrl{};
    ctrl.id = id;
    if (::ioctl(fd, VIDIOC_G_CTRL, &ctrl) < 0) {
        perror(name);
        return -1;
    }
    if(ctrl.value == value){
        printf("  [GET] %s=%d\n", name, ctrl.value);
        return ctrl.value;
    }
    else{
        fprintf(stderr, " [ERROR] Invalid value for %s: %d\n", name, ctrl.value);
        return -1;
    }

}

/**
 * @brief Read the current value of a V4L2 sensor control.
 *
 * @param fd    V4L2 device file descriptor
 * @param id    Control ID to read (V4L2_CID_* constant)
 * @param name  Control name string used for log output
 * @return      Current control value on success, -1 if ioctl fails
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
 * @brief Configure the image format on the unicam V4L2 device.
 *
 * @param fd             V4L2 device file descriptor
 * @param w              Image width to set
 * @param h              Image height to set
 * @param out_stride     Actual memory width in bytes used to store one row
 * @param out_sizeimage  Total memory needed to store one full frame (Stride * Height, bytes)
 * @return               true on success, false if ioctl fails
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
    // Read back the values the driver actually applied
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
 * @brief Allocate DMA buffers on a V4L2 device.
 *
 * @param fd             V4L2 device file descriptor
 * @param count          Number of buffers to request
 * @param type           Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE, etc.)
 * @return               Vector of MmapBuffer on success, empty vector on failure
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
 * @brief Warm-up before the real capture.
 *
 * @param fd     V4L2 device file descriptor
 * @param count  Number of frames to drain during warm-up
 * @return       true on success, false if ioctl fails
 */
/**
 * @brief Start V4L2 streaming (VIDIOC_STREAMON).
 *
 * @param fd    V4L2 device file descriptor
 * @return      true on success, false if ioctl fails
 */
bool startStream(int fd) {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (::ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("VIDIOC_STREAMON");
        return false;
    }
    printf("\n  [STREAM] started\n");
    return true;
}

/**
 * @brief Warm-up: drain `count` frames via dequeue → enqueue.
 *        Streaming must already be started (call startStream first).
 *        A successful first DQBUF is treated as confirmation that the stream is running.
 *
 * @param fd     V4L2 device file descriptor
 * @param count  Number of frames to drain
 * @return       true on success, false if DQBUF fails
 */
bool warmup(int fd, unsigned count) {
    printf("  warming up (%u frames)...\n", count);
    for (unsigned i = 0; i < count; ++i) {
        v4l2_buffer wbuf{};
        wbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        wbuf.memory = V4L2_MEMORY_MMAP;

        if (::ioctl(fd, VIDIOC_DQBUF, &wbuf) < 0) {
            perror("warmup VIDIOC_DQBUF");
            return false;
        }
        if (::ioctl(fd, VIDIOC_QBUF, &wbuf) < 0) {
            perror("warmup VIDIOC_QBUF");
            return false;
        }
    }
    return true;
}


/**
 * @brief Dequeue a completed frame buffer from the queue.
 *
 * @param fd    V4L2 device file descriptor
 * @return      The dequeued v4l2_buffer. On VIDIOC_DQBUF failure, returns an empty
 *              buffer with only type/memory fields set.
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
 * @brief Export a DMABUF fd from a DMA buffer.
 *
 * @param fd             V4L2 device file descriptor
 * @param index          Buffer index to export
 * @return               DMABUF file descriptor on success, -1 on failure
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
 * @brief Return a processed buffer back to the V4L2 queue (enqueue).
 *
 * @param fd    V4L2 device file descriptor
 * @param buf   v4l2_buffer to return (previously obtained via grabFrame)
 */
void releaseFrame(int fd, v4l2_buffer& buf) {
    if (::ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        perror("VIDIOC_QBUF");
    }
}

/**
 * @brief Stop streaming and release mmap buffers.
 *
 * @param fd      V4L2 device file descriptor
 * @param buffers List of mmap buffers to release (emptied on return)
 * @param type    Buffer type (V4L2_BUF_TYPE_VIDEO_CAPTURE, etc.)
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

