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

    std::cerr << "[MockIQ] C++ Replay recorded: cycle=" << cycle
              << ", sn=" << seqNum << std::endl;
}

void
MockIQPort::recordCPReschedule(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "reschedule";

    cppReschedules.push(record);

    std::cerr << "[MockIQ] C++ Reschedule recorded: cycle=" << cycle
              << ", sn=" << seqNum << std::endl;
}

void
MockIQPort::recordPyMTL3Replay(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "replay";

    pymtl3Replays.push(record);

    std::cerr << "[MockIQ] PyMTL3 Replay recorded: cycle=" << cycle
              << ", sn=" << seqNum << std::endl;
}

void
MockIQPort::recordPyMTL3Reschedule(uint64_t cycle, uint64_t seqNum)
{
    IQCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.methodName = "reschedule";

    pymtl3Reschedules.push(record);

    std::cerr << "[MockIQ] PyMTL3 Reschedule recorded: cycle=" << cycle
              << ", sn=" << seqNum << std::endl;
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