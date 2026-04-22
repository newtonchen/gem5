/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockIQ - Mock Instruction Queue for capturing IQ-related calls
 *
 * This class provides a mock IQ interface to capture calls from both
 * Gem5 C++ LSQUnit and PyMTL3 LSQUnitCL for behavior comparison.
 * Specifically for replay_inst and rescheduling_inst interfaces.
 */

#ifndef __CPU_O3_MOCK_IQ_HH__
#define __CPU_O3_MOCK_IQ_HH__

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
 * Record of an IQ call
 */
struct IQCallRecord
{
    uint64_t cycle;           // Cycle when the call was made
    uint64_t seqNum;          // Instruction sequence number
    std::string methodName;   // Method name ("replay", "reschedule")

    IQCallRecord()
        : cycle(0), seqNum(0), methodName("")
    {}
};

/**
 * Mock IQ for capturing LSQ replay and reschedule calls
 */
class MockIQPort
{
  public:
    MockIQPort();
    ~MockIQPort();

    /**
     * Record a replay call from C++ LSQUnit
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     */
    void recordCPReplay(uint64_t cycle, uint64_t seqNum);

    /**
     * Record a reschedule call from C++ LSQUnit
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     */
    void recordCPReschedule(uint64_t cycle, uint64_t seqNum);

    /**
     * Record a replay call from PyMTL3 LSQUnitCL
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     */
    void recordPyMTL3Replay(uint64_t cycle, uint64_t seqNum);

    /**
     * Record a reschedule call from PyMTL3 LSQUnitCL
     * @param cycle Current cycle
     * @param seqNum Instruction sequence number
     */
    void recordPyMTL3Reschedule(uint64_t cycle, uint64_t seqNum);

    /**
     * Get the next recorded C++ replay call
     */
    bool getNextCPReplay(IQCallRecord& record);

    /**
     * Get the next recorded C++ reschedule call
     */
    bool getNextCPReschedule(IQCallRecord& record);

    /**
     * Get the next recorded PyMTL3 replay call
     */
    bool getNextPyMTL3Replay(IQCallRecord& record);

    /**
     * Get the next recorded PyMTL3 reschedule call
     */
    bool getNextPyMTL3Reschedule(IQCallRecord& record);

    /**
     * Check if there are pending C++ replay calls
     */
    bool hasPendingCPReplays() const;

    /**
     * Check if there are pending C++ reschedule calls
     */
    bool hasPendingCPReschedules() const;

    /**
     * Check if there are pending PyMTL3 replay calls
     */
    bool hasPendingPyMTL3Replays() const;

    /**
     * Check if there are pending PyMTL3 reschedule calls
     */
    bool hasPendingPyMTL3Reschedules() const;

    /**
     * Clear all recorded calls
     */
    void clear();

    /**
     * Find and remove a C++ replay call by seqNum.
     * Used for PyMTL3-first matching mechanism.
     * @param seqNum Sequence number to search for
     * @param record Output parameter for the found record
     * @return true if found and removed, false otherwise
     */
    bool findAndRemoveCPReplayBySeqNum(uint64_t seqNum, IQCallRecord& record);

    /**
     * Find and remove a C++ reschedule call by seqNum.
     * Used for PyMTL3-first matching mechanism.
     * @param seqNum Sequence number to search for
     * @param record Output parameter for the found record
     * @return true if found and removed, false otherwise
     */
    bool findAndRemoveCPRescheduleBySeqNum(uint64_t seqNum, IQCallRecord& record);

    /**
     * Remove expired replay calls (older than timeout).
     * @param currentTick Current tick
     * @param timeoutCycles Timeout in cycles
     * @return Number of expired records removed
     */
    int removeExpiredReplays(uint64_t currentTick, uint64_t timeoutCycles);

    /**
     * Remove expired reschedule calls (older than timeout).
     * @param currentTick Current tick
     * @param timeoutCycles Timeout in cycles
     * @return Number of expired records removed
     */
    int removeExpiredReschedules(uint64_t currentTick, uint64_t timeoutCycles);

    /**
     * Compare two IQ calls and return mismatch reason
     */
    static bool compareCalls(const IQCallRecord& cppCall,
                            const IQCallRecord& pymtl3Call,
                            std::string& reason);

    /**
     * Convert call record to string for logging
     */
    static std::string callToString(const IQCallRecord& call);

  private:
    std::queue<IQCallRecord> cppReplays;
    std::queue<IQCallRecord> cppReschedules;
    std::queue<IQCallRecord> pymtl3Replays;
    std::queue<IQCallRecord> pymtl3Reschedules;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_MOCK_IQ_HH__