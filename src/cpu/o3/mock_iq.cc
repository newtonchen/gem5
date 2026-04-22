/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockIQ implementation
 */

#include "cpu/o3/mock_iq.hh"

#include <iostream>
#include <sstream>

namespace gem5
{

namespace o3
{

MockIQPort::MockIQPort()
{
}

MockIQPort::~MockIQPort()
{
}

void
MockIQPort::recordCPReplay(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "replay";

    cppReplays.push(record);
}

void
MockIQPort::recordCPReschedule(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "reschedule";

    cppReschedules.push(record);
}

void
MockIQPort::recordPyMTL3Replay(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "replay";

    pymtl3Replays.push(record);
}

void
MockIQPort::recordPyMTL3Reschedule(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "reschedule";

    pymtl3Reschedules.push(record);
}

bool
MockIQPort::getNextCPReplay(IQCallRecord& record)
{
    if (cppReplays.empty()) {
        return false;
    }
    record = cppReplays.front();
    cppReplays.pop();
    return true;
}

bool
MockIQPort::getNextCPReschedule(IQCallRecord& record)
{
    if (cppReschedules.empty()) {
        return false;
    }
    record = cppReschedules.front();
    cppReschedules.pop();
    return true;
}

bool
MockIQPort::getNextPyMTL3Replay(IQCallRecord& record)
{
    if (pymtl3Replays.empty()) {
        return false;
    }
    record = pymtl3Replays.front();
    pymtl3Replays.pop();
    return true;
}

bool
MockIQPort::getNextPyMTL3Reschedule(IQCallRecord& record)
{
    if (pymtl3Reschedules.empty()) {
        return false;
    }
    record = pymtl3Reschedules.front();
    pymtl3Reschedules.pop();
    return true;
}

bool
MockIQPort::hasPendingCPReplays() const
{
    return !cppReplays.empty();
}

bool
MockIQPort::hasPendingCPReschedules() const
{
    return !cppReschedules.empty();
}

bool
MockIQPort::hasPendingPyMTL3Replays() const
{
    return !pymtl3Replays.empty();
}

bool
MockIQPort::hasPendingPyMTL3Reschedules() const
{
    return !pymtl3Reschedules.empty();
}

void
MockIQPort::clear()
{
    while (!cppReplays.empty()) cppReplays.pop();
    while (!cppReschedules.empty()) cppReschedules.pop();
    while (!pymtl3Replays.empty()) pymtl3Replays.pop();
    while (!pymtl3Reschedules.empty()) pymtl3Reschedules.pop();
}

bool
MockIQPort::findAndRemoveCPReplayBySeqNum(uint64_t seqNum, IQCallRecord& record)
{
    std::queue<IQCallRecord> tempQueue;
    bool found = false;

    while (!cppReplays.empty()) {
        IQCallRecord current = cppReplays.front();
        cppReplays.pop();

        if (!found && current.seqNum == seqNum) {
            record = current;
            found = true;
        } else {
            tempQueue.push(current);
        }
    }

    while (!tempQueue.empty()) {
        cppReplays.push(tempQueue.front());
        tempQueue.pop();
    }

    return found;
}

bool
MockIQPort::findAndRemoveCPRescheduleBySeqNum(uint64_t seqNum, IQCallRecord& record)
{
    std::queue<IQCallRecord> tempQueue;
    bool found = false;

    while (!cppReschedules.empty()) {
        IQCallRecord current = cppReschedules.front();
        cppReschedules.pop();

        if (!found && current.seqNum == seqNum) {
            record = current;
            found = true;
        } else {
            tempQueue.push(current);
        }
    }

    while (!tempQueue.empty()) {
        cppReschedules.push(tempQueue.front());
        tempQueue.pop();
    }

    return found;
}

int
MockIQPort::removeExpiredReplays(uint64_t currentTick, uint64_t timeoutCycles)
{
    // 假设 500 ticks/cycle
    const uint64_t timeoutTick = currentTick - timeoutCycles * 500;
    int expiredCount = 0;
    std::queue<IQCallRecord> tempQueue;

    while (!cppReplays.empty()) {
        IQCallRecord record = cppReplays.front();
        cppReplays.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockIQ] C++ replay expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    while (!pymtl3Replays.empty()) {
        IQCallRecord record = pymtl3Replays.front();
        pymtl3Replays.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockIQ] PyMTL3 replay expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    cppReplays = std::move(tempQueue);
    return expiredCount;
}

int
MockIQPort::removeExpiredReschedules(uint64_t currentTick, uint64_t timeoutCycles)
{
    // 假设 500 ticks/cycle
    const uint64_t timeoutTick = currentTick - timeoutCycles * 500;
    int expiredCount = 0;
    std::queue<IQCallRecord> tempQueue;

    while (!cppReschedules.empty()) {
        IQCallRecord record = cppReschedules.front();
        cppReschedules.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockIQ] C++ reschedule expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    cppReschedules = std::move(tempQueue);

    while (!pymtl3Reschedules.empty()) {
        IQCallRecord record = pymtl3Reschedules.front();
        pymtl3Reschedules.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockIQ] PyMTL3 reschedule expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    pymtl3Reschedules = std::move(tempQueue);
    return expiredCount;
}

bool
MockIQPort::compareCalls(const IQCallRecord& cppCall,
                        const IQCallRecord& pymtl3Call,
                        std::string& reason)
{
    if (cppCall.methodName != pymtl3Call.methodName) {
        reason = "method name mismatch: C++=" + cppCall.methodName +
                 ", PyMTL3=" + pymtl3Call.methodName;
        return false;
    }

    if (cppCall.seqNum != pymtl3Call.seqNum) {
        reason = "seqNum mismatch: C++=" + std::to_string(cppCall.seqNum) +
                 ", PyMTL3=" + std::to_string(pymtl3Call.seqNum);
        return false;
    }

    return true;
}

std::string
MockIQPort::callToString(const IQCallRecord& call)
{
    std::ostringstream oss;
    oss << "IQ." << call.methodName
        << "(sn=" << call.seqNum << ")";
    return oss.str();
}

} // namespace o3
} // namespace gem5
