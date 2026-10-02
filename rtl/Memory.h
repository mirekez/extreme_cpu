#pragma once
#include "Config.h"

// Controller adapter / synthesizable RAM. Addresses are local BYTE addresses.
// A response_out acknowledges that a store is visible in this memory.
class Memory : public Module {
public:
    _PORT(bool) request_in, write_in, response_ready_in;
    _PORT(u<32>) address_in, tag_in;
    _PORT(logic<EC_BITS>) data_in;
    _PORT(u<EC_BITS / 8>) mask_in;
    _PORT(bool) enable_in;
    _PORT(bool) ready_out = _ASSIGN(enable_in() && (!v || response_ready_in()));
    _PORT(bool) response_out = _ASSIGN(v != 0);
    _PORT(u<32>) response_tag_out = _ASSIGN(t);
    _PORT(logic<EC_BITS>) response_data_out = _ASSIGN(d);
    _PORT(bool) response_error_out = _ASSIGN(e != 0);

private:
    memory<u8, EC_BITS / 8, EC_BANK_WORDS> words;
    reg<u1> v, e;
    reg<u<32>> t;
    reg<logic<EC_BITS>> d;

public:
    void _assign() {}

    void _work(bool reset) {
        unsigned i;
        logic<EC_BITS> mask;
        if (reset) {
            v._next = 0;
            e._next = 0;
            t._next = 0;
            d._next = 0;
        } else {
            if (response_ready_in()) {
                v._next = 0;
            }
            if (request_in() && ready_out()) {
                v._next = 1;
                t._next = tag_in();
                d._next = 0;
                e._next = (address_in() % (EC_BITS / 8)) != 0 ||
                          address_in() / (EC_BITS / 8) >= EC_BANK_WORDS;
                if ((address_in() % (EC_BITS / 8)) == 0 &&
                    address_in() / (EC_BITS / 8) < EC_BANK_WORDS) {
                    if (write_in()) {
                        mask = 0;
                        for (i = 0; i < EC_BITS / 8; ++i) {
                            mask.bits(i * 8 + 7, i * 8) = ((mask_in() >> i) & 1) ? 255 : 0;
                        }
                        words[(uint32_t)address_in() / (EC_BITS / 8)] =
                            ((logic<EC_BITS>)words[(uint32_t)address_in() / (EC_BITS / 8)] &
                             ~mask) |
                            (data_in() & mask);
                    } else {
                        d._next = (logic<EC_BITS>)words[(uint32_t)address_in() / (EC_BITS / 8)];
                    }
                }
            }
        }
    }

    void _strobe() {
        words.apply();
        v.strobe();
        e.strobe();
        t.strobe();
        d.strobe();
    }
};
