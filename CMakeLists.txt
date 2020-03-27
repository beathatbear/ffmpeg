cmake_minimum_required(VERSION 3.0.0)
project(aac VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 11)
include(CTest)
enable_testing()
include_directories("${CMAKE_SOURCE_DIR}/compiled/x86/include")
link_directories("${CMAKE_SOURCE_DIR}/compiled/x86/lib")
link_directories("/home/wy/build/x264/lib")
add_executable(aac transcode_test.cpp transcode.cpp)
add_executable(bbc ${CMAKE_SOURCE_DIR}/doc/examples/decode_audio.cpp )
target_link_libraries(bbc avformat avcodec avutil avfilter mp3lame )

target_link_libraries(aac avformat swresample avcodec avutil avfilter mp3lame fdk-aac)
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)

