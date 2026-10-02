extern "C" void kernel(){
    __builtin_memcpy(reinterpret_cast<void*>((EC_BANK_WORDS+1024)*EC_BUS_BYTES),
                     reinterpret_cast<const void*>(2048*EC_BUS_BYTES),1024*EC_BUS_BYTES);
}
