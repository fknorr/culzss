cmake_minimum_required(VERSION 3.19)

project(culzss C CUDA)

add_library(culzss_cuda STATIC
    gpu_compress.cu
    gpu_compress.h
    gpu_decompress.cu
    gpu_decompress.h
)

add_executable(culzss
  culzss.c
  culzss.h
  decompression.c
  decompression.h
  deculzss.c
  deculzss.h
  main.c
)

target_compile_options(culzss PRIVATE "-O0;-UNDEBUG") # avoid effects of some undefined behavior

target_link_libraries(culzss culzss_cuda)
