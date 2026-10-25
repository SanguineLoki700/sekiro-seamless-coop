#include "receipt_budget.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using skcoop::launcher::ReceiptBudget;
void require(bool v) { if(!v)throw std::runtime_error("Receipt reserve invariant failed"); }
int main() {
    try {
        ReceiptBudget budget;
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
        require(budget.permits(ReceiptBudget::record_limit-1)); budget.committed(ReceiptBudget::record_limit-1);
        require(budget.bytes()<ReceiptBudget::maximum);
        require(!budget.permits(ReceiptBudget::record_limit));
        require(!budget.permits(std::numeric_limits<std::size_t>::max()));
        std::cout<<"PASS 30-minute periodic stream retains terminal receipt space\n";
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
