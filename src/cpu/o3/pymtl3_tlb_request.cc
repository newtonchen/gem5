/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * PyTLBRequest - TLB Translation Request for PyMTL3 Integration
 */

#include "cpu/o3/pymtl3_tlb_request.hh"
#include "arch/generic/mmu.hh"
#include <iostream>

namespace gem5
{

namespace o3
{

PyTLBRequest::PyTLBRequest(Addr _vaddr, unsigned _size, bool _isLoad,
                             uint64_t _seq_num, gem5::ThreadContext* _tc, BaseMMU* _mmu,
                             FinishCallback _callback)
    : vaddr(_vaddr),
      size(_size),
      isLoad(_isLoad),
      seqNum(_seq_num),
      translationDone(false),
      translationDelayed(false),
      translationFault(NoFault),
      translatedPaddr(0),
      finishCallback(_callback),
      tc(_tc),
      mmu(_mmu)
{
    // Create the Request object with proper virtual address setup
    // Use the constructor that calls setVirt()
    req = std::make_shared<Request>(vaddr, size, Request::Flags(), 
                                     0,  // requestor_id
                                     0,  // pc
                                     tc ? tc->contextId() : 0);  // context_id
    
    std::cerr << "[PyTLBRequest] Created for sn=" << seqNum 
              << " vaddr=0x" << std::hex << vaddr << std::dec
              << " size=" << size << " isLoad=" << isLoad << std::endl;
}

PyTLBRequest::~PyTLBRequest()
{
    std::cerr << "[PyTLBRequest] Destroyed for sn=" << seqNum << std::endl;
}

void
PyTLBRequest::initiateTranslation()
{
    if (!mmu || !tc) {
        std::cerr << "[PyTLBRequest] ERROR: MMU or TC not available" << std::endl;
        translationDone = true;
        translationFault = std::make_shared<GenericPageTableFault>(vaddr);
        if (finishCallback) {
            finishCallback(seqNum, 0, translationFault, false);
        }
        return;
    }
    
    std::cerr << "[PyTLBRequest] Initiating translation for sn=" << seqNum 
              << " vaddr=0x" << std::hex << vaddr << std::dec << std::endl;
    
    // Call translateTiming - this is the same as LSQRequest::sendFragmentToTranslation
    BaseMMU::Mode mode = isLoad ? BaseMMU::Read : BaseMMU::Write;
    mmu->translateTiming(req, tc, this, mode);
    
    // Note: translateTiming may call markDelayed() or finish() synchronously
    // or asynchronously depending on whether the translation hits in the TLB
}

void
PyTLBRequest::markDelayed()
{
    translationDelayed = true;
    std::cerr << "[PyTLBRequest] Translation delayed for sn=" << seqNum << std::endl;
    
    // Notify Python that translation is delayed
    if (finishCallback) {
        // For delayed translations, we don't have the result yet
        // The finish() callback will be called later
        finishCallback(seqNum, 0, NoFault, true);
    }
}

void
PyTLBRequest::finish(const Fault &fault, const RequestPtr &req,
                     gem5::ThreadContext *tc, BaseMMU::Mode mode)
{
    translationDone = true;
    translationFault = fault;
    
    if (fault == NoFault && req->hasPaddr()) {
        translatedPaddr = req->getPaddr();
    } else {
        translatedPaddr = 0;
    }
    
    std::cerr << "[PyTLBRequest] Translation finished for sn=" << seqNum;
    if (fault == NoFault) {
        std::cerr << " paddr=0x" << std::hex << translatedPaddr << std::dec;
    } else {
        std::cerr << " FAULT";
    }
    std::cerr << " delayed=" << translationDelayed << std::endl;
    
    // Notify Python
    if (finishCallback) {
        finishCallback(seqNum, translatedPaddr, fault, translationDelayed);
    }
}

} // namespace o3
} // namespace gem5
