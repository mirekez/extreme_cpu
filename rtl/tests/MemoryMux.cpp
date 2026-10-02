#include "../MemoryMux.h"
MemoryMux top;
#ifndef SYNTHESIS
#define TEST_TOP MemoryMux
#include "../../tests/MuxHarness.h"
#include <set>
int main() {
 try {
    Harness h;auto& dut=h.dut;h.tick(true);h.tick(true);
    constexpr unsigned clients=2*EC_CORES, each=41;
    unsigned sent[clients]{}, received[clients]{}, wait[clients]{};
    struct Answer {unsigned tag;Word data;};
    std::deque<Answer> bank[EC_BANKS];
    std::set<unsigned> seen;
    std::mt19937 rng(17);
    unsigned completed=0, delivered=0;
    for(unsigned cycle=0;cycle<20000 && completed<clients*each;++cycle) {
        for(unsigned c=0;c<clients;++c) {
            h.request_in[c]=sent[c]<each;h.write_in[c]=c&1;h.tag_in[c]=sent[c];
            h.address_in[c]=((sent[c]%EC_BANKS)*EC_BANK_WORDS+c*64+sent[c])*(EC_BITS/8);
            h.data_in[c]=pattern(c*256+sent[c]);
        }
        for(unsigned b=0;b<EC_BANKS;++b) {
            h.mem_ready_in[b]=(cycle%9)>2;
            h.mem_response_in[b]=!bank[b].empty() && (rng()%3!=0);
            if(h.mem_response_in[b]) {h.mem_response_tag_in[b]=bank[b].front().tag;h.mem_response_data_in[b]=bank[b].front().data;}
        }
        h.settle();
        for(unsigned c=0;c<clients;++c) {
            if(OUT(response_out[c])) {
                unsigned tag=OUT(response_tag_out[c]);
                require(tag<sent[c] && seen.insert(c*256+tag).second,"duplicate or unsolicited response");
                require(read_word(OUT(response_data_out[c]))==pattern(c*256+tag),"response routing");
                require(!OUT(response_error_out[c]),"unexpected mux error");++received[c];++completed;
            }
            if(h.request_in[c] && OUT(accepted_out[c])) {++sent[c];wait[c]=0;}
            else if(h.request_in[c]) {++wait[c];require(wait[c]<clients*8+20,"round-robin starvation");}
        }
        for(unsigned b=0;b<EC_BANKS;++b) {
            if(h.mem_response_in[b] && OUT(mem_response_ready_out[b])) bank[b].pop_front();
            if(OUT(mem_request_out[b]) && h.mem_ready_in[b]) {
                unsigned tag=OUT(mem_tag_out[b]), c=tag>>8, n=tag&255;
                require(c<clients && n<each,"bad memory tag");
                require(OUT(mem_address_out[b])==(c*64+n)*(EC_BITS/8) && b==n%EC_BANKS,"region routing");
                require(bool(OUT(mem_write_out[b]))==bool(c&1),"operation routing");
                require(read_word(OUT(mem_data_out[b]))==pattern(tag),"request payload");
                bank[b].push_back({tag,pattern(tag)});++delivered;
            }
        }
        h.tick();
    }
    require(completed==clients*each && delivered==completed,"mux lost transaction");
    // Unmapped requests must complete with an error, not hang or alias bank zero.
    for(unsigned c=0;c<clients;++c) h.request_in[c]=false;
    for(unsigned b=0;b<EC_BANKS;++b) h.mem_response_in[b]=false;
    h.request_in[0]=true;h.address_in[0]=EC_BANKS*EC_BANK_WORDS*(EC_BITS/8);h.tag_in[0]=77;
    h.settle();require(OUT(accepted_out[0]),"unmapped acceptance");h.tick();h.request_in[0]=false;
    bool failed=false;
    for(unsigned n=0;n<6;++n) {h.settle();if(OUT(response_out[0])) {require(OUT(response_error_out[0]) && OUT(response_tag_out[0])==77,"unmapped response");failed=true;}h.tick();}
    require(failed,"unmapped request hung");
    if(EC_BANKS>=2) {
        for(unsigned c=0;c<clients;++c) h.request_in[c]=false;
        for(unsigned b=0;b<EC_BANKS;++b) {h.mem_response_in[b]=false;h.mem_ready_in[b]=true;}
        h.tick(true);
        h.request_in[0]=true;h.address_in[0]=0;h.tag_in[0]=11;
        h.request_in[1]=true;h.address_in[1]=EC_BANK_WORDS*(EC_BITS/8);h.tag_in[1]=22;
        h.settle();require(OUT(accepted_out[0]) && OUT(accepted_out[1]),"independent banks must accept together");
        h.tick();h.request_in[0]=false;h.mem_ready_in[0]=false;
        for(unsigned n=0;n<5;++n) {
            h.settle();
            require(OUT(mem_request_out[0]) && OUT(mem_tag_out[0])==11,"stalled bank request changed");
            require(OUT(accepted_out[1]) && OUT(mem_request_out[1]),"stalled bank blocked another bank");
            h.tick();
        }
        h.request_in[1]=false;h.mem_ready_in[0]=true;h.tick();
        h.mem_response_in[0]=true;h.mem_response_tag_in[0]=11;
        h.mem_response_in[1]=true;h.mem_response_tag_in[1]=(1<<8)|22;
        h.settle();require(OUT(mem_response_ready_out[0]) && OUT(mem_response_ready_out[1]),"independent FIFO answers must progress together");
        h.tick();h.mem_response_in[0]=false;h.mem_response_in[1]=false;h.settle();
        require(OUT(response_out[0]) && OUT(response_out[1]),"parallel registered answers missing");
        require(OUT(response_tag_out[0])==11 && OUT(response_tag_out[1])==22,"parallel answer tags");
        h.tick();
        // Two banks returning to the SAME FIFO must arbitrate without loss.
        h.mem_response_in[0]=true;h.mem_response_tag_in[0]=31;
        h.mem_response_in[1]=true;h.mem_response_tag_in[1]=32;
        h.settle();bool first=OUT(mem_response_ready_out[0]);
        require(first!=bool(OUT(mem_response_ready_out[1])),"multiple answers delivered to one FIFO");
        h.tick();h.mem_response_in[first ? 0 : 1]=false;h.settle();
        require(OUT(response_out[0]) && OUT(response_tag_out[0])==(first ? 31 : 32),"first colliding answer");
        require(OUT(mem_response_ready_out[first ? 1 : 0]),"colliding answer starved");
        h.tick();h.mem_response_in[0]=false;h.mem_response_in[1]=false;h.settle();
        require(OUT(response_out[0]) && OUT(response_tag_out[0])==(first ? 32 : 31),"second colliding answer");
    }
    std::cout<<"MemoryMux PASS\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#endif
