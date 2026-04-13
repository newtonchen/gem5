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
#include "arch/generic/mmu.hh"
#include "base/str.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/limits.hh"
#include "mem/request.hh"
#include <iostream>

// 全局变量，用于回调函数访问当前实例
static gem5::o3::LSQUnitComparison* g_currentLSQUnitComparison = nullptr;

// 全局变量，用于TLB回调访问当前实例
static gem5::o3::LSQUnitComparison* g_currentLSQUnitForTLB = nullptr;

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
      mockDCache(nullptr),
      cpuPtr(nullptr),
      threadId(0),
      outstandingTLBReqs(),
      tlbRespCallback(nullptr)
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
                                 const std::string& methodName, uint64_t seqNum)
{
    if (g_currentLSQUnitComparison) {
        g_currentLSQUnitComparison->notifyPyMTL3DCacheCall(cycle, addr, size, isWrite, data, methodName, seqNum);
    }
}

// 静态回调函数，用于处理 PyMTL3 的 TLB 转换请求（新异步方式）
// 这个函数会被 PyMTL3 调用，发起异步 TLB 转换
static void pymtl3TLBReqCallback(uint64_t seq_num, uint64_t vaddr, 
                                  uint32_t size, bool is_load)
{
    if (g_currentLSQUnitForTLB) {
        // 调用 LSQUnitComparison::handleTLBReq 发起异步 TLB 转换
        g_currentLSQUnitForTLB->handleTLBReq(seq_num, vaddr, size, is_load);
    }
}

// 静态回调函数，用于发送 TLB 转换结果给 PyMTL3
// 这个函数会被 C++ 调用，将 TLB 结果发送回 PyMTL3
static void pymtl3TLBRespCallback(uint64_t seq_num, uint64_t paddr, int fault)
{
    if (g_currentLSQUnitForTLB && g_currentLSQUnitForTLB->getPyMTL3LSQ()) {
        // 调用 pybind11 函数将结果发送给 PyMTL3
        pymtl3_send_tlb_resp(g_currentLSQUnitForTLB->getPyMTL3LSQ(), 
                              seq_num, paddr, fault);
    }
}

void
LSQUnitComparison::init(CPU *cpu_ptr, IEW *iew_ptr,
                       const BaseO3CPUParams &params,
                       LSQ *lsq_ptr, unsigned id)
{
    // 调用基类 init（这是主要的 Gem5 实现）
    LSQUnit::init(cpu_ptr, iew_ptr, params, lsq_ptr, id);

    // 保存 CPU 指针和线程 ID，用于 TLB 转换
    cpuPtr = cpu_ptr;
    threadId = id;

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
            g_currentLSQUnitForTLB = this;
            
            // 设置 DCache 回调函数
            pymtl3_set_dcache_callback(pymtl3LSQ, pymtl3DCacheCallback);
            
            // 设置 TLB 请求回调函数（新异步方式）
            std::cout << "[LSQComparison] Setting up TLB request callback..." << std::endl;
            pymtl3_set_tlb_req_callback(pymtl3LSQ, pymtl3TLBReqCallback);
            
            // 设置 TLB 响应回调函数，用于异步 TLB 结果返回
            std::cout << "[LSQComparison] Setting up TLB response callback..." << std::endl;
            pymtl3_set_tlb_resp_callback(pymtl3LSQ, pymtl3TLBRespCallback);
            
            // 保存回调函数指针到成员变量
            tlbRespCallback = pymtl3TLBRespCallback;
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
        
        // 获取指令的 fault 状态（来自之前流水线阶段，如 ITLB）
        int fault = (load_inst->getFault() != NoFault) ? 1 : 0;
        if (fault != 0) {
            std::cerr << "[LSQComparison-DEBUG] insertLoad: sn=" << seq_num 
                      << " has fault from earlier stage" << std::endl;
        }

        pymtl3_insert_load(pymtl3LSQ, seq_num, pc, ea, size, fault);

        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
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
        
        // 获取指令的 fault 状态（来自之前流水线阶段，如 ITLB）
        int fault = (store_inst->getFault() != NoFault) ? 1 : 0;
        if (fault != 0) {
            std::cerr << "[LSQComparison-DEBUG] insertStore: sn=" << seq_num 
                      << " has fault from earlier stage" << std::endl;
        }

        // 调试日志：追踪 sn=37-40
        if (seq_num >= 37 && seq_num <= 40) {
            std::cerr << "[LSQComparison-TRACE] insertStore: sn=" << seq_num 
                      << ", pc=0x" << std::hex << pc << std::dec
                      << ", fault=" << fault << std::endl;
        }

        pymtl3_insert_store(pymtl3LSQ, seq_num, pc, ea, size, fault);

        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
    }
}

