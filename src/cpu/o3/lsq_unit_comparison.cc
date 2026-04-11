/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * LSQUnitComparison - Simplified Implementation
 *
 * This class inherits from LSQUnit and uses the base class as the Gem5 implementation.
 * It optionally creates a PyMTL3 LSQUnitCL instance for comparison.
 * If PyMTL3 is not available, it gracefully degrades to just using the base class.
 */

#include "cpu/o3/lsq_unit_comparison.hh"
#include "base/str.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/limits.hh"
#include <iostream>

// 全局变量，用于回调函数访问当前实例
static gem5::o3::LSQUnitComparison* g_currentLSQUnitComparison = nullptr;

namespace gem5
{

namespace o3
{

#if LSQ_COMPARISON_MODE

LSQUnitComparison::LSQUnitComparison(uint32_t lqEntries, uint32_t sqEntries)
    : LSQUnit(lqEntries, sqEntries),
      pymtl3LSQ(nullptr),
      pymtl3Available(false),
      mismatchCount(0),
      mockDCache(nullptr)
{
    // 注意：不在构造函数中初始化 Python，因为此时 Python 可能还未准备好
    // Mock DCache 端口的创建也移到 init() 中
    std::cerr << "[LSQComparison] LSQUnitComparison constructor called" << std::endl;
}

LSQUnitComparison::~LSQUnitComparison()
{
    // 清理 Mock DCache 端口
    if (mockDCache) {
        delete mockDCache;
        mockDCache = nullptr;
    }

    // PyMTL3 LSQ 的清理在 pymtl3_lsq_bindings.cc 中处理
}

// 静态回调函数，用于接收 PyMTL3 的 DCache 调用通知
static void pymtl3DCacheCallback(uint64_t cycle, uint64_t addr, uint32_t size,
                                 bool isWrite, const std::vector<uint8_t>& data,
                                 const std::string& methodName)
{
    if (g_currentLSQUnitComparison) {
        g_currentLSQUnitComparison->notifyPyMTL3DCacheCall(cycle, addr, size, isWrite, data, methodName);
    }
}

void
LSQUnitComparison::init(CPU *cpu_ptr, IEW *iew_ptr,
                       const BaseO3CPUParams &params,
                       LSQ *lsq_ptr, unsigned id)
{
    // 调用基类 init（这是主要的 Gem5 实现）
    LSQUnit::init(cpu_ptr, iew_ptr, params, lsq_ptr, id);

    // 创建 Mock DCache 端口（在 init 中创建，确保对象已完全构造）
    if (!mockDCache) {
        mockDCache = new MockDCachePort();
    }

    // 尝试初始化 PyMTL3 LSQUnitCL
    if (!pymtl3LSQ) {
        // 初始化 Python 模块
        init_pymtl3_module();
        
        pymtl3LSQ = create_pymtl3_lsq(params.LQEntries, params.SQEntries);
        pymtl3Available = (pymtl3LSQ != nullptr);
        if (pymtl3Available) {
            std::cout << "[LSQComparison] PyMTL3 LSQ initialized successfully" << std::endl;
            
            // 设置 DCache 回调，用于接收 PyMTL3 的 DCache 调用通知
            std::cout << "[LSQComparison] Setting up DCache callback..." << std::endl;
            
            // 设置全局指针，让回调函数可以访问当前实例
            g_currentLSQUnitComparison = this;
            
            // 设置回调函数
            pymtl3_set_dcache_callback(pymtl3LSQ, pymtl3DCacheCallback);
        } else {
            std::cout << "[LSQComparison] PyMTL3 LSQ not available, using Gem5 only" << std::endl;
        }
    }
}

std::string
LSQUnitComparison::name() const
{
    return csprintf("%s.comparison", LSQUnit::name());
}

// ==== 包装方法实现 ====

void
LSQUnitComparison::insertLoad(const DynInstPtr &load_inst)
{
    // 调用基类实现（Gem5）
    LSQUnit::insertLoad(load_inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = load_inst->seqNum;
        Addr pc = load_inst->pcState().instAddr();
        Addr ea = 0; // TODO: 从 DynInst 获取有效地址
        uint32_t size = 4; // TODO: 从 DynInst 获取访问大小

        pymtl3_insert_load(pymtl3LSQ, seq_num, pc, ea, size);

        // 对比队列状态
        compareQueueStates();
    }
}

void
LSQUnitComparison::insertStore(const DynInstPtr &store_inst)
{
    // 调用基类实现（Gem5）
    LSQUnit::insertStore(store_inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = store_inst->seqNum;
        Addr pc = store_inst->pcState().instAddr();
        Addr ea = 0; // TODO: 从 DynInst 获取有效地址
        uint32_t size = 4; // TODO: 从 DynInst 获取访问大小

        pymtl3_insert_store(pymtl3LSQ, seq_num, pc, ea, size);

        // 对比队列状态
        compareQueueStates();
    }
}

Fault
LSQUnitComparison::executeLoad(const DynInstPtr &inst)
{
    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeLoad(inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        // TODO: 调用 PyMTL3 实现并对比结果
        compareQueueStates();
        compareStatistics();
    }

    return result;
}

Fault
LSQUnitComparison::executeStore(const DynInstPtr &inst)
{
    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeStore(inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        // TODO: 调用 PyMTL3 实现并对比结果
        compareQueueStates();
        compareStatistics();
    }

    return result;
}

void
LSQUnitComparison::commitLoad()
{
    // 调用基类实现（Gem5）
    LSQUnit::commitLoad();

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        pymtl3_commit_load(pymtl3LSQ);
        compareQueueStates();
    }
}

void
LSQUnitComparison::commitLoads(InstSeqNum &youngest_inst)
{
    // 调用基类实现（Gem5）
    LSQUnit::commitLoads(youngest_inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        pymtl3_commit_stores(pymtl3LSQ, youngest_inst);
        compareQueueStates();
    }
}

void
LSQUnitComparison::commitStores(InstSeqNum &youngest_inst)
{
    // 调用基类实现（Gem5）
    LSQUnit::commitStores(youngest_inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        pymtl3_commit_stores(pymtl3LSQ, youngest_inst);
        compareQueueStates();
    }
}

void
LSQUnitComparison::writebackStores()
{
    // 调用基类实现（Gem5）
    LSQUnit::writebackStores();

    // PyMTL3 不需要显式写回
}

void
LSQUnitComparison::squash(const InstSeqNum &squashed_num)
{
    // 调用基类实现（Gem5）
    LSQUnit::squash(squashed_num);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        pymtl3_squash(pymtl3LSQ, squashed_num);
        compareQueueStates();
    }
}

