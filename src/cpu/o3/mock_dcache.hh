/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockDCache - Mock DCache port for capturing and comparing LSQUnit output calls
 *
 * This class provides a mock DCache interface to capture calls from both
 * Gem5 C++ LSQUnit and PyMTL3 LSQUnitCL for behavior comparison.
 */

#ifndef __CPU_O3_MOCK_DCACHE_HH__
#define __CPU_O3_MOCK_DCACHE_HH__

#include <cstdint>
#include <vector>
#include <string>
#include <queue>

#include "base/types.hh"
#include "mem/packet.hh"

namespace gem5
{

namespace o3
{

/**
 * Record of a DCache call
 */
struct DCacheCallRecord
{
    uint64_t cycle;           // Cycle when the call was made
    Addr addr;                // Memory address
    uint32_t size;            // Access size
    bool isWrite;             // true for write, false for read
    std::vector<uint8_t> data; // Data (for writes)
    std::string methodName;   // Method name (e.g., "completeDataAccess")
    uint64_t seqNum;          // Instruction sequence number for matching

    DCacheCallRecord()
        : cycle(0), addr(0), size(0), isWrite(false), methodName(""), seqNum(0)
    {}
};

/**
 * Mock DCache port for capturing LSQUnit output calls
 */
class MockDCachePort
{
  public:
    MockDCachePort();
    ~MockDCachePort();

    /**
     * Record a call from C++ LSQUnit
     * @param cycle Current cycle
     * @param pkt Packet containing call information
     * @param methodName Name of the calling method
     * @param seqNum Instruction sequence number (optional)
     */
    void recordCPCall(uint64_t cycle, PacketPtr pkt,
                     const std::string& methodName,
                     uint64_t seqNum = 0);

    /**
     * Record a call from PyMTL3 LSQUnitCL
     * @param cycle Current cycle
     * @param addr Memory address
     * @param size Access size
     * @param isWrite Write or read
     * @param data Data bytes (for writes)
     * @param methodName Name of the calling method
     * @param seqNum Instruction sequence number (optional)
     */
    void recordPyMTL3Call(uint64_t cycle, Addr addr, uint32_t size,
                         bool isWrite, const std::vector<uint8_t>& data,
                         const std::string& methodName,
                         uint64_t seqNum = 0);

    /**
     * Get the next recorded C++ call
     * @return DCacheCallRecord or nullptr if empty
     */
    bool getNextCPCall(DCacheCallRecord& record);

    /**
     * Get the next recorded PyMTL3 call
     * @return DCacheCallRecord or nullptr if empty
     */
    bool getNextPyMTL3Call(DCacheCallRecord& record);

    /**
     * Check if there are pending C++ calls
     */
    bool hasPendingCPCalls() const;

    /**
     * Check if there are pending PyMTL3 calls
     */
    bool hasPendingPyMTL3Calls() const;

    /**
     * Find and remove a C++ call by seqNum
     * @param seqNum Sequence number to search for
     * @param record Output parameter for the found record
     * @return true if found and removed, false otherwise
     */
    bool findAndRemoveCPCallBySeqNum(uint64_t seqNum, DCacheCallRecord& record);

    /**
     * Remove expired calls from both C++ and PyMTL3 queues
     * @param currentCycle Current simulation cycle
     * @param timeoutCycles Number of cycles before a call is considered expired
     * @return Number of expired calls removed
     */
    int removeExpiredCalls(uint64_t currentCycle, uint64_t timeoutCycles);

    /**
     * Clear all recorded calls
     */
    void clear();

    /**
     * Compare two DCache calls and return mismatch reason
     * @param cppCall C++ call record
     * @param pymtl3Call PyMTL3 call record
     * @param reason Output mismatch reason
     * @return true if calls match, false otherwise
     */
    static bool compareCalls(const DCacheCallRecord& cppCall,
                            const DCacheCallRecord& pymtl3Call,
                            std::string& reason);

    /**
     * Convert call record to string for logging
     */
    static std::string callToString(const DCacheCallRecord& call);

  private:
    std::queue<DCacheCallRecord> cppCalls;
    std::queue<DCacheCallRecord> pymtl3Calls;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_MOCK_DCACHE_HH__