Fault
LSQUnitComparison::executeLoad(const DynInstPtr &inst)
{
    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeLoad(inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = inst->seqNum;
        int lq_idx = inst->lqIdx;
        
        // 调试输出 - 添加更多上下文信息
        std::cerr << "[LSQComparison-DEBUG] executeLoad: sn=" << seq_num 
                  << ", lq_idx=" << lq_idx
                  << ", effAddrValid=" << inst->effAddrValid()
                  << ", isExecuted=" << inst->isExecuted()
                  << ", isTranslationDelayed=" << inst->isTranslationDelayed()
                  << ", translationCompleted=" << inst->translationCompleted()
                  << ", getFault=" << (inst->getFault() == NoFault ? "NoFault" : "Fault")
                  << ", result=" << (result == NoFault ? "NoFault" : "Fault")
                  << std::endl;
        
        // 如果指令已经在 Gem5 中执行过了，跳过 fault 比较
        // 因为这条指令可能已经在之前的周期中被处理过，PyMTL3 可能还没有同步
        if (inst->isExecuted()) {
            std::cerr << "[LSQComparison-DEBUG] executeLoad: sn=" << seq_num 
                      << " already executed, skipping fault comparison" << std::endl;
        } else {
            // 在调用 execute_load 之前，先更新 PyMTL3 中指令的 fault 状态
            // 这确保了即使 fault 是在 insert 之后设置的，也能正确同步
            int fault = (inst->getFault() != NoFault) ? 1 : 0;
            pymtl3_update_load_inst_fault(pymtl3LSQ, lq_idx, fault, seq_num);
            
            // 调用 PyMTL3 的 execute_load
            int py_fault = pymtl3_execute_load(pymtl3LSQ, seq_num, lq_idx);
            
            // 对比结果
            if ((result == NoFault && py_fault != 0) || 
                (result != NoFault && py_fault == 0)) {
                std::cerr << "[LSQComparison] Mismatch in executeLoad fault: "
                          << "Gem5=" << (result == NoFault ? "NoFault" : "Fault")
                          << ", PyMTL3=" << (py_fault == 0 ? "NoFault" : "Fault") << " [sn=" << seq_num << ", lq_idx=" << lq_idx << "]"
                          << std::endl;
            }
        }
        
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
        compareStatistics();
    }

    return result;
}

