__global__ void k(float* a) {
    int i = blockIdx.x * blockDim.x +
        threadIdx.x;
    kernel<<<1, n>>>(a,
                     b);
}
