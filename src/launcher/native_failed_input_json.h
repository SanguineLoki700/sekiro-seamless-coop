#pragma once
#include "skcoop/launch_protocol.h"
#include <array>
#include <sstream>
#include <string>

namespace skcoop::launcher {
// Diagnostics only. Published epochs bind samples to the immutable first
// failure; missing stages must never become invented zero observations.
inline std::string native_failed_input_json(LONG failure_kind,LONG64 failure_epoch,
                                            const launch::NativeFailedInputDiagnostic& sample) {
    const auto read32=[](const volatile LONG& value){return InterlockedCompareExchange(const_cast<volatile LONG*>(&value),0,0);};
    const auto read64=[](const volatile LONG64& value){return InterlockedCompareExchange64(const_cast<volatile LONG64*>(&value),0,0);};
    const auto epoch=read64(sample.epoch);
    if(!failure_kind||failure_epoch<=0||epoch!=failure_epoch)return "null";
    const auto flags=read32(sample.flags);
    if((flags&~launch::native_failed_input_known_flags)||read32(sample.padding))return "null";
    std::array<LONG,4> intent{},after_pad{},at_consumer{};
    for(unsigned i=0;i<4;++i){intent[i]=read32(sample.intent_micro[i]);after_pad[i]=read32(sample.after_pad_micro[i]);at_consumer[i]=read32(sample.at_consumer_micro[i]);}
    if(read64(sample.epoch)!=epoch)return "null";
    std::ostringstream value;
    const auto vector=[&](const auto& lanes){value<<'[';for(unsigned i=0;i<4;++i){if(i)value<<',';value<<lanes[i];}value<<']';};
    value<<"{\"epoch\":"<<epoch<<",\"flags\":"<<flags<<",\"intent_micro\":";
    if(flags&launch::native_failed_input_pad_valid)vector(intent);else value<<"null";
    value<<",\"after_pad_micro\":";
    if(flags&launch::native_failed_input_pad_valid)vector(after_pad);else value<<"null";
    value<<",\"at_consumer_micro\":";
    if(flags&launch::native_failed_input_consumer_valid)vector(at_consumer);else value<<"null";
    value<<'}';return value.str();
}
}