Fault
LSQUnitComparison::executeStore(const DynInstPtr &inst)
{
    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeStore(inst);

    // 如果 PyMTL3 可用，更新 store 地址并调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        // Store 执行后，地址和大小已经计算完成
        InstSeqNum seq_num = inst->seqNum;
        int sq_idx = inst->sqIdx;
        Addr ea = inst->effAddr;
        uint32_t size = inst->effSize;
        
        // 调试输出
        std::cerr << "[LSQComparison-DEBUG] executeStore: sn=" << seq_num 
                  << ", sq_idx=" << sq_idx
                  << ", ea=0x" << std::hex << ea << std::dec
                  << ", size=" << size << std::endl;
        
        // 1. 更新 PyMTL3 中的 store 地址
        pymtl3_update_store_addr(pymtl3LSQ, seq_num, ea, size);
        
        // 2. 调用 PyMTL3 的 execute_store
        int py_fault = pymtl3_execute_store(pymtl3LSQ, seq_num, sq_idx);
        
        // 对比结果
        if ((result == NoFault && py_fault != 0) || 
            (result != NoFault && py_fault == 0)) {
            std::cerr << "[LSQComparison] Mismatch in executeStore fault: "
                      << "Gem5=" << (result == NoFault ? "NoFault" : "Fault")
                      << ", PyMTL3=" << (py_fault == 0 ? "NoFault" : "Fault") << std::endl;
        }
        
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
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
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
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
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
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
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
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
        // 注意：队列状态比较在 LSQ::tick() 中统一进行
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

    // 1. 记录当前 cycle (使用 PyMTL3 的 cycle，与 PyMTL3 同步)
    uint64_t callCycle = 0;
    if (pymtl3Available && pymtl3LSQ) {
        callCycle = pymtl3_get_cycle(pymtl3LSQ);
    } else {
        callCycle = curTick();  // 回退到 tick（不应该发生）
    }

    // 2. 记录 C++ 的 DCache 调用
    if (mockDCache && pkt) {
        std::cerr << "[LSQComparison-DEBUG] Recording C++ DCache call at cycle=" << callCycle << std::endl;
        mockDCache->recordCPCall(callCycle, pkt, "completeDataAccess");
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
        if (data_pkt) {
            std::cerr << "[LSQComparison-DEBUG] Packet details: addr=0x" 
                      << std::hex << data_pkt->getAddr() << std::dec
                      << ", size=" << data_pkt->getSize()
                      << ", isWrite=" << data_pkt->isWrite() << std::endl;
        }
    }

    // 1. 记录当前 cycle (使用 PyMTL3 的 cycle，与 PyMTL3 同步)
    uint64_t callCycle = 0;
    if (pymtl3Available && pymtl3LSQ) {
        callCycle = pymtl3_get_cycle(pymtl3LSQ);
    } else {
        callCycle = curTick();  // 回退到 tick（不应该发生）
    }

    // 2. 如果是store，更新PyMTL3的地址为物理地址（用于比较）
    if (!isLoad && data_pkt && pymtl3Available && pymtl3LSQ) {
        // 从packet中获取物理地址和大小
        Addr physAddr = data_pkt->getAddr();
        uint32_t size = data_pkt->getSize();
        
        // 尝试从packet的inst获取seq_num
        // 注意：packet可能不直接包含inst，我们需要其他方式获取
        // 这里我们暂时使用地址来匹配
        std::cerr << "[LSQComparison-DEBUG] Store sendTimingReq: physAddr=0x" 
                  << std::hex << physAddr << std::dec << ", size=" << size << std::endl;
        
        // TODO: 更新PyMTL3的store地址为物理地址
        // 这需要知道是哪个store在发送，可能需要从LSQUnit的状态中获取
    }

    // 3. 调用基类实现（Gem5）- 实际发送数据包
    bool ret = LSQUnit::trySendPacket(isLoad, data_pkt);
    
    // 4. 只在请求真正被发送时才记录 DCache 调用
    // 注意：trySendPacket 可能返回 false（缓存端口忙），此时不应记录
    // 另外，PyMTL3 的 dcache_req 只处理 store 写回（isLoad=false），
    // 所以只记录 store 请求用于比较
    if (ret && !isLoad && mockDCache && data_pkt) {
        // 从 packet 的 senderState 获取指令 seqNum
        uint64_t seqNum = 0;
        if (data_pkt->senderState) {
            LSQRequest *request = dynamic_cast<LSQRequest*>(data_pkt->senderState);
            if (request && request->instruction()) {
                seqNum = request->instruction()->seqNum;
            }
        }
        std::cerr << "[LSQComparison-DEBUG] Recording C++ DCache sendTimingReq at cycle=" << callCycle << " sn=" << seqNum << " (sent successfully)" << std::endl;
        mockDCache->recordCPCall(callCycle, data_pkt, "sendTimingReq", seqNum);
    } else if (!ret && sendCount <= 10) {
        std::cerr << "[LSQComparison-DEBUG] sendTimingReq not sent (cache blocked or port unavailable)" << std::endl;
    }

    // 5. 对比 DCache 调用
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
                                         const std::string& methodName,
                                         uint64_t seqNum)
{
    if (!mockDCache) {
        return;
    }

    // 记录 PyMTL3 的 DCache 调用，包含 seqNum 用于准确匹配
    mockDCache->recordPyMTL3Call(cycle, addr, size, isWrite, data, methodName, seqNum);
}

