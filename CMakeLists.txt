project(culzss C CUDA)

add_executable(culzss
  culzss.c
  culzss.h
  decompression.c
  decompression.h
  deculzss.c
  deculzss.h
  gpu_compress.cu
  gpu_compress.h
  gpu_decompress.cu
  gpu_decompress.h
  main.c
)

