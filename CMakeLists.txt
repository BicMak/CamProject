cmake_minimum_required(VERSION 3.16)
project(v4l2_mipi_capture
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "V4L2 MIPI CSI camera capture for Raspberry Pi"
)

# ── C++ 표준 설정 ─────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)    # GNU 확장 끄기 (이식성)

# ── 빌드 타입 기본값 ─────────────────────────────────────────
# cmake -B build -DCMAKE_BUILD_TYPE=Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# ── 컴파일러 경고 ────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter      # V4L2 콜백에서 안 쓰는 파라미터 많음
)

# Debug일 때 sanitizer (sexyPi에서 메모리 버그 잡기용)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

# ── 공통 유틸 라이브러리 ─────────────────────────────────────
add_library(v4l2_utils STATIC
    src/v4l2_utils.cpp
)
target_include_directories(v4l2_utils PUBLIC include)

# ── 타겟: unicam raw 덤프 (ISP 없음) ────────────────────────
add_executable(unicam_capture
    src/unicam_capture.cpp
)
target_link_libraries(unicam_capture PRIVATE
    v4l2_utils
)

# ── (확장용) Hailo, OpenMP 등 붙일 때 ────────────────────────
# find_package(HailoRT QUIET)
# if(HailoRT_FOUND)
#     target_link_libraries(v4l2_capture PRIVATE HailoRT::HailoRT)
#     target_compile_definitions(v4l2_capture PRIVATE USE_HAILO=1)
# endif()
#
# find_package(OpenMP QUIET)
# if(OpenMP_CXX_FOUND)
#     target_link_libraries(v4l2_capture PRIVATE OpenMP::OpenMP_CXX)
# endif()
