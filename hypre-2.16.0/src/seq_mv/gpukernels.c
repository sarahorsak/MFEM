#include "seq_mv.h"

//#include <stdio.h>
//#include <cuda_runtime.h>
//#include <cublas_v2.h>

#if defined(HYPRE_USING_GPU)

extern "C"{
  __global__
  void VecScaleKernelText(HYPRE_Complex *__restrict__ u, const HYPRE_Complex *__restrict__ v, const HYPRE_Complex *__restrict__ l1_norm, hypre_int num_rows){
    HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i<num_rows){
      u[i]+=__ldg(v+i)/__ldg(l1_norm+i);
    }
  }
}

extern "C"{
  __global__
  void VecScaleKernel(HYPRE_Complex *__restrict__ u, const HYPRE_Complex *__restrict__ v, const HYPRE_Complex * __restrict__ l1_norm, hypre_int num_rows){
    HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i<num_rows){
      u[i]+=v[i]/l1_norm[i];
  }
  }
}

extern "C"{
  void VecScale(HYPRE_Complex *u, HYPRE_Complex *v, HYPRE_Complex *l1_norm, hypre_int num_rows,cudaStream_t s){
    PUSH_RANGE_PAYLOAD("VECSCALE",1,num_rows);
    const HYPRE_Int tpb=64;
    HYPRE_Int num_blocks=num_rows/tpb+1;
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    MemPrefetchSized(l1_norm,num_rows*sizeof(HYPRE_Complex),HYPRE_DEVICE,s);
    VecScaleKernel<<<num_blocks,tpb,0,s>>>(u,v,l1_norm,num_rows);
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    hypre_CheckErrorDevice(cudaStreamSynchronize(s));
    POP_RANGE;
  }
}


extern "C"{

  __global__
  void VecCopyKernel(HYPRE_Complex* __restrict__ tgt, const HYPRE_Complex* __restrict__ src, hypre_int size){
    HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i<size) tgt[i]=src[i];
}
  void VecCopy(HYPRE_Complex* tgt, const HYPRE_Complex* src, hypre_int size,cudaStream_t s){
    HYPRE_Int tpb=64;
    HYPRE_Int num_blocks=size/tpb+1;
    PUSH_RANGE_PAYLOAD("VecCopy",5,size);
    //MemPrefetch(tgt,0,s);
    //MemPrefetch(src,0,s);
    VecCopyKernel<<<num_blocks,tpb,0,s>>>(tgt,src,size);
    //hypre_CheckErrorDevice(cudaStreamSynchronize(s));
    POP_RANGE;
  }
}
extern "C"{

  __global__
  void VecSetKernel(HYPRE_Complex* __restrict__ tgt, const HYPRE_Complex value,hypre_int size){
    HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i<size) tgt[i]=value;
}
  void VecSet(HYPRE_Complex* tgt, hypre_int size, HYPRE_Complex value, cudaStream_t s){
    HYPRE_Int tpb=64;
    //cudaDeviceSynchronize();
    MemPrefetchSized(tgt,size*sizeof(HYPRE_Complex),HYPRE_DEVICE,s);
    HYPRE_Int num_blocks=size/tpb+1;
    VecSetKernel<<<num_blocks,tpb,0,s>>>(tgt,value,size);
    cudaStreamSynchronize(s);
    //cudaDeviceSynchronize();
  }
}
extern "C"{
  __global__
  void  PackOnDeviceKernel(HYPRE_Complex* __restrict__ send_data,const HYPRE_Complex* __restrict__ x_local_data, const HYPRE_Int* __restrict__ send_map, HYPRE_Int begin, HYPRE_Int end){
    HYPRE_Int i = begin+blockIdx.x * blockDim.x + threadIdx.x;
    if (i<end){
      send_data[i-begin]=x_local_data[send_map[i]];
    }
  }
  void PackOnDevice(HYPRE_Complex *send_data,HYPRE_Complex *x_local_data, HYPRE_Int *send_map, HYPRE_Int begin, HYPRE_Int end,cudaStream_t s){
    if ((end-begin)<=0) return;
    HYPRE_Int tpb=64;
    HYPRE_Int num_blocks=(end-begin)/tpb+1;
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    PackOnDeviceKernel<<<num_blocks,tpb,0,s>>>(send_data,x_local_data,send_map,begin,end);
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    PUSH_RANGE("PACK_PREFETCH",1);
#ifndef HYPRE_GPU_USE_PINNED
    MemPrefetchSized((void*)send_data,(end-begin)*sizeof(HYPRE_Complex),cudaCpuDeviceId,s);
#endif
    POP_RANGE;
    //hypre_CheckErrorDevice(cudaStreamSynchronize(s));
  }
}

  // Scale vector by scalar