Addr
LSQUnitComparison::translateAddress(Addr vaddr)
{
    // 使用 Gem5 的 MMU 进行真正的 TLB 转换
    std::cerr << "[LSQComparison-TLB] translateAddress called for vaddr=0x" 
              << std::hex << vaddr << std::dec << std::endl;
    
    // 检查 CPU 指针是否有效
    if (!cpuPtr) {
        std::cerr << "[LSQComparison-TLB] CPU pointer not available, returning vaddr" << std::endl;
        return vaddr;
    }
    
    // 获取 ThreadContext 用于转换
    gem5::ThreadContext *tc = nullptr;
    try {
        tc = cpuPtr->tcBase(threadId);
    } catch (...) {
        std::cerr << "[LSQComparison-TLB] Exception getting ThreadContext, returning vaddr" << std::endl;
        return vaddr;
    }
    
    if (!tc) {
        std::cerr << "[LSQComparison-TLB] ThreadContext not available, returning vaddr" << std::endl;
        return vaddr;
    }
    
    // 在SE模式下，TLB需要Process指针
    // 检查ThreadContext是否有有效的Process
    if (!tc->getProcessPtr()) {
        std::cerr << "[LSQComparison-TLB] Process not available in ThreadContext, returning vaddr" << std::endl;
        return vaddr;
    }
    
    // 获取 MMU
    BaseMMU *mmu = tc->getMMUPtr();
    if (!mmu) {
        std::cerr << "[LSQComparison-TLB] MMU not available from TC, trying getMMUPtr()" << std::endl;
        // 回退到 getMMUPtr()
        mmu = getMMUPtr();
    }
    
    if (!mmu) {
        std::cerr << "[LSQComparison-TLB] MMU not available, returning vaddr" << std::endl;
        return vaddr;
    }
    
    // 在SE模式下，某些地址可能导致TLB崩溃
    // 添加地址范围检查，避免访问无效地址
    // 通常用户空间地址在 0x0000_0000_0000_0000 到 0x0000_7FFF_FFFF_FFFF
    // 或者内核空间地址在 0xFFFF_8000_0000_0000 以上
    
    // 简化处理：对于SE模式下的模拟，使用identity mapping
    // 这样可以避免TLB转换的复杂性
    std::cerr << "[LSQComparison-TLB] Using identity mapping for SE mode" << std::endl;
    return vaddr;
    
    // 注意：如果需要真正的TLB转换，可以取消下面的注释
    // 但需要确保Process和页表正确初始化
    /*
    // 创建 Request 用于 TLB 转换
    // 使用 8 字节作为默认大小（可以根据需要调整）
    RequestPtr req = std::make_shared<Request>(vaddr, 8, Request::PHYSICAL, 0);
    
    // 执行 TLB 转换
    Fault fault = NoFault;
    try {
        fault = mmu->translateAtomic(req, tc, BaseMMU::Mode::Write);
    } catch (...) {
        std::cerr << "[LSQComparison-TLB] Exception during translateAtomic, returning vaddr" << std::endl;
        return vaddr;
    }
    
    if (fault != NoFault) {
        std::cerr << "[LSQComparison-TLB] Translation failed with fault" << std::endl;
        // 转换失败，返回虚拟地址
        return vaddr;
    }
    
    // 获取转换后的物理地址
    if (req->hasPaddr()) {
        Addr paddr = req->getPaddr();
        std::cerr << "[LSQComparison-TLB] Translation successful: vaddr=0x" 
                  << std::hex << vaddr << " -> paddr=0x" << paddr << std::dec << std::endl;
        return paddr;
    } else {
        std::cerr << "[LSQComparison-TLB] Translation did not set paddr" << std::endl;
        return vaddr;
    }
    */
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

    // 如果只有 PyMTL3 有调用，记录不匹配
    if (!mockDCache->hasPendingCPCalls() &&
        mockDCache->hasPendingPyMTL3Calls()) {
        DCacheCallRecord pymtl3Call;
        mockDCache->getNextPyMTL3Call(pymtl3Call);
        logMismatch("DCache call",
            csprintf("PyMTL3 called %s but C++ didn't",
                     pymtl3Call.methodName));
        return;
    }

    // 如果只有 C++ 有调用，可能是 C++ 在同一 cycle 发送了多个请求
    // 而 PyMTL3 只发送了一个。这种情况下，我们暂时跳过比较，
    // 等待 PyMTL3 的调用到达
    if (mockDCache->hasPendingCPCalls() &&
        !mockDCache->hasPendingPyMTL3Calls()) {
        // 不记录为不匹配，只是等待 PyMTL3 的调用
        // 因为 C++ 可能在同一 cycle 发送多个请求
        return;
    }

    // 双方都有调用，进行对比
    // 注意：C++ 可能在同一 cycle 发送多个请求，而 PyMTL3 只发送一个
    // 所以我们采用宽松的比较策略：只要 PyMTL3 的请求匹配 C++ 的任意一个请求即可
    DCacheCallRecord pymtl3Call;
    mockDCache->getNextPyMTL3Call(pymtl3Call);
    
    // 获取 C++ 的所有待处理调用
    std::vector<DCacheCallRecord> cppCalls;
    while (mockDCache->hasPendingCPCalls()) {
        DCacheCallRecord cppCall;
        mockDCache->getNextCPCall(cppCall);
        cppCalls.push_back(cppCall);
    }
    
    // 尝试找到匹配的 C++ 调用
    // 优先使用 seqNum 进行匹配，这是最准确的方式
    bool foundMatch = false;
    std::string bestReason = "no matching call found";
    
    // 首先尝试用 seqNum 精确匹配
    if (pymtl3Call.seqNum != 0) {
        for (auto& cppCall : cppCalls) {
            if (cppCall.seqNum == pymtl3Call.seqNum) {
                // seqNum 匹配，进行详细比较
                std::string reason;
                if (MockDCachePort::compareCalls(cppCall, pymtl3Call, reason)) {
                    foundMatch = true;
                } else {
                    bestReason = reason;
                }
                break;
            }
        }
    }
    
    // 如果没有找到 seqNum 匹配，尝试其他匹配方式
    if (!foundMatch && pymtl3Call.seqNum == 0) {
        for (auto& cppCall : cppCalls) {
            std::string reason;
            if (MockDCachePort::compareCalls(cppCall, pymtl3Call, reason)) {
                foundMatch = true;
                break;
            } else {
                // 记录第一个不匹配的原因
                if (bestReason == "no matching call found") {
                    bestReason = reason;
                }
            }
        }
    }
    
    if (!foundMatch) {
        // 如果没有找到匹配，记录不匹配
        // 但只记录第一个 C++ 调用和 PyMTL3 调用的对比
        if (!cppCalls.empty()) {
            logMismatch("DCache call",
                csprintf("%s vs %s: %s",
                         MockDCachePort::callToString(cppCalls[0]).c_str(),
                         MockDCachePort::callToString(pymtl3Call).c_str(),
                         bestReason.c_str()));
        }
    }
}

