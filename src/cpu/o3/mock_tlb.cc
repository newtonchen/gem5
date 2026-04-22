/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockTLB implementation
 */

#include "cpu/o3/mock_tlb.hh"

#include <iostream>
#include <sstream>

namespace gem5
{

namespace o3
{

MockTLBPort::MockTLBPort()
{
}

MockTLBPort::~MockTLBPort()
{
}

void
MockTLBPort::recordCPTLBResp(uint64_t cycle, uint64_t seqNum,
                             Addr paddr, int fault)
{
    TLBCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.paddr = paddr;
    record.fault = fault;
    record.methodName = "translateResp";

    cppTLBResps.push(record);
}

void
MockTLBPort::recordCPInternalTLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                                    bool isLoad, uint64_t seqNum)
{
    TLBCallRecord record;
    record.cycle = cycle;
    record.vaddr = vaddr;
    record.size = size;
    record.isLoad = isLoad;
    record.seqNum = seqNum;
    record.methodName = "translateReq";

    cppInternalTLBReqs.push(record);
}

void
MockTLBPort::recordPyMTL3TLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                                 bool isLoad, uint64_t seqNum)
{
    TLBCallRecord record;
    record.cycle = cycle;
    record.vaddr = vaddr;
    record.size = size;
    record.isLoad = isLoad;
    record.seqNum = seqNum;
    record.methodName = "translateReq";

    pymtl3TLBReqs.push(record);
}

void
MockTLBPort::recordPyMTL3TLBResp(uint64_t cycle, uint64_t seqNum,
                                  Addr paddr, int fault)
{
    TLBCallRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.paddr = paddr;
    record.fault = fault;
    record.methodName = "translateResp";

    pymtl3TLBResps.push(record);
}

bool
MockTLBPort::getNextCPTLBResp(TLBCallRecord& record)
{
    if (cppTLBResps.empty()) {
        return false;
    }
    record = cppTLBResps.front();
    cppTLBResps.pop();
    return true;
}

bool
MockTLBPort::getNextCPInternalTLBReq(TLBCallRecord& record)
{
    if (cppInternalTLBReqs.empty()) {
        return false;
    }
    record = cppInternalTLBReqs.front();
    cppInternalTLBReqs.pop();
    return true;
}

bool
MockTLBPort::getNextPyMTL3TLBReq(TLBCallRecord& record)
{
    if (pymtl3TLBReqs.empty()) {
        return false;
    }
    record = pymtl3TLBReqs.front();
    pymtl3TLBReqs.pop();
    return true;
}

bool
MockTLBPort::getNextPyMTL3TLBResp(TLBCallRecord& record)
{
    if (pymtl3TLBResps.empty()) {
        return false;
    }
    record = pymtl3TLBResps.front();
    pymtl3TLBResps.pop();
    return true;
}

bool
MockTLBPort::hasPendingCPTLBResps() const
{
    return !cppTLBResps.empty();
}

bool
MockTLBPort::hasPendingCPInternalTLBReqs() const
{
    return !cppInternalTLBReqs.empty();
}

bool
MockTLBPort::hasPendingPyMTL3TLBReqs() const
{
    return !pymtl3TLBReqs.empty();
}

bool
MockTLBPort::hasPendingPyMTL3TLBResps() const
{
    return !pymtl3TLBResps.empty();
}

bool
MockTLBPort::hasPyMTL3TLBReqWithSeqNum(uint64_t seqNum) const
{
    std::queue<TLBCallRecord> tempQueue = pymtl3TLBReqs;
    while (!tempQueue.empty()) {
        if (tempQueue.front().seqNum == seqNum) {
            return true;
        }
        tempQueue.pop();
    }
    return false;
}

void
MockTLBPort::clear()
{
    while (!cppTLBResps.empty()) cppTLBResps.pop();
    while (!cppInternalTLBReqs.empty()) cppInternalTLBReqs.pop();
    while (!pymtl3TLBReqs.empty()) pymtl3TLBReqs.pop();
    while (!pymtl3TLBResps.empty()) pymtl3TLBResps.pop();
}

