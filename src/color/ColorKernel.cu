// LogForge CUDA implementation of the unchanged CPU double reference.
// Compile with --fmad=false --ftz=false --prec-div=true --prec-sqrt=true.
// No fast math, float16, lookup tables or approximate intrinsics.
struct Statistics {
    unsigned long long samples, floor, white, invalid;
    double inputMin, inputMax, outputMin, outputMax;
};
__device__ double linear(double v) {
    const double a=0.17883277, b=1.0-4.0*a, c=0.5-a*log(4.0*a);
    if (v<0.0) return -v*v/3.0;
    return v<=0.5 ? v*v/3.0 : (exp((v-c)/a)+b)/12.0;
}
__device__ double apple(double v) {
    const double r0=-0.05641088;
    if(v<r0) return 0.0;
    if(v<0.01) return 47.28711236*(v-r0)*(v-r0);
    return 0.08550479*log2(v+0.00964052)+0.69336945;
}
__device__ void encode(float& value,double v,Statistics& s) {
    const double out=apple(v);
    ++s.samples;
    s.invalid += !isfinite(value) || !isfinite(v) || !isfinite(out);
    s.floor += v < -0.05641088;
    s.white += out > 1.0;
    s.inputMin=fmin(s.inputMin,(double)value);s.inputMax=fmax(s.inputMax,(double)value);
    s.outputMin=fmin(s.outputMin,out);s.outputMax=fmax(s.outputMax,out);
    value=(float)out;
}
extern "C" __global__ void color(float* values,unsigned long long n,double scale,int creative,
                               double shadows,double highlights,double saturation,Statistics* stats) {
    __shared__ Statistics shared[128];
    Statistics s{0,0,0,0,1.0/0.0,-1.0/0.0,1.0/0.0,-1.0/0.0};
    const auto count=creative ? n/3 : n;
    for(auto i=(unsigned long long)blockIdx.x*blockDim.x+threadIdx.x;i<count;i+=(unsigned long long)gridDim.x*blockDim.x) {
        if(!creative) encode(values[i],linear(values[i])*scale,s);
        else {
            double r=linear(values[2*count+i])*scale, g=linear(values[i])*scale, b=linear(values[count+i])*scale;
            const double y=0.2627*r+0.6780*g+0.0593*b;
            const double x=y>0.0 ? log2(y/0.18) : -6.0;
            const double t=fmin(fmax(fabs(x)/6.0,0.0),1.0), weight=t*t*(3.0-2.0*t);
            const double gain=exp2((x<0.0 ? shadows : -highlights)*weight);
            encode(values[2*count+i],gain*(y+saturation*(r-y)),s);
            encode(values[i],gain*(y+saturation*(g-y)),s);
            encode(values[count+i],gain*(y+saturation*(b-y)),s);
        }
    }
    shared[threadIdx.x]=s;__syncthreads();
    for(unsigned stride=64;stride;stride/=2) {
        if(threadIdx.x<stride) {
            auto& a=shared[threadIdx.x];const auto& b=shared[threadIdx.x+stride];
            a.samples+=b.samples;a.floor+=b.floor;a.white+=b.white;a.invalid+=b.invalid;
            a.inputMin=fmin(a.inputMin,b.inputMin);a.inputMax=fmax(a.inputMax,b.inputMax);
            a.outputMin=fmin(a.outputMin,b.outputMin);a.outputMax=fmax(a.outputMax,b.outputMax);
        }
        __syncthreads();
    }
    if(threadIdx.x==0) stats[blockIdx.x]=shared[0];
}
