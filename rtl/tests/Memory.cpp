#include "../Memory.h"
Memory top;
#ifndef SYNTHESIS
#define TEST_TOP Memory
#include "../../tests/MemoryHarness.h"
int main() {
 try {
    Harness h;auto& dut=h.dut;
    h.tick(true);h.enable_in=true;h.response_ready_in=true;
    for(unsigned i=0;i<67;++i) {
        for(unsigned wr=0;wr<2;++wr) {
            h.request_in=true;h.write_in=wr==0;h.address_in=i*(EC_BITS/8);h.tag_in=i+123;h.data_in=pattern(i);
            h.settle();require(OUT(ready_out),"RAM ready");h.tick();h.request_in=false;h.response_ready_in=false;
            for(unsigned stall=0;stall<5;++stall) {
                h.settle();require(OUT(response_out) && OUT(response_tag_out)==i+123 && !OUT(response_error_out),"RAM response metadata");
                require(!OUT(ready_out),"RAM overwrite stalled response");
                if(wr) require(read_word(OUT(response_data_out))==pattern(i),"RAM data");
                h.tick();
            }
            h.response_ready_in=true;h.tick();h.settle();require(!OUT(response_out),"RAM drain");
        }
    }
    for(unsigned addr: {1u,unsigned(EC_BANK_WORDS*(EC_BITS/8))}) {
        h.request_in=true;h.address_in=addr;h.tick();h.request_in=false;h.settle();require(OUT(response_error_out),"bad address must fail");h.tick();
    }
    h.enable_in=false;h.request_in=true;h.tick();h.settle();require(!OUT(ready_out) && !OUT(response_out),"controller stall");
    h.tick(true);h.settle();require(!OUT(response_out),"reset");
    std::cout<<"Memory PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#endif
