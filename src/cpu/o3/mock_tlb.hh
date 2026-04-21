/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockTLB - Mock TLB for capturing and comparing LSQ TLB translation calls
 *
 * This class provides a mock TLB interface to capture calls from both
 * Gem5 C++ LSQUnit and PyMTL3 LSQUnitCL for behavior comparison.
 */

#ifndef __CPU_O3_MOCK_TLB_HH__
#define __CPU_O3_MOCK_TLB_HH__

#include <cstdint>
#include <vector>
#include <string>
#include <queue>

#include "base/types.hh"

namespace gem5
{

namespace o3
{

/**
 * Record of a TLB call
 */
struct TLBCallRecord
{
    uint64_t cycle;           // Cycle when the call was made
    Addr vaddr;               // Virtual address
    Addr paddr;               // Physical address (for response)
    uint32_t size;            // Access size
    bool isLoad;              // true for load, false for store
    uint64_t seqNum;         // Instruction sequence number
    int fault;                // Fault status (0 = NoFault)
    std::string methodName;   // Method name ("translateReq", "translateResp")

    TLBCallRecord()
        : cycle(0), vaddr(0), paddr(0), size(0), isLoad(false),
          seqNum(0), fault(0), methodName("")
    {}
};

/**
 * Mock TLB for capturing LSQ TLB translation calls
 */
class MockTLBPort
{
  public:
    MockTLBPort();
    ~MockTLBPort();

    /**
     * Record a TLB request from C++ LSQUnit internal (via executeStore/executeLoad)
     * This is used to capture TLB requests initiated internally by C++ LSQUnit
     * @param cycle Current cycle
     * @param vaddr Virtual address
     * @param size Access size
     * @param isLoad True for load, false for store
     * @param seqNum Instruction sequence number
     */
    void recordCPInternalTLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                                bool isLoad, uint64_t seqNum);

    /**
     * Record a TLB response from C++ LSQUnit
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     * @param paddr Physical address
     * @param fault Fault status
     */
    void recordCPTLBResp(uint64_t cycle, uint64_t seqNum,
                         Addr paddr, int fault);

    /**
     * Record a TLB request from PyMTL3 LSQUnitCL
     * @param cycle Current cycle
     * @param vaddr Virtual address
     * @param size Access size
     * @param isLoad True for load, false for store
     * @param seqNum Instruction sequence number
     */
    void recordPyMTL3TLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                             bool isLoad, uint64_t seqNum);

    /**
     * Record a TLB response from PyMTL3 LSQUnitCL
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     * @param paddr Physical address
     * @param fault Fault status
     */
    void recordPyMTL3TLBResp(uint64_t cycle, uint64_t seqNum,
                             Addr paddr, int fault);

    /**
     * Get the next recorded C++ TLB response
     */
    bool getNextCPTLBResp(TLBCallRecord& record);

    /**
     * Get the next recorded C++ internal TLB request (from executeStore/executeLoad)
     */
    bool getNextCPInternalTLBReq(TLBCallRecord& record);

    /**
     * Get the next recorded PyMTL3 TLB request
     */
    bool getNextPyMTL3TLBReq(TLBCallRecord& record);

    /**
     * Get the next recorded PyMTL3 TLB response
     */
    bool getNextPyMTL3TLBResp(TLBCallRecord& record);

    /**
     * Check if there are pending C++ TLB responses
     */
    bool hasPendingCPTLBResps() const;

    /**
     * Check if there are pending C++ internal TLB requests
     */
    bool hasPendingCPInternalTLBReqs() const;

    /**
     * Check if there are pending PyMTL3 TLB requests
     */
    bool hasPendingPyMTL3TLBReqs() const;

    /**
     * Check if there are pending PyMTL3 TLB responses
     */
    bool hasPendingPyMTL3TLBResps() const;

    /**
     * Check if there is a PyMTL3 TLB request with specific seqNum
     */
    bool hasPyMTL3TLBReqWithSeqNum(uint64_t seqNum) const;

    /**
     * Clear all recorded calls
     */
    void clear();

    /**
     * Compare two TLB calls and return mismatch reason
     */
    static bool compareCalls(const TLBCallRecord& cppCall,
                            const TLBCallRecord& pymtl3Call,
                            std::string& reason);

    /**
     * Convert call record to string for logging
     */
    static std::string callToString(const TLBCallRecord& call);

  private:
    std::queue<TLBCallRecord> cppTLBResps;         // C++ TLB resps
    std::queue<TLBCallRecord> cppInternalTLBReqs;  // C++ TLB reqs from executeStore/executeLoad
    std::queue<TLBCallRecord> pymtl3TLBReqs;       // PyMTL3 TLB reqs
    std::queue<TLBCallRecord> pymtl3TLBResps;      // PyMTL3 TLB resps
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_MOCK_TLB_HH__