void
LSQUnitComparison::checkSnoop(PacketPtr pkt)
{
    // 调用基类实现（Gem5）
    LSQUnit::checkSnoop(pkt);

    // PyMTL3 不需要 snoop 检查
}

void
LSQUnitComparison::startStaleTranslationFlush()
{
    // 调用基类实现（Gem5）
    LSQUnit::startStaleTranslationFlush();

    // PyMTL3 不需要此功能
}

bool
LSQUnitComparison::checkStaleTranslations()
{
    // 调用基类实现（Gem5）
    return LSQUnit::checkStaleTranslations();
}

void
LSQUnitComparison::completeDataAccess(PacketPtr pkt)
{
    // 0. 调试输出 - 确认方法被调用
    static int callCount = 0;
    callCount++;
    if (callCount <= 10) {
        std::cerr << "[LSQComparison-DEBUG] completeDataAccess called, count=" 
                  << callCount << ", pkt=" << pkt << std::endl;
    }

    // 1. 记录当前 tick (Gem5 使用 curTick() 而不是 curCycle())
    uint64_t callTick = curTick();

    // 2. 记录 C++ 的 DCache 调用
    if (mockDCache && pkt) {
        std::cerr << "[LSQComparison-DEBUG] Recording C++ DCache call" << std::endl;
        mockDCache->recordCPCall(callTick, pkt, "completeDataAccess");
    } else {
        if (callCount <= 10) {
            std::cerr << "[LSQComparison-DEBUG] Skipping record: mockDCache=" 
                      << mockDCache << ", pkt=" << pkt << std::endl;
        }
    }

    // 3. 调用基类实现（Gem5）
    LSQUnit::completeDataAccess(pkt);

    // 4. 对比 DCache 调用
    if (pymtl3Available && mockDCache) {
        compareDCacheCalls();
    }
}

bool
LSQUnitComparison::recvTimingResp(PacketPtr pkt)
{
    // 调用基类实现（Gem5）
    return LSQUnit::recvTimingResp(pkt);
}

void
LSQUnitComparison::recvRetry()
{
    // 调用基类实现（Gem5）
    LSQUnit::recvRetry();
}

bool
LSQUnitComparison::trySendPacket(bool isLoad, PacketPtr data_pkt)
{
    // 0. 调试输出 - 确认方法被调用
    static int sendCount = 0;
    sendCount++;
    if (sendCount <= 10) {
        std::cerr << "[LSQComparison-DEBUG] trySendPacket called, count=" 
                  << sendCount << ", isLoad=" << isLoad 
                  << ", pkt=" << data_pkt << std::endl;
    }

    // 1. 记录当前 tick
    uint64_t callTick = curTick();

    // 2. 记录 C++ 的 DCache 调用（在发送前记录）
    if (mockDCache && data_pkt) {
        std::cerr << "[LSQComparison-DEBUG] Recording C++ DCache sendTimingReq" << std::endl;
        mockDCache->recordCPCall(callTick, data_pkt, "sendTimingReq");
    }

    // 3. 调用基类实现（Gem5）- 实际发送数据包
    bool ret = LSQUnit::trySendPacket(isLoad, data_pkt);

    // 4. 对比 DCache 调用
    if (pymtl3Available && mockDCache) {
        compareDCacheCalls();
    }

    return ret;
}

