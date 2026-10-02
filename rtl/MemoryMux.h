#pragma once
#include "Config.h"

// Independent registered request lanes per bank and answer lanes per client.
// High tag bits identify a client; low 8 bits identify its FIFO slot.
class MemoryMux : public Module {
public:
    _PORT(bool) request_in[2 * EC_CORES], write_in[2 * EC_CORES];
    _PORT(u<32>) address_in[2 * EC_CORES], tag_in[2 * EC_CORES];
    _PORT(u<EC_BITS / 8>) mask_in[2 * EC_CORES], mem_mask_out[EC_BANKS];
    _PORT(logic<EC_BITS>) data_in[2 * EC_CORES];
    _PORT(bool)
    accepted_out[2 * EC_CORES], response_out[2 * EC_CORES], response_error_out[2 * EC_CORES];
    _PORT(u<32>) response_tag_out[2 * EC_CORES];
    _PORT(logic<EC_BITS>) response_data_out[2 * EC_CORES];
    _PORT(bool)
    mem_request_out[EC_BANKS], mem_write_out[EC_BANKS], mem_response_ready_out[EC_BANKS];
    _PORT(u<32>) mem_address_out[EC_BANKS], mem_tag_out[EC_BANKS];
    _PORT(logic<EC_BITS>) mem_data_out[EC_BANKS];
    _PORT(bool) mem_ready_in[EC_BANKS], mem_response_in[EC_BANKS], mem_error_in[EC_BANKS];
    _PORT(u<32>) mem_response_tag_in[EC_BANKS];
    _PORT(logic<EC_BITS>) mem_response_data_in[EC_BANKS];

private:
    // The extra request lane returns errors for unmapped addresses.
    reg<u1> qv[EC_BANKS + 1], qw[EC_BANKS + 1];
    reg<u<32>> qa[EC_BANKS + 1], qt[EC_BANKS + 1], rr[EC_BANKS + 1];
    reg<u<EC_BITS / 8>> qm[EC_BANKS + 1];
    reg<logic<EC_BITS>> qd[EC_BANKS + 1];
    reg<u1> rv[2 * EC_CORES], re[2 * EC_CORES];
    reg<u<32>> rt[2 * EC_CORES], br[2 * EC_CORES];
    reg<logic<EC_BITS>> rd[2 * EC_CORES];
    u<32> grant_comb[EC_BANKS + 1], answer_comb[2 * EC_CORES];
    bool space_comb[EC_BANKS + 1], accept_comb[2 * EC_CORES], take_comb[EC_BANKS + 1];
    bool response_arbitration_comb;

    bool& response_arbitration_comb_func() {
        unsigned bank, client, n, candidate;
        for (bank = 0; bank <= EC_BANKS; ++bank) {
            take_comb[bank] = false;
        }
        // A FIFO can absorb one answer per cycle. Arbitrate bank collisions
        // separately for each FIFO, allowing loads and stores to complete together.
        for (client = 0; client < 2 * EC_CORES; ++client) {
            answer_comb[client] = EC_BANKS + 1;
            for (n = 0; n <= EC_BANKS; ++n) {
                candidate = ((uint32_t)br[client] + n) % (EC_BANKS + 1);
                if (answer_comb[client] == EC_BANKS + 1) {
                    if (candidate < EC_BANKS) {
                        if (mem_response_in[candidate]() &&
                            (mem_response_tag_in[candidate]() >> 8) == client) {
                            answer_comb[client] = candidate;
                        }
                    } else if (qv[EC_BANKS] && (qt[EC_BANKS] >> 8) == client) {
                        answer_comb[client] = candidate;
                    }
                }
            }
            if (answer_comb[client] <= EC_BANKS) {
                take_comb[(uint32_t)answer_comb[client]] = true;
            }
        }
        response_arbitration_comb = true;
        return response_arbitration_comb;
    }

    bool arbitration_comb;

