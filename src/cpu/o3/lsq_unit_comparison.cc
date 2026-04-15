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
      mockTLB(nullptr),
      mockIQ(nullptr),
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

    // 清理 Mock TLB 端口
    if (mockTLB) {
        delete mockTLB;
        mockTLB = nullptr;
    }

    // 清理 Mock IQ 端口
    if (mockIQ) {
        delete mockIQ;
        mockIQ = nullptr;
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

// 静态回调函数，用于接收 PyMTL3 的 writeback 调用通知
static void pymtl3WritebackCallback(uint64_t cycle, uint64_t seqNum,
                                    bool hasData, const std::vector<uint8_t>& data,
                                    int fault)
{
    if (g_currentLSQUnitComparison) {
        g_currentLSQUnitComparison->notifyPyMTL3WritebackCall(cycle, seqNum, hasData, data, fault);
    }
}

// 静态回调函数，用于接收 PyMTL3 的 IQ replay/reschedule 调用通知
static void pymtl3IQCallback(uint64_t cycle, uint64_t seqNum,
                             const std::string& method)
{
    if (g_currentLSQUnitComparison) {
        g_currentLSQUnitComparison->notifyPyMTL3IQCall(cycle, seqNum, method);
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

    // 创建 Mock TLB 端口
    if (!mockTLB) {
        mockTLB = new MockTLBPort();
    }

    // 创建 Mock IQ 端口
    if (!mockIQ) {
        mockIQ = new MockIQPort();
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
            
            // 设置 writeback 回调函数，用于 writeback 对比
            std::cout << "[LSQComparison] Setting up writeback callback..." << std::endl;
            pymtl3_set_writeback_callback(pymtl3LSQ, pymtl3WritebackCallback);
            
            // 设置 IQ 回调函数，用于 replay/reschedule 对比
            std::cout << "[LSQComparison] Setting up IQ callback..." << std::endl;
            pymtl3_set_iq_callback(pymtl3LSQ, pymtl3IQCallback);
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

        pymtl3_insert_store(pymtl3LSQ, seq_num, pc, ea, size, fault);

        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
    }
}

Fault
LSQUnitComparison::executeLoad(const DynInstPtr &inst)
{
    uint64_t callCycle = curTick();

    // 记录执行前的 rescheduledLoads 计数
    uint64_t prevRescheduledLoads = 0;
    if (mockIQ) {
        prevRescheduledLoads = LSQUnit::stats.rescheduledLoads.value();
    }

    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeLoad(inst);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = inst->seqNum;
        int lq_idx = inst->lqIdx;

        // 记录 C++ 侧的 reschedule 调用（通过统计值变化检测）
        if (mockIQ) {
            uint64_t curRescheduledLoads = LSQUnit::stats.rescheduledLoads.value();
            if (curRescheduledLoads > prevRescheduledLoads) {
                mockIQ->recordCPReschedule(callCycle, seq_num);
            }
        }

        // 如果指令已经在 Gem5 中执行过了，跳过 fault 比较
        if (!inst->isExecuted()) {
            // 更新 load 地址（如果 effAddrValid）
            if (inst->effAddrValid()) {
                pymtl3_update_load_addr(pymtl3LSQ, seq_num, inst->effAddr, inst->effSize);
            }

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
        
        // 对比 IQ 调用记录
        compareIQCalls();

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
        
        // 获取 Store 数据
        const uint8_t* store_data = nullptr;
        bool is_all_zeros = false;
        if (sq_idx >= 0 && sq_idx < storeQueue.capacity()) {
            auto& sq_entry = storeQueue[sq_idx];
            if (sq_entry.valid()) {
                store_data = reinterpret_cast<const uint8_t*>(sq_entry.data());
                is_all_zeros = sq_entry.isAllZeros();
            }
        }
        
        // 1. 更新 PyMTL3 中的 store 地址和数据
        pymtl3_update_store_addr(pymtl3LSQ, seq_num, ea, size, 
                                 store_data, size, is_all_zeros);
        
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
    uint64_t callCycle = curTick();

    bool wasStalled = LSQUnit::isStalled();

    // 调用基类实现（Gem5）
    LSQUnit::writebackStores();

    // 检测 C++ 侧的 replay 调用（stall 解除意味着 replay 发生）
    // 注意：无法直接获取被 replay 的 load 的 seq_num，
    // 因为 stallingLoadIdx 是 private 的
    if (mockIQ && wasStalled && !LSQUnit::isStalled()) {
        mockIQ->recordCPReplay(callCycle, 0);
    }

    // 对比 IQ 调用记录
    if (pymtl3Available && pymtl3LSQ) {
        compareIQCalls();
    }

    // PyMTL3 不需要显式写回
}

void
LSQUnitComparison::writeback(const DynInstPtr &inst, PacketPtr pkt)
{
    // 记录 C++ 端的 writeback 调用
    WritebackRecord record;
    record.cycle = curTick();
    record.seqNum = inst->seqNum;
    record.fault = (inst->fault != NoFault) ? 1 : 0;
    
    // 如果是 load 且有数据，复制数据
    if (inst->isLoad() && pkt && pkt->hasData()) {
        record.hasData = true;
        const uint8_t* dataPtr = pkt->getPtr<uint8_t>();
        uint32_t dataSize = pkt->getSize();
        record.data.assign(dataPtr, dataPtr + dataSize);
    } else {
        record.hasData = false;
    }
    
    cppWritebackCalls.push(record);

    // 调用基类实现
    LSQUnit::writeback(inst, pkt);
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
    
    // 获取 request 和 inst
    LSQRequest *request = dynamic_cast<LSQRequest *>(pkt->senderState);
    DynInstPtr inst = request->instruction();
    bool needWB = request->needWBToRegister();
    PacketPtr mainPkt = request->mainPacket();
    bool isSquashed = inst->isSquashed();
    
    // 调试输出已禁用

    // 1. 记录当前 cycle (使用 PyMTL3 的 cycle，与 PyMTL3 同步)
    uint64_t callCycle = 0;
    if (pymtl3Available && pymtl3LSQ) {
        callCycle = pymtl3_get_cycle(pymtl3LSQ);
    } else {
        callCycle = curTick();  // 回退到 tick（不应该发生）
    }

    // 2. 记录 C++ 的 DCache 调用
    if (mockDCache && pkt) {
        mockDCache->recordCPCall(callCycle, pkt, "completeDataAccess");
    }

    // 3. 发送 DCache 响应给 PyMTL3（如果是 Load 且需要写回）
    if (pymtl3Available && pymtl3LSQ && inst->isLoad() && needWB && !isSquashed) {
        // 获取响应数据
        const uint8_t* data = pkt->getConstPtr<uint8_t>();
        uint32_t data_size = pkt->getSize();
        
        pymtl3_send_dcache_resp(pymtl3LSQ, inst->seqNum, data, data_size);
    }

    // 4. 调用基类实现（Gem5）
    LSQUnit::completeDataAccess(pkt);
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
    // 调试输出已禁用

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
        mockDCache->recordCPCall(callCycle, data_pkt, "sendTimingReq", seqNum);
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
LSQUnitComparison::compareIQCalls()
{
    if (!mockIQ) {
        return;
    }

    // 对比 Replay 调用
    while (mockIQ->hasPendingCPReplays() || mockIQ->hasPendingPyMTL3Replays()) {
        IQCallRecord cpRecord, pyRecord;
        bool hasCP = mockIQ->getNextCPReplay(cpRecord);
        bool hasPy = mockIQ->getNextPyMTL3Replay(pyRecord);

        if (hasCP && hasPy) {
            std::string reason;
            if (!MockIQPort::compareCalls(cpRecord, pyRecord, reason)) {
                logMismatch("IQ.replay", reason);
            }
        } else if (hasCP) {
            logMismatch("IQ.replay",
                csprintf("C++ has replay but PyMTL3 does not [sn=%d]",
                         cpRecord.seqNum));
        } else if (hasPy) {
            logMismatch("IQ.replay",
                csprintf("PyMTL3 has replay but C++ does not [sn=%d]",
                         pyRecord.seqNum));
        }
    }

    // 对比 Reschedule 调用
    while (mockIQ->hasPendingCPReschedules() || mockIQ->hasPendingPyMTL3Reschedules()) {
        IQCallRecord cpRecord, pyRecord;
        bool hasCP = mockIQ->getNextCPReschedule(cpRecord);
        bool hasPy = mockIQ->getNextPyMTL3Reschedule(pyRecord);

        if (hasCP && hasPy) {
            std::string reason;
            if (!MockIQPort::compareCalls(cpRecord, pyRecord, reason)) {
                logMismatch("IQ.reschedule", reason);
            }
        } else if (hasCP) {
            logMismatch("IQ.reschedule",
                csprintf("C++ has reschedule but PyMTL3 does not [sn=%d]",
                         cpRecord.seqNum));
        } else if (hasPy) {
            logMismatch("IQ.reschedule",
                csprintf("PyMTL3 has reschedule but C++ does not [sn=%d]",
                         pyRecord.seqNum));
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
    
    // 检查 CPU 指针是否有效
    if (!cpuPtr) {
        return vaddr;
    }
    
    // 获取 ThreadContext 用于转换
    gem5::ThreadContext *tc = nullptr;
    try {
        tc = cpuPtr->tcBase(threadId);
    } catch (...) {
        return vaddr;
    }
    
    if (!tc) {
        return vaddr;
    }
    
    // 在SE模式下，TLB需要Process指针
    // 检查ThreadContext是否有有效的Process
    if (!tc->getProcessPtr()) {
        return vaddr;
    }
    
    // 获取 MMU
    BaseMMU *mmu = tc->getMMUPtr();
    if (!mmu) {
        // 回退到 getMMUPtr()
        mmu = getMMUPtr();
    }
    
    if (!mmu) {
        return vaddr;
    }
    
    // 在SE模式下，某些地址可能导致TLB崩溃
    // 添加地址范围检查，避免访问无效地址
    // 通常用户空间地址在 0x0000_0000_0000_0000 到 0x0000_7FFF_FFFF_FFFF
    // 或者内核空间地址在 0xFFFF_8000_0000_0000 以上
    
    // 简化处理：对于SE模式下的模拟，使用identity mapping
    // 这样可以避免TLB转换的复杂性
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
        return vaddr;
    }
    
    if (fault != NoFault) {
        // 转换失败，返回虚拟地址
        return vaddr;
    }
    
    // 获取转换后的物理地址
    if (req->hasPaddr()) {
        Addr paddr = req->getPaddr();
        return paddr;
    } else {
        return vaddr;
    }
    */
}

void
LSQUnitComparison::notifyPyMTL3WritebackCall(uint64_t cycle, uint64_t seqNum,
                                             bool hasData, const std::vector<uint8_t>& data,
                                             int fault)
{
    WritebackRecord record;
    record.cycle = cycle;
    record.seqNum = seqNum;
    record.hasData = hasData;
    record.data = data;
    record.fault = fault;
    
    pymtl3WritebackCalls.push(record);
}

void
LSQUnitComparison::notifyPyMTL3IQCall(uint64_t cycle, uint64_t seqNum,
                                      const std::string& method)
{
    if (mockIQ) {
        if (method == "replay") {
            mockIQ->recordPyMTL3Replay(cycle, seqNum);
        } else if (method == "reschedule") {
            mockIQ->recordPyMTL3Reschedule(cycle, seqNum);
        }
    }
}

// ===== 异步TLB转换实现 =====

void
LSQUnitComparison::handleTLBReq(uint64_t seq_num, Addr vaddr,
                                 uint32_t size, bool is_load)
{
    uint64_t callCycle = curTick();

    // 记录 C++ TLB 请求
    if (mockTLB) {
        mockTLB->recordCPTLBReq(callCycle, vaddr, size, is_load, seq_num);
    }

    // 获取 ThreadContext 和 MMU
    if (!cpuPtr) {
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    gem5::ThreadContext* tc = nullptr;
    try {
        tc = cpuPtr->tcBase(threadId);
    } catch (...) {
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    if (!tc) {
        sendTLBResp(seq_num, 0, 1);  // Send fault
        return;
    }
    
    BaseMMU* mmu = tc->getMMUPtr();
    if (!mmu) {
        mmu = getMMUPtr();
    }
    
    if (!mmu) {
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
    uint64_t callCycle = curTick();

    // 记录 C++ TLB resp
    int fault_code = (fault == NoFault) ? 0 : 1;
    if (mockTLB) {
        mockTLB->recordCPTLBResp(callCycle, seq_num, paddr, fault_code);
    }

    // 从 outstanding 列表中移除（如果还在的话）
    for (auto it = outstandingTLBReqs.begin(); it != outstandingTLBReqs.end(); ++it) {
        if ((*it)->getSeqNum() == seq_num) {
            outstandingTLBReqs.erase(it);
            break;
        }
    }

    // 尝试对比并驱动 PyMTL3
    if (mockTLB && pymtl3Available) {
        compareAndDriveTLBResp(seq_num, paddr, fault_code);
    }
}

void
LSQUnitComparison::compareAndDriveTLBResp(uint64_t seq_num, Addr paddr, int fault)
{
    // 检查是否有等待的 PyMTL3 TLB resp
    TLBCallRecord pymtl3Resp;
    if (!mockTLB->getNextPyMTL3TLBResp(pymtl3Resp)) {
        return;
    }


    // 对比 PyMTL3 resp 和 C++ resp
    std::string reason;
    TLBCallRecord cppResp;
    cppResp.seqNum = seq_num;
    cppResp.paddr = paddr;
    cppResp.fault = fault;
    cppResp.methodName = "translateResp";

    if (!MockTLBPort::compareCalls(cppResp, pymtl3Resp, reason)) {
        logMismatch("TLB.resp", reason);
    }

    // 驱动 PyMTL3
    sendTLBResp(seq_num, paddr, fault);
}

void
LSQUnitComparison::sendTLBResp(uint64_t seq_num, Addr paddr, int fault)
{
    if (tlbRespCallback) {
        tlbRespCallback(seq_num, paddr, fault);
    }
}

void
LSQUnitComparison::recordPyMTL3TLBReq(uint64_t cycle, Addr vaddr, uint32_t size,
                                      bool isLoad, uint64_t seqNum)
{
    if (mockTLB) {
        mockTLB->recordPyMTL3TLBReq(cycle, vaddr, size, isLoad, seqNum);
    }
}

void
LSQUnitComparison::recordPyMTL3Replay(uint64_t cycle, uint64_t seqNum)
{
    if (mockIQ) {
        mockIQ->recordPyMTL3Replay(cycle, seqNum);
    }
}

void
LSQUnitComparison::recordPyMTL3Reschedule(uint64_t cycle, uint64_t seqNum)
{
    if (mockIQ) {
        mockIQ->recordPyMTL3Reschedule(cycle, seqNum);
    }
}

void
LSQUnitComparison::handleReplayReq(uint64_t seqNum)
{
    // C++ 侧的 replay 记录已通过 writebackStores 中的检测逻辑完成
    // 此方法保留作为备用入口
}

void
LSQUnitComparison::handleRescheduleReq(uint64_t seqNum)
{
    // C++ 侧的 reschedule 记录已通过 executeLoad 中的检测逻辑完成
    // 此方法保留作为备用入口
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
