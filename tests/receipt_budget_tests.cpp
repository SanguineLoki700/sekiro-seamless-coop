#include "receipt_budget.h"
#include "native_failed_input_json.h"
#include "../include/skcoop/launch_protocol.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
using skcoop::launcher::ReceiptBudget;
void require(bool v) { if(!v)throw std::runtime_error("Receipt reserve invariant failed"); }
std::string conservative_native_trace_record() {
    // Use the longest signed decimal representation for every integer, all
    // 128 slots with both optional observations populated, and 16 KiB of extra
    // JSON data for the remaining counters, process/save fields and envelope.
    // This is a size bound fixture, not claimed native gameplay evidence.
    const auto narrow=std::to_string(std::numeric_limits<LONG>::min());
    const auto wide=std::to_string(std::numeric_limits<LONG64>::min());
    const auto triple='['+narrow+','+narrow+','+narrow+']';
    std::ostringstream record;
    record<<"{\"other_fields_upper_bound\":\""<<std::string(16*1024,'x')<<"\",\"native_controller\":{\"trace\":[";
    for(std::size_t i=0;i<skcoop::launch::native_trace_capacity;++i){
        if(i)record<<',';
        record<<"{\"phase\":"<<narrow<<",\"state_code\":"<<narrow
            <<",\"generation\":"<<wide<<",\"frame\":"<<wide<<",\"elapsed_ms\":"<<wide
            <<",\"observation_flags\":"<<narrow<<",\"position_mm\":"<<triple
            <<",\"resolved_input\":{\"movement_milli\":"<<triple<<",\"held\":"<<narrow<<"}}";
    }
    const auto quadruple='['+narrow+','+narrow+','+narrow+','+narrow+']';
    record<<"],\"failed_input_sample\":{\"epoch\":"<<wide<<",\"flags\":"<<narrow
        <<",\"intent_micro\":"<<quadruple<<",\"after_pad_micro\":"<<quadruple
        <<",\"at_consumer_micro\":"<<quadruple<<"}}}";return record.str();
}
void failed_input_availability() {
    using namespace skcoop::launch;
    NativeFailedInputDiagnostic sample{};sample.epoch=17;
    sample.intent_micro[0]=1;sample.intent_micro[1]=-2;sample.intent_micro[2]=350000;sample.intent_micro[3]=4;
    sample.after_pad_micro[2]=-350000;
    const auto render=[&](LONG kind=15,LONG64 epoch=17){return skcoop::launcher::native_failed_input_json(kind,epoch,sample);};
    require(render(0)=="null"&&render(15,16)=="null");
    sample.flags=native_failed_input_pad_valid|native_failed_input_original_turn_observed|native_failed_input_turn_matched;
    const auto pad=render();
    require(pad.find("\"intent_micro\":[1,-2,350000,4]")!=std::string::npos&&
        pad.find("\"after_pad_micro\":[0,0,-350000,0]")!=std::string::npos&&
        pad.find("\"at_consumer_micro\":null")!=std::string::npos);
    // A separately observed zero consumer is valid; missing Pad data stays null.
    sample.flags=native_failed_input_consumer_valid;
    const auto consumer=render();
    require(consumer.find("\"intent_micro\":null")!=std::string::npos&&consumer.find("\"after_pad_micro\":null")!=std::string::npos&&
        consumer.find("\"at_consumer_micro\":[0,0,0,0]")!=std::string::npos);
    sample.flags=native_failed_input_known_flags|16;require(render()=="null");
    sample.flags=native_failed_input_known_flags;sample.padding=1;require(render()=="null");
    sample.padding=0;sample.epoch=0;require(render()=="null");
}
int main() {
    try {
        failed_input_availability();
        ReceiptBudget budget;
        const auto full_trace=conservative_native_trace_record();
        require(skcoop::launch::native_trace_capacity==128);
        require(full_trace.size()<ReceiptBudget::record_limit&&budget.permits(full_trace.size()));
        require(ReceiptBudget::record_limit<ReceiptBudget::final_reserve);
        // Reproduce a long periodic actor stream; exhausting samples must still
        // permit both the limit marker and a maximum-size terminal receipt.
        for(auto initial:{500u,1200u}) { require(budget.permits(initial)); budget.committed(initial); }
        unsigned accepted{},refused{};
        for(unsigned second=0;second<1800;++second) {
            const std::size_t sample=950+(second%7)*17;
            if(budget.permits(sample,true)) { budget.committed(sample); ++accepted; }
            else ++refused;
        }
        require(accepted>0 && refused>0 && budget.bytes()<ReceiptBudget::maximum-ReceiptBudget::final_reserve);
        require(budget.permits(120)); budget.committed(120);
        require(budget.permits(full_trace.size()));
        require(budget.permits(ReceiptBudget::record_limit-1)); budget.committed(ReceiptBudget::record_limit-1);
        require(budget.bytes()<ReceiptBudget::maximum);
        require(!budget.permits(ReceiptBudget::record_limit));
        require(!budget.permits(std::numeric_limits<std::size_t>::max()));
        std::cout<<"PASS 30-minute periodic stream retains terminal receipt space; 128-slot conservative trace record "
            <<full_trace.size()<<" bytes < "<<ReceiptBudget::record_limit<<" bytes\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