    bool& arbitration_comb_func() {
        unsigned bank, client, n, region;
        bool evaluated;
        evaluated = response_arbitration_comb_func();
        for (client = 0; client < 2 * EC_CORES; ++client) {
            accept_comb[client] = false;
        }
        for (bank = 0; bank <= EC_BANKS; ++bank) {
            if (bank < EC_BANKS) {
                space_comb[bank] = !qv[bank] || mem_ready_in[bank]();
            } else {
                space_comb[bank] = !qv[bank] || take_comb[bank];
            }
            grant_comb[bank] = 2 * EC_CORES;
            for (n = 0; n < 2 * EC_CORES; ++n) {
                client = ((uint32_t)rr[bank] + n) % (2 * EC_CORES);
                region = (uint32_t)address_in[client]() / (EC_BANK_WORDS * (EC_BITS / 8));
                if (region >= EC_BANKS) {
                    region = EC_BANKS;
                }
                if (grant_comb[bank] == 2 * EC_CORES && request_in[client]() && region == bank) {
                    grant_comb[bank] = client;
                }
            }
            if (space_comb[bank] && grant_comb[bank] < 2 * EC_CORES) {
                accept_comb[(uint32_t)grant_comb[bank]] = true;
            }
        }
        arbitration_comb = true;
        return arbitration_comb;
    }

public:
    void _assign() {
        unsigned i;
        for (i = 0; i < 2 * EC_CORES; ++i) {
            accepted_out[i] = _ASSIGN_I(arbitration_comb_func() && accept_comb[i]);
            response_out[i] = _ASSIGN_I(rv[i] != 0);
            response_error_out[i] = _ASSIGN_I(re[i] != 0);
            response_tag_out[i] = _ASSIGN_I(rt[i] & 255);
            response_data_out[i] = _ASSIGN_I(rd[i]);
        }
        for (i = 0; i < EC_BANKS; ++i) {
            mem_request_out[i] = _ASSIGN_I(qv[i] != 0);
            mem_write_out[i] = _ASSIGN_I(qw[i] != 0);
            mem_address_out[i] = _ASSIGN_I(qa[i]);
            mem_tag_out[i] = _ASSIGN_I(qt[i]);
            mem_data_out[i] = _ASSIGN_I(qd[i]);
            mem_mask_out[i] = _ASSIGN_I(qm[i]);
            mem_response_ready_out[i] = _ASSIGN_I(response_arbitration_comb_func() && take_comb[i]);
        }
    }

    void _work(bool reset) {
        unsigned i, g, a;
        bool evaluated;
        if (reset) {
            for (i = 0; i <= EC_BANKS; ++i) {
                qv[i]._next = 0;
                qw[i]._next = 0;
                qa[i]._next = 0;
                qm[i]._next = 0;
                qt[i]._next = 0;
                qd[i]._next = 0;
                rr[i]._next = 0;
            }
            for (i = 0; i < 2 * EC_CORES; ++i) {
                rv[i]._next = 0;
                re[i]._next = 0;
                rt[i]._next = 0;
                rd[i]._next = 0;
                br[i]._next = 0;
            }
        } else {
            evaluated = arbitration_comb_func();
            for (i = 0; i < 2 * EC_CORES; ++i) {
                a = (uint32_t)answer_comb[i];
                rv[i]._next = 0;
                if (a <= EC_BANKS) {
                    rv[i]._next = 1;
                    br[i]._next = (a + 1) % (EC_BANKS + 1);
                    if (a < EC_BANKS) {
                        rt[i]._next = mem_response_tag_in[a]();
                        rd[i]._next = mem_response_data_in[a]();
                        re[i]._next = mem_error_in[a]();
                    } else {
                        rt[i]._next = qt[EC_BANKS];
                        rd[i]._next = 0;
                        re[i]._next = 1;
                    }
                }
            }
            for (i = 0; i <= EC_BANKS; ++i) {
                g = (uint32_t)grant_comb[i];
                if (space_comb[i]) {
                    qv[i]._next = 0;
                    if (g < 2 * EC_CORES) {
                        qm[i]._next = mask_in[g]();
                        qv[i]._next = 1;
                        qw[i]._next = write_in[g]();
                        qd[i]._next = data_in[g]();
                        qt[i]._next = (g << 8) | (tag_in[g]() & 255);
                        qa[i]._next = address_in[g]() % (EC_BANK_WORDS * (EC_BITS / 8));
                        rr[i]._next = (g + 1) % (2 * EC_CORES);
                    }
                }
            }
        }
    }

    void _strobe() {
        unsigned i;
        for (i = 0; i <= EC_BANKS; ++i) {
            qm[i].strobe();
            qv[i].strobe();
            qw[i].strobe();
            qa[i].strobe();
            qt[i].strobe();
            qd[i].strobe();
            rr[i].strobe();
        }
        for (i = 0; i < 2 * EC_CORES; ++i) {
            rv[i].strobe();
            re[i].strobe();
            rt[i].strobe();
            rd[i].strobe();
            br[i].strobe();
        }
    }
};
