#pragma once
#include "Config.h"
// Slots remain allocated through completion. Tags permit bank response reordering.
class StoreFifo : public Module {
public:
    _PORT(bool) push_in;
    _PORT(u<32>) address_in;
    _PORT(logic<EC_BITS>) data_in;
    _PORT(u<EC_BITS/8>) mask_in;
    _PORT(u<EC_BITS/8>) request_mask_out = _ASSIGN(masks[(uint32_t)send]);
    _PORT(bool) kind_in;
    _PORT(bool) ready_out = _ASSIGN(used < EC_DEPTH);
    _PORT(bool) pop_in;
    _PORT(bool) valid_out = _ASSIGN(used != 0 && complete[(uint32_t)head] != 0);
    _PORT(logic<EC_BITS>) result_out = _ASSIGN(values[(uint32_t)head]);
    _PORT(u<32>) result_address_out = _ASSIGN(addresses[(uint32_t)head]);
    _PORT(bool) result_kind_out = _ASSIGN(kinds[(uint32_t)head] != 0);
    _PORT(bool) empty_out = _ASSIGN(used == 0);
    _PORT(bool) request_out = _ASSIGN(queued != 0);
    _PORT(u<32>) request_address_out = _ASSIGN(addresses[(uint32_t)send]);
    _PORT(logic<EC_BITS>) request_data_out = _ASSIGN(values[(uint32_t)send]);
    _PORT(u<32>) request_tag_out = _ASSIGN(u<32>(send));
    _PORT(bool) accepted_in;
    _PORT(bool) response_in;
    _PORT(u<32>) response_tag_in;
    _PORT(logic<EC_BITS>) response_data_in;
    _PORT(bool) response_error_in;
    _PORT(bool) error_out = _ASSIGN(failed != 0);
private:
    reg<u<32>> head, tail, send, used, queued;
    reg<u<EC_BITS/8>> masks[EC_DEPTH];
    reg<u1> failed;
    reg<u<32>> addresses[EC_DEPTH];
    reg<logic<EC_BITS>> values[EC_DEPTH];
    reg<u1> kinds[EC_DEPTH], complete[EC_DEPTH];
public:
    void _assign() {}
    void _work(bool reset) {
        unsigned i;
        bool add, remove, issue;
        if (reset) {
            head._next=0; tail._next=0; send._next=0; used._next=0; queued._next=0; failed._next=0;
            for (i=0;i<EC_DEPTH;++i) {
                complete[i]._next=0; kinds[i]._next=0;
                masks[i]._next=0; addresses[i]._next=0; values[i]._next=0;
            }
        } else {
        add = push_in() && ready_out();
        remove = valid_out() && true;
        issue = request_out() && accepted_in();
            used._next = used + (add ? 1 : 0) - (remove ? 1 : 0);
            queued._next = queued + (add ? 1 : 0) - (issue ? 1 : 0);
            if (add) {
                masks[(uint32_t)tail]._next=mask_in();
                addresses[(uint32_t)tail]._next=address_in();
                values[(uint32_t)tail]._next=data_in();
                kinds[(uint32_t)tail]._next=kind_in();
                complete[(uint32_t)tail]._next=0;
                tail._next=(tail+1)&(EC_DEPTH-1);
            }
            if (issue) send._next=(send+1)&(EC_DEPTH-1);
            if (response_in()) {
                complete[(uint32_t)response_tag_in() & (EC_DEPTH-1)]._next=1;

                if (response_error_in()) failed._next=1;
            }
            if (remove) {
                complete[(uint32_t)head]._next=0;
                head._next=(head+1)&(EC_DEPTH-1);
            }
        }
    }
    void _strobe() {
        unsigned i;
        head.strobe(); tail.strobe(); send.strobe(); used.strobe(); queued.strobe(); failed.strobe();
        for (i=0;i<EC_DEPTH;++i) {
            masks[i].strobe(); addresses[i].strobe(); values[i].strobe(); kinds[i].strobe(); complete[i].strobe();
        }
    }
};
