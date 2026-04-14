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
MockTLBPort::recordCPTLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                           bool isLoad, uint64_t seqNum)
{
    TLBCallRecord record;
    record.cycle = cycle;
    record.vaddr = vaddr;
    record.size = size;
    record.isLoad = isLoad;
    record.seqNum = seqNum;
    record.methodName = "translateReq";

    cppTLBReqs.push(record);

    std::cerr << "[MockTLB] C++ TLB Req recorded: cycle=" << cycle
              << ", vaddr=0x" << std::hex << vaddr << std::dec
              << ", size=" << size << ", isLoad=" << isLoad
              << ", sn=" << seqNum << std::endl;
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

    std::cerr << "[MockTLB] C++ TLB Resp recorded: cycle=" << cycle
              << ", sn=" << seqNum
              << ", paddr=0x" << std::hex << paddr << std::dec
              << ", fault=" << fault << std::endl;
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

    std::cerr << "[MockTLB] PyMTL3 TLB Req recorded: cycle=" << cycle
              << ", vaddr=0x" << std::hex << vaddr << std::dec
              << ", size=" << size << ", isLoad=" << isLoad
              << ", sn=" << seqNum << std::endl;
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

    std::cerr << "[MockTLB] PyMTL3 TLB Resp recorded: cycle=" << cycle
              << ", sn=" << seqNum
              << ", paddr=0x" << std::hex << paddr << std::dec
              << ", fault=" << fault << std::endl;
}

bool
MockTLBPort::getNextCPTLBReq(TLBCallRecord& record)
{
    if (cppTLBReqs.empty()) {
        return false;
    }
    record = cppTLBReqs.front();
    cppTLBReqs.pop();
    return true;
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
MockTLBPort::hasPendingCPTLBReqs() const
{
    return !cppTLBReqs.empty();
}

bool
MockTLBPort::hasPendingCPTLBResps() const
{
    return !cppTLBResps.empty();
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

void
MockTLBPort::clear()
{
    while (!cppTLBReqs.empty()) cppTLBReqs.pop();
    while (!cppTLBResps.empty()) cppTLBResps.pop();
    while (!pymtl3TLBReqs.empty()) pymtl3TLBReqs.pop();
    while (!pymtl3TLBResps.empty()) pymtl3TLBResps.pop();
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
    } else if (cppCall.methodName == "translateResp") {
        if (cppCall.paddr != pymtl3Call.paddr) {
            reason = "paddr mismatch: C++=0x" + std::to_string(cppCall.paddr) +
                     ", PyMTL3=0x" + std::to_string(pymtl3Call.paddr);
            return false;
        }
        if (cppCall.fault != pymtl3Call.fault) {
            reason = "fault mismatch: C++=" + std::to_string(cppCall.fault) +
                     ", PyMTL3=" + std::to_string(pymtl3Call.fault);
            return false;
        }
    }

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