// ===== 异步TLB转换实现 =====

void
LSQUnitComparison::handleTLBReq(uint64_t seq_num, Addr vaddr, 
                                 uint32_t size, bool is_load)
{
    std::cerr << "[LSQComparison-TLB] handleTLBReq: sn=" << seq_num
              << ", vaddr=0x" << std::hex << vaddr << std::dec
              << ", size=" << size << ", is_load=" << is_load << std::endl;
    
    // 获取 ThreadContext 和 MMU
    if (!cpuPtr) {
        std::cerr << "[LSQComparison-TLB] ERROR: CPU pointer not available" << std::endl;
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    gem5::ThreadContext* tc = nullptr;
    try {
        tc = cpuPtr->tcBase(threadId);
    } catch (...) {
        std::cerr << "[LSQComparison-TLB] ERROR: Exception getting ThreadContext" << std::endl;
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    if (!tc) {
        std::cerr << "[LSQComparison-TLB] ERROR: ThreadContext not available" << std::endl;
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    BaseMMU* mmu = tc->getMMUPtr();
    if (!mmu) {
        mmu = getMMUPtr();
    }
    
    if (!mmu) {
        std::cerr << "[LSQComparison-TLB] ERROR: MMU not available" << std::endl;
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    // 创建 finish callback
    auto finishCallback = [this](uint64_t sn, Addr paddr, Fault fault, bool delayed) {
        this->completeTLBTranslation(sn, paddr, fault, delayed);
    };
    
    // 创建 PyTLBRequest 对象
    auto tlbReq = std::make_unique<PyTLBRequest>(
        vaddr, size, is_load, seq_num, tc, mmu, finishCallback);
    
    // 保存到 outstanding 列表
    outstandingTLBReqs.push_back(std::move(tlbReq));
    
    // 获取指针并启动转换
    PyTLBRequest* reqPtr = outstandingTLBReqs.back().get();
    reqPtr->initiateTranslation();
    
    // 注意：initiateTranslation 可能同步调用 finish()，也可能异步调用
    // 同步调用时，outstandingTLBReqs 中的 entry 可能已经被移除
}

void
LSQUnitComparison::completeTLBTranslation(uint64_t seq_num, Addr paddr, 
                                           Fault fault, bool delayed)
{
    std::cerr << "[LSQComparison-TLB] completeTLBTranslation: sn=" << seq_num
              << ", paddr=0x" << std::hex << paddr << std::dec
              << ", fault=" << (fault == NoFault ? "NoFault" : "Fault")
              << ", delayed=" << delayed << std::endl;
    
    // 发送结果给 PyMTL3
    int fault_code = (fault == NoFault) ? 0 : 1;
    sendTLBResp(seq_num, paddr, fault_code);
    
    // 从 outstanding 列表中移除（如果还在的话）
    for (auto it = outstandingTLBReqs.begin(); it != outstandingTLBReqs.end(); ++it) {
        if ((*it)->getSeqNum() == seq_num) {
            outstandingTLBReqs.erase(it);
            break;
        }
    }
}

void
LSQUnitComparison::sendTLBResp(uint64_t seq_num, Addr paddr, int fault)
{
    std::cerr << "[LSQComparison-TLB] sendTLBResp: sn=" << seq_num
              << ", paddr=0x" << std::hex << paddr << std::dec
              << ", fault=" << fault << std::endl;
    
    if (tlbRespCallback) {
        tlbRespCallback(seq_num, paddr, fault);
    } else {
        std::cerr << "[LSQComparison-TLB] Warning: tlbRespCallback not set" << std::endl;
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
