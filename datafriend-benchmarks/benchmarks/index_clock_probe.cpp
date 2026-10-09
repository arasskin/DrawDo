#include <chrono>
#include <cstdio>
#include <cstdint>
using clock_type=std::chrono::steady_clock;
uint64_t ticks(){uint64_t v;asm volatile("isb; mrs %0,cntvct_el0":"=r"(v)::"memory");return v;}
int main(){
 uint64_t hz;asm volatile("mrs %0,cntfrq_el0":"=r"(hz));
 auto start=clock_type::now();auto first=ticks();volatile uint64_t x=1;
 do{for(int i=0;i<10000;++i)x=x*6364136223846793005ULL+1;}while(clock_type::now()-start<std::chrono::milliseconds(50));
 auto last=ticks();auto end=clock_type::now();
 auto steady=std::chrono::duration<double,std::nano>(end-start).count();
 auto hardware=double(last-first)*1e9/hz;
 start=clock_type::now();for(int i=0;i<100000;++i)x=ticks();end=clock_type::now();
 auto direct=std::chrono::duration<double,std::nano>(end-start).count()/100000;
 start=clock_type::now();for(int i=0;i<100000;++i)x=clock_type::now().time_since_epoch().count();end=clock_type::now();
 auto standard=std::chrono::duration<double,std::nano>(end-start).count()/100000;
 printf("{\"frequency_hz\":%llu,\"hardware_elapsed_ns\":%.0f,\"steady_elapsed_ns\":%.0f,\"ratio\":%.6f,\"direct_read_ns\":%.2f,\"steady_read_ns\":%.2f}\n",(unsigned long long)hz,hardware,steady,hardware/steady,direct,standard);
 return hardware/steady>.99&&hardware/steady<1.01?0:1;
}
