#include <stdint.h>
volatile int8_t signed_byte=-7;
volatile uint8_t unsigned_byte=251;
volatile int16_t signed_half=-1234;
volatile uint16_t unsigned_half=60000;
volatile uint32_t input=17;
alignas(64) volatile uint8_t bytes[128];
struct __attribute__((packed)) Packed {uint8_t lead;uint32_t value;uint16_t tail;};
volatile Packed packed;
extern "C" unsigned kernel(){
    for(unsigned i=0;i<128;++i)bytes[i]=uint8_t(i*3u+1);
    packed.lead=91;packed.value=0x12345678;packed.tail=0xabcd;
    unsigned sum=0;for(unsigned i=0;i<128;++i)sum+=bytes[i];
    if(signed_byte!=-7 || unsigned_byte!=251 || signed_half!=-1234 || unsigned_half!=60000)return 1;
    if(packed.lead!=91 || packed.value!=0x12345678 || packed.tail!=0xabcd)return 2;
    unsigned n=input;
    int s=-int(n);
    if(s/3!=-5 || s%3!=-2 || (s>>2)!=-5)return 3;
    return sum+n*7u+n/3u+n%3u;
}