extern "C"{
__global__
void VecScaleScalarKernel(HYPRE_Complex *__restrict__ u, const HYPRE_Complex alpha, HYPRE_Int num_rows){
  HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
  //if (i<5) printf("DEVICE %d %lf %lf %lf\n",i,u[i],v[i],l1_norm[i]);
  if (i<num_rows){
    u[i]*=alpha;
    //if (i==0) printf("Diff Device %d %lf %lf %lf\n",i,u[i],v[i],l1_norm[i]);
  }
}
}
extern "C"{
  hypre_int VecScaleScalar(HYPRE_Complex *u, const HYPRE_Complex alpha, HYPRE_Int num_rows,cudaStream_t s){
    PUSH_RANGE("SEQVECSCALE",4);
    HYPRE_Int num_blocks=num_rows/64+1;

#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    VecScaleScalarKernel<<<num_blocks,64,0,s>>>(u,alpha,num_rows);
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    hypre_CheckErrorDevice(cudaStreamSynchronize(s));
    POP_RANGE;
    return 0;
  }
}

extern "C"{
__global__
void DiagScaleVectorKernel(HYPRE_Complex* __restrict__ x, HYPRE_Complex* __restrict y, HYPRE_Complex* __restrict__ A_data, const hypre_int* __restrict__ A_i, HYPRE_Int num_rows){
   HYPRE_Int i= blockIdx.x * blockDim.x + threadIdx.x;
   if (i<num_rows)
   {
      x[i] = y[i]/A_data[A_i[i]];
   }
}
}

extern "C"{
   void DiagScaleVector(HYPRE_Complex *x, HYPRE_Complex *y, HYPRE_Complex *A_data, HYPRE_Int *A_i, HYPRE_Int num_rows, cudaStream_t s){
      const HYPRE_Int tpb=64;
      HYPRE_Int num_blocks=num_rows/tpb+1;
      DiagScaleVectorKernel<<<num_blocks,tpb,0,s>>>(x,y,A_data,A_i,num_rows);
      hypre_CheckErrorDevice(cudaStreamSynchronize(s));
   }
}

