#include "receipt_budget.h"
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
    record<<"]}}";return record.str();
}
int main() {
    try {
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
