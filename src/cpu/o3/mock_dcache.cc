/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * MockDCache implementation
 */

#include "cpu/o3/mock_dcache.hh"
#include "base/str.hh"
#include <sstream>

namespace gem5
{

namespace o3
{

MockDCachePort::MockDCachePort()
{
}

MockDCachePort::~MockDCachePort()
{
}

void
MockDCachePort::recordCPCall(uint64_t cycle, PacketPtr pkt,
                             const std::string& methodName)
{
    DCacheCallRecord record;
    record.cycle = cycle;
    record.addr = pkt->getAddr();
    record.size = pkt->getSize();
    record.isWrite = pkt->isWrite();
    record.methodName = methodName;

    // Copy data for writes
    if (pkt->isWrite() && pkt->hasData()) {
        const uint8_t* dataPtr = pkt->getPtr<uint8_t>();
        record.data.assign(dataPtr, dataPtr + pkt->getSize());
    }

    cppCalls.push(record);
}

void
MockDCachePort::recordPyMTL3Call(uint64_t cycle, Addr addr, uint32_t size,
                                 bool isWrite, const std::vector<uint8_t>& data,
                                 const std::string& methodName)
{
    DCacheCallRecord record;
    record.cycle = cycle;
    record.addr = addr;
    record.size = size;
    record.isWrite = isWrite;
    record.data = data;
    record.methodName = methodName;

    pymtl3Calls.push(record);
}

bool
MockDCachePort::getNextCPCall(DCacheCallRecord& record)
{
    if (cppCalls.empty()) {
        return false;
    }
    record = cppCalls.front();
    cppCalls.pop();
    return true;
}

bool
MockDCachePort::getNextPyMTL3Call(DCacheCallRecord& record)
{
    if (pymtl3Calls.empty()) {
        return false;
    }
    record = pymtl3Calls.front();
    pymtl3Calls.pop();
    return true;
}

bool
MockDCachePort::hasPendingCPCalls() const
{
    return !cppCalls.empty();
}

bool
MockDCachePort::hasPendingPyMTL3Calls() const
{
    return !pymtl3Calls.empty();
}

void
MockDCachePort::clear()
{
    while (!cppCalls.empty()) {
        cppCalls.pop();
    }
    while (!pymtl3Calls.empty()) {
        pymtl3Calls.pop();
    }
}

bool
MockDCachePort::compareCalls(const DCacheCallRecord& cppCall,
                             const DCacheCallRecord& pymtl3Call,
                             std::string& reason)
{
    // Compare cycle
    if (cppCall.cycle != pymtl3Call.cycle) {
        reason = csprintf("cycle mismatch: C++=%lu, PyMTL3=%lu",
                         cppCall.cycle, pymtl3Call.cycle);
        return false;
    }

    // Compare address
    if (cppCall.addr != pymtl3Call.addr) {
        reason = csprintf("addr mismatch: C++=0x%lx, PyMTL3=0x%lx",
                         cppCall.addr, pymtl3Call.addr);
        return false;
    }

    // Compare size
    if (cppCall.size != pymtl3Call.size) {
        reason = csprintf("size mismatch: C++=%u, PyMTL3=%u",
                         cppCall.size, pymtl3Call.size);
        return false;
    }

    // Compare read/write
    if (cppCall.isWrite != pymtl3Call.isWrite) {
        reason = csprintf("isWrite mismatch: C++=%s, PyMTL3=%s",
                         cppCall.isWrite ? "true" : "false",
                         pymtl3Call.isWrite ? "true" : "false");
        return false;
    }

    // Compare method name
    if (cppCall.methodName != pymtl3Call.methodName) {
        reason = csprintf("method mismatch: C++=%s, PyMTL3=%s",
                         cppCall.methodName, pymtl3Call.methodName);
        return false;
    }

    // Compare data for writes
    if (cppCall.isWrite) {
        if (cppCall.data.size() != pymtl3Call.data.size()) {
            reason = csprintf("data size mismatch: C++=%zu, PyMTL3=%zu",
                             cppCall.data.size(), pymtl3Call.data.size());
            return false;
        }
        for (size_t i = 0; i < cppCall.data.size(); i++) {
            if (cppCall.data[i] != pymtl3Call.data[i]) {
                reason = csprintf("data mismatch at byte %zu: C++=0x%x, PyMTL3=0x%x",
                                 i, cppCall.data[i], pymtl3Call.data[i]);
                return false;
            }
        }
    }

    reason = "calls match";
    return true;
}

std::string
MockDCachePort::callToString(const DCacheCallRecord& call)
{
    std::stringstream ss;
    ss << call.methodName << "("
       << "cycle=" << call.cycle
       << ", addr=0x" << std::hex << call.addr << std::dec
       << ", size=" << call.size
       << ", isWrite=" << (call.isWrite ? "true" : "false")
       << ")";
    return ss.str();
}

} // namespace o3
} // namespace gem5