extern "C"{
__global__
void SpMVCudaKernel(HYPRE_Complex* __restrict__ y,HYPRE_Complex alpha, const HYPRE_Complex* __restrict__ A_data, const HYPRE_Int* __restrict__ A_i, const HYPRE_Int* __restrict__ A_j, const HYPRE_Complex* __restrict__ x, HYPRE_Complex beta, HYPRE_Int num_rows)
{
  HYPRE_Int i= blockIdx.x * blockDim.x + threadIdx.x;
  if (i<num_rows){
    HYPRE_Complex temp = 0.0;
    HYPRE_Int jj;
    for (jj = A_i[i]; jj < A_i[i+1]; jj++){
      HYPRE_Int ajj=A_j[jj];
      temp += A_data[jj] * x[ajj];
    }
    y[i] =y[i]*beta+alpha*temp;
  }
}

__global__
void SpMVCudaKernelZB(HYPRE_Complex* __restrict__ y,HYPRE_Complex alpha, const HYPRE_Complex* __restrict__ A_data, const HYPRE_Int* __restrict__ A_i, const HYPRE_Int* __restrict__ A_j, const HYPRE_Complex* __restrict__ x, HYPRE_Int num_rows)
{
  HYPRE_Int i= blockIdx.x * blockDim.x + threadIdx.x;
  if (i<num_rows){
    HYPRE_Complex temp = 0.0;
    HYPRE_Int jj;
    for (jj = A_i[i]; jj < A_i[i+1]; jj++){
      HYPRE_Int ajj=A_j[jj];
      temp += A_data[jj] * x[ajj];
    }
    y[i] = alpha*temp;
  }
}
  void SpMVCuda(HYPRE_Int num_rows,HYPRE_Complex alpha, HYPRE_Complex *A_data,HYPRE_Int *A_i, HYPRE_Int *A_j, HYPRE_Complex *x, HYPRE_Complex beta, HYPRE_Complex *y){
    HYPRE_Int num_threads=64;
    HYPRE_Int num_blocks=num_rows/num_threads+1;
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif
    if (beta==0.0)
      SpMVCudaKernelZB<<<num_blocks,num_threads>>>(y,alpha,A_data,A_i,A_j,x,num_rows);
    else
      SpMVCudaKernel<<<num_blocks,num_threads>>>(y,alpha,A_data,A_i,A_j,x,beta,num_rows);
#ifdef CATCH_LAUNCH_ERRORS
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
#endif

}
}
extern "C"{
  __global__
  void CompileFlagSafetyCheck(HYPRE_Int actual){
#ifdef __CUDA_ARCH__
    HYPRE_Int cudarch=__CUDA_ARCH__;
    if (cudarch!=actual){
      //printf("WARNING :: nvcc -arch flag does not match actual device architecture\nWARNING :: The code can fail silently and produce wrong results\n");
      //printf("Arch specified at compile = sm_%d Actual device = sm_%d\n",cudarch/10,actual/10);
    }
#else
    hypre_error_w_msg(HYPRE_ERROR_GENERIC,"ERROR:: CUDA_ ARCH is not defined \n This should not be happening\n");
#endif
  }
}
extern "C"{
  void CudaCompileFlagCheck(){
    HYPRE_Int devCount;
    cudaGetDeviceCount(&devCount);
    HYPRE_Int i;
    HYPRE_Int cudarch_actual;
    for(i = 0; i < devCount; ++i)
      {
	struct cudaDeviceProp props;
	cudaGetDeviceProperties(&props, i);
	cudarch_actual=props.major*100+props.minor*10;
    }
    hypre_CheckErrorDevice(cudaPeekAtLastError());
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
    CompileFlagSafetyCheck<<<1,1,0,0>>>(cudarch_actual);
    cudaError_t code=cudaPeekAtLastError();
    if (code != cudaSuccess)
      {
	hypre_error_w_msg(HYPRE_ERROR_GENERIC,"ERROR in CudaCompileFlagCheck \nERROR :: Check if compile arch flags match actual device arch = sm_\n");
	//fprintf(stderr,"ERROR in CudaCompileFlagCheck%s \n", cudaGetErrorString(code));
	//fprintf(stderr,"ERROR :: Check if compile arch flags match actual device arch = sm_%d\n",cudarch_actual/10);
	exit(2);
      }
    hypre_CheckErrorDevice(cudaDeviceSynchronize());
  }
}

extern "C"
{

   __global__
   void BigToSmallCopyKernel (HYPRE_Int* __restrict__ tgt, const HYPRE_BigInt* __restrict__ src, HYPRE_Int size)
   {
      HYPRE_Int i = blockIdx.x * blockDim.x + threadIdx.x;
      if (i<size) tgt[i] = src[i];
   }
 
   void BigToSmallCopy (HYPRE_Int* tgt, const HYPRE_BigInt* src, HYPRE_Int size, cudaStream_t s)
   {
      HYPRE_Int tpb=64;
      HYPRE_Int num_blocks=size/tpb+1;
      PUSH_RANGE_PAYLOAD("VecCopy",5,size);
      //MemPrefetch(tgt,0,s);
      //MemPrefetch(src,0,s);
      BigToSmallCopyKernel<<<num_blocks,tpb,0,s>>>(tgt,src,size);
      //hypre_CheckErrorDevice(cudaStreamSynchronize(s));
      POP_RANGE;
   }
}

#endif
