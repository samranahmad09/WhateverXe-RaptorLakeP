#include "kern_display13_panel_timing.hpp"
#include <assert.h>
#include <stdio.h>
#include <fstream>
#include <iterator>
#include <vector>
using namespace Display13PanelTiming;

// Tiny interpreter for the one changed instruction. This does not execute a
// donor image. It verifies that the patch preserves EBX, including non-A pipes.
static uint32_t selectedBank(const uint8_t *site,uint32_t pipe,uint32_t configWord2) {
    if(configWord2)return pipe;
    if(site[0]==0xBB) {uint32_t value;memcpy(&value,site+1,4);return value;}
    for(unsigned i=0;i<5;++i)assert(site[i]==0x90);
    return pipe;
}
static uint32_t readMN(uint32_t reg) {
    // A valid active DP link on A. The legacy bank is not a live transcoder.
    if(reg==0x60040)return 0xA4D73;
    if(reg==0x60044)return 0x80000;
    return 0;
}
int main(int argc,char **argv) {
    if(argc==2) {
        std::ifstream input(argv[1],std::ios::binary);assert(input);
        const std::vector<uint8_t> donor((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        assert(plan(true,donor.data(),donor.size(),Query)==Plan::Ready);
        puts("Local donor preflight PASS (read only)");
    }
    std::vector<uint8_t> image(0x4A024,0xCC);
    for(const auto &g:Guards)memcpy(image.data()+g.offset,g.bytes,g.length);
    const auto original=image;
    assert(plan(false,nullptr,0,0)==Plan::NotRequired);
    assert(plan(false,image.data(),image.size(),Query)==Plan::NotRequired && image==original);
    assert(plan(true,nullptr,image.size(),Query)==Plan::Rejected);
    assert(plan(true,image.data(),image.size(),Query+1)==Plan::Rejected);
    for(const auto &g:Guards) {
        assert(plan(true,image.data(),g.offset,Query)==Plan::Rejected);
        assert(plan(true,image.data(),g.offset+g.length-1,Query)==Plan::Rejected);
        // Every changed guard byte rejects the image before patch application.
        for(size_t i=0;i<g.length;++i) {
            image[g.offset+i]^=1;
            assert(plan(true,image.data(),image.size(),Query)==Plan::Rejected);
            image[g.offset+i]^=1;
        }
    }
    assert(plan(true,image.data(),image.size(),Query)==Plan::Ready && image==original);
    assert(!memcmp(image.data()+Selection,Before,sizeof(Before)));
    auto bank=selectedBank(image.data()+Selection,0,0);
    assert(bank==15 && !readMN(0x60044+(bank<<12))); // Original divisor is zero.
    memcpy(image.data()+Selection,After,sizeof(After));
    assert(plan(true,image.data(),image.size(),Query)==Plan::Rejected); // No double application.
    for(unsigned pipe=0;pipe<4;++pipe)for(unsigned word=0;word<3;++word) {
        assert(selectedBank(image.data()+Selection,pipe,word)==pipe);
        if(word)assert(selectedBank(original.data()+Selection,pipe,word)==pipe);
    }
    bank=selectedBank(image.data()+Selection,0,0);
    const auto m=readMN(0x60040+(bank<<12)),n=readMN(0x60044+(bank<<12));
    assert(n && uint64_t(m)*270000000/n==347710590);
    for(size_t i=0;i<image.size();++i)
        if(i<Selection || i>=Selection+sizeof(After))assert(image[i]==original[i]);
    puts("Display13 panel query: exact guards, real transcoder, non-panel preservation PASS");
}