bool
MockTLBPort::findAndRemoveCPInternalTLBReqBySeqNum(uint64_t seqNum, TLBCallRecord& record)
{
    std::queue<TLBCallRecord> tempQueue;
    bool found = false;

    while (!cppInternalTLBReqs.empty()) {
        TLBCallRecord current = cppInternalTLBReqs.front();
        cppInternalTLBReqs.pop();

        if (!found && current.seqNum == seqNum) {
            record = current;
            found = true;
        } else {
            tempQueue.push(current);
        }
    }

    while (!tempQueue.empty()) {
        cppInternalTLBReqs.push(tempQueue.front());
        tempQueue.pop();
    }

    return found;
}

int
MockTLBPort::removeExpiredCPInternalTLBReqs(uint64_t currentTick, uint64_t timeoutCycles)
{
    // 假设 500 ticks/cycle
    const uint64_t timeoutTick = currentTick - timeoutCycles * 500;
    int expiredCount = 0;
    std::queue<TLBCallRecord> tempQueue;

    while (!cppInternalTLBReqs.empty()) {
        TLBCallRecord record = cppInternalTLBReqs.front();
        cppInternalTLBReqs.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockTLB] C++ internal TLB req expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    cppInternalTLBReqs = std::move(tempQueue);
    return expiredCount;
}

int
MockTLBPort::removeExpiredPyMTL3TLBReqs(uint64_t currentTick, uint64_t timeoutCycles)
{
    // 假设 500 ticks/cycle
    const uint64_t timeoutTick = currentTick - timeoutCycles * 500;
    int expiredCount = 0;
    std::queue<TLBCallRecord> tempQueue;

    while (!pymtl3TLBReqs.empty()) {
        TLBCallRecord record = pymtl3TLBReqs.front();
        pymtl3TLBReqs.pop();
        if (record.cycle < timeoutTick) {
            expiredCount++;
            std::cout << "[MockTLB] PyMTL3 TLB req expired: sn=" << record.seqNum << std::endl;
        } else {
            tempQueue.push(record);
        }
    }

    pymtl3TLBReqs = std::move(tempQueue);
    return expiredCount;
}

bool
MockTLBPort::compareCalls(const TLBCallRecord& cppCall,
                         const TLBCallRecord& pymtl3Call,
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

    if (cppCall.methodName == "translateReq") {
        if (cppCall.vaddr != pymtl3Call.vaddr) {
            reason = "vaddr mismatch: C++=0x" + std::to_string(cppCall.vaddr) +
                     ", PyMTL3=0x" + std::to_string(pymtl3Call.vaddr);
            return false;
        }
        if (cppCall.size != pymtl3Call.size) {
            reason = "size mismatch: C++=" + std::to_string(cppCall.size) +
                     ", PyMTL3=" + std::to_string(pymtl3Call.size);
            return false;
        }
        if (cppCall.isLoad != pymtl3Call.isLoad) {
            reason = "isLoad mismatch: C++=" + std::to_string(cppCall.isLoad) +
                     ", PyMTL3=" + std::to_string(pymtl3Call.isLoad);
            return false;
        }
    }
    // Note: translateResp comparison is skipped because PyMTL3's TLB resp
    // is driven by C++, so they should always match.

    return true;
}

std::string
MockTLBPort::callToString(const TLBCallRecord& call)
{
    std::ostringstream oss;
    oss << "TLB." << call.methodName
        << "(sn=" << call.seqNum;
    if (call.methodName == "translateReq") {
        oss << ", vaddr=0x" << std::hex << call.vaddr << std::dec
            << ", size=" << call.size
            << ", isLoad=" << call.isLoad;
    } else if (call.methodName == "translateResp") {
        oss << ", paddr=0x" << std::hex << call.paddr << std::dec
            << ", fault=" << call.fault;
    }
    oss << ")";
    return oss.str();
}

} // namespace o3
} // namespace gem5
