/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * PyTLBRequest - TLB Translation Request for PyMTL3 Integration
 *
 * This class provides a C++ TLB translation request that can be used
 * from Python via pybind11. It follows the same pattern as LSQRequest
 * in the main Gem5 code.
 */

#ifndef __CPU_O3_PYMTL3_TLB_REQUEST_HH__
#define __CPU_O3_PYMTL3_TLB_REQUEST_HH__

#include "arch/generic/mmu.hh"
#include "cpu/o3/dyn_inst.hh"
#include "mem/request.hh"
#include "cpu/thread_context.hh"
#include <functional>
#include <iostream>

namespace gem5
{

namespace o3
{

/**
 * PyTLBRequest - A TLB translation request for PyMTL3
 *
 * This class wraps a TLB translation request and provides callbacks
 * for when the translation completes. It inherits from BaseMMU::Translation
 * to work with the MMU's translateTiming() method.
 */
class PyTLBRequest : public BaseMMU::Translation
{
  public:
    // Callback type for when translation finishes
    using FinishCallback = std::function<void(uint64_t seq_num, Addr paddr, 
                                               Fault fault, bool delayed)>;

  private:
    // Request parameters
    Addr vaddr;
    unsigned size;
    bool isLoad;
    uint64_t seqNum;
    
    // State
    RequestPtr req;
    bool translationDone;
    bool translationDelayed;
    Fault translationFault;
    Addr translatedPaddr;
    
    // Callback to Python
    FinishCallback finishCallback;
    
    // ThreadContext for translation
    gem5::ThreadContext* tc;
    
    // MMU pointer
    BaseMMU* mmu;

  public:
    /**
     * Constructor
     */
    PyTLBRequest(Addr _vaddr, unsigned _size, bool _isLoad, 
                 uint64_t _seq_num, gem5::ThreadContext* _tc, BaseMMU* _mmu,
                 FinishCallback _callback);
    
    /**
     * Destructor
     */
    ~PyTLBRequest();
    
    /**
     * Initiate the TLB translation
     * This calls mmu->translateTiming() to start the translation
     */
    void initiateTranslation();
    
    /**
     * Check if translation is complete
     */
    bool isTranslationDone() const { return translationDone; }
    
    /**
     * Check if translation was delayed (page table walk)
     */
    bool isTranslationDelayed() const { return translationDelayed; }
    
    /**
     * Get the translation fault
     */
    Fault getFault() const { return translationFault; }
    
    /**
     * Get the translated physical address
     */
    Addr getPaddr() const { return translatedPaddr; }
    
    /**
     * Get the sequence number
     */
    uint64_t getSeqNum() const { return seqNum; }

    // BaseMMU::Translation interface
    /**
     * Called by the MMU when translation is delayed (e.g., page table walk)
     */
    void markDelayed() override;
    
    /**
     * Called by the MMU when translation completes
     */
    void finish(const Fault &fault, const RequestPtr &req,
                gem5::ThreadContext *tc, BaseMMU::Mode mode) override;
    
    /**
     * Check if this translation has been squashed
     * Always returns false for PyMTL3 translations
     */
    bool squashed() const override { return false; }
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_PYMTL3_TLB_REQUEST_HH__
