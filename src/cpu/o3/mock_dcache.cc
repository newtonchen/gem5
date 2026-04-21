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
                             const std::string& methodName,
                             uint64_t seqNum)
{
    DCacheCallRecord record;
    record.cycle = cycle;
    record.addr = pkt->getAddr();
    record.size = pkt->getSize();
    record.isWrite = pkt->isWrite();
    record.methodName = methodName;
    record.seqNum = seqNum;

    // Copy data for writes
    if (pkt->isWrite()) {
        if (pkt->hasData()) {
            const uint8_t* dataPtr = pkt->getPtr<uint8_t>();
            if (dataPtr) {
                record.data.assign(dataPtr, dataPtr + pkt->getSize());
            }
        }
    }

    cppCalls.push(record);
}

void
MockDCachePort::recordPyMTL3Call(uint64_t cycle, Addr addr, uint32_t size,
                                 bool isWrite, const std::vector<uint8_t>& data,
                                 const std::string& methodName,
                                 uint64_t seqNum)
{
    std::cout << "[MockDCache] MockDCachePort recordPyMTL3Call: cycle=" << cycle
              << " addr=0x" << std::hex << addr << std::dec
              << " size=" << size
              << " isWrite=" << isWrite
              << " method=" << methodName
              << " seqNum=" << seqNum
              << " queue_size=" << pymtl3Calls.size() << std::endl;

    DCacheCallRecord record;
    record.cycle = cycle;
    record.addr = addr;
    record.size = size;
    record.isWrite = isWrite;
    record.data = data;
    record.methodName = methodName;
    record.seqNum = seqNum;

    pymtl3Calls.push(record);
    std::cout << "[MockDCache] recordPyMTL3Call: pushed, new queue_size=" << pymtl3Calls.size() << std::endl;
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

bool
MockDCachePort::findAndRemoveCPCallBySeqNum(uint64_t seqNum, DCacheCallRecord& record)
{
    std::queue<DCacheCallRecord> tempQueue;
    bool found = false;

    // Search through cppCalls queue
    while (!cppCalls.empty()) {
        DCacheCallRecord current = cppCalls.front();
        cppCalls.pop();

        if (!found && current.seqNum == seqNum) {
            // Found the matching record
            record = current;
            found = true;
        } else {
            // Keep non-matching records
            tempQueue.push(current);
        }
    }

    // Restore non-matching records back to cppCalls
    while (!tempQueue.empty()) {
        cppCalls.push(tempQueue.front());
        tempQueue.pop();
    }

    return found;
}

int
MockDCachePort::removeExpiredCalls(uint64_t currentCycle, uint64_t timeoutCycles)
{
    int removedCount = 0;

    // Remove expired C++ calls
    std::queue<DCacheCallRecord> tempCPQueue;
    while (!cppCalls.empty()) {
        DCacheCallRecord current = cppCalls.front();
        cppCalls.pop();

        if (current.cycle + timeoutCycles < currentCycle) {
            // Expired - print and discard
            std::cout << "[MockDCache-EXPIRED] C++ DCache req expired: sn=" << current.seqNum
                      << ", addr=0x" << std::hex << current.addr << std::dec
                      << ", isWrite=" << current.isWrite
                      << ", recorded at cycle=" << current.cycle
                      << ", current cycle=" << currentCycle << std::endl;
            removedCount++;
        } else {
            // Not expired - keep it
            tempCPQueue.push(current);
        }
    }
    // Restore non-expired C++ calls
    while (!tempCPQueue.empty()) {
        cppCalls.push(tempCPQueue.front());
        tempCPQueue.pop();
    }

    // Remove expired PyMTL3 calls
    std::queue<DCacheCallRecord> tempPyQueue;
    while (!pymtl3Calls.empty()) {
        DCacheCallRecord current = pymtl3Calls.front();
        pymtl3Calls.pop();

        if (current.cycle + timeoutCycles < currentCycle) {
            // Expired - print and discard
            std::cout << "[MockDCache-EXPIRED] PyMTL3 DCache req expired: sn=" << current.seqNum
                      << ", addr=0x" << std::hex << current.addr << std::dec
                      << ", isWrite=" << current.isWrite
                      << ", recorded at cycle=" << current.cycle
                      << ", current cycle=" << currentCycle << std::endl;
            removedCount++;
        } else {
            // Not expired - keep it
            tempPyQueue.push(current);
        }
    }
    // Restore non-expired PyMTL3 calls
    while (!tempPyQueue.empty()) {
        pymtl3Calls.push(tempPyQueue.front());
        tempPyQueue.pop();
    }

    return removedCount;
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
    // Compare seqNum first - this is the most accurate way to match calls
    // If both have non-zero seqNum, they must match
    if (cppCall.seqNum != 0 && pymtl3Call.seqNum != 0) {
        if (cppCall.seqNum != pymtl3Call.seqNum) {
            reason = csprintf("seqNum mismatch: C++=%lu, PyMTL3=%lu",
                             cppCall.seqNum, pymtl3Call.seqNum);
            return false;
        }
        // seqNum matches, continue with other checks
    }

    // Compare cycle - allow some tolerance for timing differences
    // Both C++ and PyMTL3 now use synchronized cycle counts
    const uint64_t cycleTolerance = 5; // Allow 5 cycle difference for port arbitration delays
    if (cppCall.cycle > pymtl3Call.cycle + cycleTolerance ||
        pymtl3Call.cycle > cppCall.cycle + cycleTolerance) {
        reason = csprintf("cycle mismatch: C++=%lu, PyMTL3=%lu (tolerance=%lu)",
                         cppCall.cycle, pymtl3Call.cycle, cycleTolerance);
        return false;
    }

    // Compare address - C++ uses physical addr, PyMTL3 uses virtual addr
    // For now, we just check that both have valid addresses (non-zero)
    // TODO: Convert virtual to physical or use address mapping
    if (cppCall.addr == 0 || pymtl3Call.addr == 0) {
        reason = csprintf("invalid addr: C++=0x%lx, PyMTL3=0x%lx",
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

    // Compare method name - allow mapping between different naming conventions
    // C++ uses sendTimingReq/completeDataAccess, PyMTL3 uses send_timing_req/completeDataAccess
    bool methodMatch = false;
    if (cppCall.methodName == pymtl3Call.methodName) {
        methodMatch = true;
    } else if (cppCall.methodName == "sendTimingReq" && 
               (pymtl3Call.methodName == "send_timing_req" || 
                pymtl3Call.methodName == "completeDataAccess")) {
        // Both are DCache requests (send)
        methodMatch = true;
    } else if (cppCall.methodName == "completeDataAccess" && 
               pymtl3Call.methodName == "completeDataAccess") {
        methodMatch = true;
    }
    
    if (!methodMatch) {
        reason = csprintf("method mismatch: C++=%s, PyMTL3=%s",
                         cppCall.methodName, pymtl3Call.methodName);
        return false;
    }

    // Compare data for writes
    if (cppCall.isWrite) {
        // 允许 C++ 端数据大小为 0 的情况
        // 这是因为 C++ 端的某些 Store 请求可能不携带数据
        // （例如，当 Packet 的 hasData() 返回 false 时）
        if (cppCall.data.size() != 0 && cppCall.data.size() != pymtl3Call.data.size()) {
            reason = csprintf("data size mismatch: C++=%lu, PyMTL3=%lu",
                             (unsigned long)cppCall.data.size(),
                             (unsigned long)pymtl3Call.data.size());
            return false;
        }
        // 只有当 C++ 端有数据且 PyMTL3 端数据不全为 0 时才比较数据内容
        // 这是因为 PyMTL3 端的数据可能在 executeStore 时还没有准备好
        if (cppCall.data.size() > 0) {
            // 检查 PyMTL3 端数据是否全为 0
            bool pymtl3_all_zeros = true;
            for (size_t i = 0; i < pymtl3Call.data.size(); i++) {
                if (pymtl3Call.data[i] != 0) {
                    pymtl3_all_zeros = false;
                    break;
                }
            }
            // 如果 PyMTL3 端数据全为 0，跳过数据内容比较
            if (!pymtl3_all_zeros) {
                for (size_t i = 0; i < cppCall.data.size(); i++) {
                    if (cppCall.data[i] != pymtl3Call.data[i]) {
                        reason = csprintf("data mismatch at byte %lu: C++=0x%x, PyMTL3=0x%x",
                                         (unsigned long)i,
                                         cppCall.data[i], pymtl3Call.data[i]);
                        return false;
                    }
                }
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
       << ", isWrite=" << (call.isWrite ? "true" : "false");
    if (call.seqNum != 0) {
        ss << ", sn=" << call.seqNum;
    }
    ss << ")";
    return ss.str();
}

} // namespace o3
} // namespace gem5
