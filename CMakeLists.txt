cmake_minimum_required(VERSION 3.19)

project(culzss C CUDA)

add_library(culzss_cuda STATIC
    gpu_compress.cu
    gpu_compress.h
    gpu_decompress.cu
    gpu_decompress.h
)

target_compile_options(culzss_cuda PRIVATE "-fPIC")

add_library(culzss SHARED
    culzss.c
    culzss.h
    decompression.c
    decompression.h
    deculzss.c
    deculzss.h
    lib.h
    lib.c
)

target_compile_options(culzss PRIVATE "-O0") # avoid effects of some undefined behavior

target_link_libraries(culzss culzss_cuda)