// ==== 私有方法 ====

void
LSQUnitComparison::logMismatch(const std::string &methodName,
                              const std::string &message)
{
    mismatchCount++;
    std::cerr << "[LSQComparison] Mismatch in " << methodName
              << ": " << message << std::endl;
}

void
LSQUnitComparison::compareQueueStates()
{
    if (!pymtl3Available || !pymtl3LSQ) {
        return;
    }

    // 获取 Gem5 的队列大小
    int gem5Loads = numLoads();
    int gem5Stores = numStores();

    // 获取 PyMTL3 的队列大小
    int pymtl3Loads = pymtl3_num_loads(pymtl3LSQ);
    int pymtl3Stores = pymtl3_num_stores(pymtl3LSQ);

    // 对比队列大小
    if (gem5Loads != pymtl3Loads) {
        logMismatch("queue state (LQ size)",
            csprintf("Gem5: %d loads; PyMTL3: %d loads",
                     gem5Loads, pymtl3Loads));
    }

    if (gem5Stores != pymtl3Stores) {
        logMismatch("queue state (SQ size)",
            csprintf("Gem5: %d stores; PyMTL3: %d stores",
                     gem5Stores, pymtl3Stores));
    }
}

void
LSQUnitComparison::compareStatistics()
{
    if (!pymtl3Available || !pymtl3LSQ) {
        return;
    }

    // 对比关键统计计数器
    // 注意：Gem5 的统计信息是 Scalar 类型，需要通过 value() 获取
    // PyMTL3 的统计信息通过 pymtl3_get_stat 获取

    const char* statNames[] = {
        "forwLoads",
        "squashedLoads",
        "memOrderViolation",
        "rescheduledLoads",
        "blockedByCache"
    };

    for (const char* statName : statNames) {
        uint64_t pymtl3Value = pymtl3_get_stat(pymtl3LSQ, statName);
        // Gem5 的统计信息需要在运行时访问，这里简化处理
        // 实际实现需要访问 stats.forwLoads.value() 等
        // 但由于统计信息更新时机问题，暂时只记录 PyMTL3 的值
        if (pymtl3Value > 0) {
            // 可以在这里添加更详细的对比逻辑
            // 例如：对比 Gem5 和 PyMTL3 的计数器差异
        }
    }
}

void
LSQUnitComparison::notifyPyMTL3DCacheCall(uint64_t cycle, Addr addr,
                                         uint32_t size, bool isWrite,
                                         const std::vector<uint8_t>& data,
                                         const std::string& methodName)
{
    if (!mockDCache) {
        return;
    }

    // 记录 PyMTL3 的 DCache 调用
    mockDCache->recordPyMTL3Call(cycle, addr, size, isWrite, data, methodName);
}

void
LSQUnitComparison::compareDCacheCalls()
{
    if (!mockDCache) {
        return;
    }

    // 检查是否有待对比的调用
    if (!mockDCache->hasPendingCPCalls() &&
        !mockDCache->hasPendingPyMTL3Calls()) {
        return;
    }

    // 如果只有一方有调用，记录不匹配
    if (mockDCache->hasPendingCPCalls() &&
        !mockDCache->hasPendingPyMTL3Calls()) {
        DCacheCallRecord cppCall;
        mockDCache->getNextCPCall(cppCall);
        logMismatch("DCache call",
            csprintf("C++ called %s but PyMTL3 didn't",
                     cppCall.methodName));
        return;
    }

    if (!mockDCache->hasPendingCPCalls() &&
        mockDCache->hasPendingPyMTL3Calls()) {
        DCacheCallRecord pymtl3Call;
        mockDCache->getNextPyMTL3Call(pymtl3Call);
        logMismatch("DCache call",
            csprintf("PyMTL3 called %s but C++ didn't",
                     pymtl3Call.methodName));
        return;
    }

    // 双方都有调用，进行对比
    DCacheCallRecord cppCall;
    DCacheCallRecord pymtl3Call;

    mockDCache->getNextCPCall(cppCall);
    mockDCache->getNextPyMTL3Call(pymtl3Call);

    std::string reason;
    if (!MockDCachePort::compareCalls(cppCall, pymtl3Call, reason)) {
        logMismatch("DCache call",
            csprintf("%s vs %s: %s",
                     MockDCachePort::callToString(cppCall).c_str(),
                     MockDCachePort::callToString(pymtl3Call).c_str(),
                     reason.c_str()));
    }
}

// 全局回调函数，供 pybind11 调用
void notify_dcache_call_from_pymtl3(uint64_t cycle, Addr addr, uint32_t size,
                                    bool isWrite,
                                    const std::vector<uint8_t>& data,
                                    const std::string& methodName)
{
    // 这个函数会被 pybind11 调用，需要访问当前的 LSQUnitComparison 实例
    // 实际实现中需要通过全局变量或其他方式传递实例指针
    // 这里简化处理
}

#endif // LSQ_COMPARISON_MODE

} // namespace o3
} // namespace gem5
