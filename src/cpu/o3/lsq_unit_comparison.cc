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
#include "debug/PyMTL3.hh"
#include "mem/request.hh"
#include <iostream>

// 全局变量，用于TLB回调访问当前实例
static gem5::o3::LSQUnitComparison* g_currentLSQUnitForTLB = nullptr;

namespace gem5
{

namespace o3
{

// 全局变量，用于回调函数访问当前实例
// 注意：这个变量在 lsq_unit_comparison.cc 中定义，在 pymtl3_lsq_bindings.cc 中使用 extern 声明
LSQUnitComparison* g_currentLSQUnitComparison = nullptr;

#if LSQ_COMPARISON_MODE

LSQUnitComparison::LSQUnitComparison(uint32_t lqEntries, uint32_t sqEntries)
    : LSQUnit(lqEntries, sqEntries),
      pymtl3LSQ(nullptr),
      pymtl3Available(false),
      mismatchCount(0),
      stallingLoadSeqNum(0),
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
    std::cout << "[LSQComparison] LSQUnitComparison constructor called" << std::endl;
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
    std::cout << "[LSQComparison-DEBUG] pymtl3TLBReqCallback called: sn=" << seq_num 
              << ", vaddr=0x" << std::hex << vaddr << std::dec 
              << ", size=" << size << ", is_load=" << is_load << std::endl;
    if (g_currentLSQUnitForTLB) {
        // 调用 LSQUnitComparison::handleTLBReq 发起异步 TLB 转换
        g_currentLSQUnitForTLB->handleTLBReq(seq_num, vaddr, size, is_load);
    } else {
        std::cout << "[LSQComparison-DEBUG] g_currentLSQUnitForTLB is null!" << std::endl;
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

// Helper function to calculate virtual address for load instructions
// This is called before TLB translation to get vaddr for PyMTL3
Addr
LSQUnitComparison::calculateLoadVaddr(const DynInstPtr &inst)
{
    // Try to extract vaddr from instruction
    // For RISC-V: vaddr = base_reg + sext(offset)
    
    // Check if this is a load instruction
    if (!inst->isLoad()) {
        return 0;
    }
    
    // Get the static instruction
    auto staticInst = inst->staticInst;
    if (!staticInst) {
        return 0;
    }
    
    // For RISC-V, we need to get:
    // 1. Base register value (Rs1)
    // 2. Offset (imm12 from machInst)
    
    // Try to read base register value
    // Load instructions have at least 1 source register (base address)
    Addr base_val = 0;
    if (inst->numSrcRegs() > 0) {
        // Get the first source register value (Rs1 for RISC-V load)
        // Note: This assumes the first source reg is the base address
        RegVal reg_val = inst->getRegOperand(staticInst.get(), 0);
        base_val = static_cast<Addr>(reg_val);
    }
    
    // Try to extract offset from instruction encoding
    // For RISC-V I-type load instructions: offset = machInst[31:20]
    int64_t offset = 0;
    
    // Access machInst through the RiscvStaticInst
    // We need to use the getMachInst method or access it directly
    // Since RiscvStaticInst has public machInst member
    
    // Check if we can get the extended machine instruction
    uint64_t machInst = staticInst->getEMI();
    
    // Extract 12-bit immediate from bits [31:20]
    // Sign extend to 64 bits
    int32_t imm12 = (machInst >> 20) & 0xFFF;
    if (imm12 & 0x800) {
        // Sign extend
        imm12 |= 0xFFFFF000;
    }
    offset = static_cast<int64_t>(imm12);
    
    DPRINTF(PyMTL3, "calculateLoadVaddr: base=0x%llx, offset=%d (0x%x), raw_inst=0x%x\n",
            base_val, offset, imm12, (uint32_t)machInst);
    
    // Calculate vaddr with sign extension
    Addr vaddr = base_val + static_cast<Addr>(offset);
    
    return vaddr;
}

// Helper function to calculate virtual address for store instructions
// This is called before TLB translation to get vaddr for PyMTL3
// For RISC-V store instructions: vaddr = base_reg + sext(offset)
// Store uses S-type format: offset[11:5] in machInst[31:25], offset[4:0] in machInst[11:7]
Addr
LSQUnitComparison::calculateStoreVaddr(const DynInstPtr &inst)
{
    // Try to extract vaddr from instruction
    // For RISC-V: vaddr = base_reg + sext(offset)
    
    // Check if this is a store instruction
    if (!inst->isStore()) {
        return 0;
    }
    
    // Get the static instruction
    auto staticInst = inst->staticInst;
    if (!staticInst) {
        return 0;
    }
    
    // For RISC-V store, we need to get:
    // 1. Base register value (Rs1) - first source reg
    // 2. Offset (S-type immediate from machInst)
    
    // Try to read base register value
    // Store instructions have at least 1 source register (base address)
    Addr base_val = 0;
    if (inst->numSrcRegs() > 0) {
        // Get the first source register value (Rs1 for RISC-V store)
        RegVal reg_val = inst->getRegOperand(staticInst.get(), 0);
        base_val = static_cast<Addr>(reg_val);
    }
    
    // Try to extract offset from instruction encoding
    // For RISC-V S-type store instructions:
    // offset[11:5] = machInst[31:25]
    // offset[4:0]  = machInst[11:7]
    int64_t offset = 0;
    
    // Check if we can get the extended machine instruction
    uint64_t machInst = staticInst->getEMI();
    
    // Extract 12-bit immediate from S-type format
    int32_t imm12 = ((machInst >> 25) & 0x7F) << 5 |   // imm[11:5] from machInst[31:25]
                    ((machInst >> 7) & 0x1F);           // imm[4:0] from machInst[11:7]
    
    // Sign extend to 64 bits
    if (imm12 & 0x800) {
        imm12 |= 0xFFFFF000;
    }
    offset = static_cast<int64_t>(imm12);
    
    DPRINTF(PyMTL3, "calculateStoreVaddr: base=0x%llx, offset=%d (0x%x), raw_inst=0x%x\n",
            base_val, offset, imm12, (uint32_t)machInst);
    
    // Calculate vaddr with sign extension
    Addr vaddr = base_val + static_cast<Addr>(offset);
    
    return vaddr;
}

// ==== 包装方法实现 ====

void
LSQUnitComparison::insertLoad(const DynInstPtr &load_inst)
{
    DPRINTF(PyMTL3, "insertLoad sn=%llu pc=%llx\n", load_inst->seqNum, load_inst->pcState().instAddr());
    
    LSQUnit::insertLoad(load_inst);

    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = load_inst->seqNum;
        Addr pc = load_inst->pcState().instAddr();
        Addr ea = 0;
        // Use inst->effSize if available, otherwise default to 8
        uint32_t size = load_inst->effSize > 0 ? load_inst->effSize : 8;
        
        int fault = (load_inst->getFault() != NoFault) ? 1 : 0;
        bool is_strictly_ordered = load_inst->strictlyOrdered();

        DPRINTF(PyMTL3, "insertLoad sn=%llu: size=%u (effSize=%u)\n", seq_num, size, load_inst->effSize);

        pymtl3_insert_load(pymtl3LSQ, seq_num, pc, ea, size, fault, is_strictly_ordered);
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
        // Use inst->effSize if available, otherwise default to 8
        uint32_t size = store_inst->effSize > 0 ? store_inst->effSize : 8;
        
        // 获取指令的 fault 状态（来自之前流水线阶段，如 ITLB）
        int fault = (store_inst->getFault() != NoFault) ? 1 : 0;

        DPRINTF(PyMTL3, "insertStore sn=%llu: size=%u (effSize=%u)\n", seq_num, size, store_inst->effSize);

        // 获取 is_atomic 标志
        bool is_atomic = store_inst->isAtomic();
        
        pymtl3_insert_store(pymtl3LSQ, seq_num, pc, ea, size, fault, is_atomic);

        // 注意：队列状态比较在 LSQ::tick() 中统一进行
        // 以确保 PyMTL3 的 @update_ff 已经执行
    }
}

Fault
LSQUnitComparison::executeLoad(const DynInstPtr &inst)
{
    uint64_t callCycle = curTick();

    DPRINTF(PyMTL3, "executeLoad sn=%llu lqIdx=%d\n", inst->seqNum, inst->lqIdx);

    uint64_t prevRescheduledLoads = 0;
    if (mockIQ) {
        prevRescheduledLoads = LSQUnit::stats.rescheduledLoads.value();
    }

    bool wasStalled = LSQUnit::isStalled();

    uint32_t savedEffSize = inst->effSize;

    // 打印执行前的状态
    DPRINTF(PyMTL3, "executeLoad sn=%llu: BEFORE - executed=%d, translationDelayed=%d, effAddrValid=%d, strictlyOrdered=%d, isAtCommit=%d\n",
            inst->seqNum, inst->isExecuted(), inst->isTranslationDelayed(),
            inst->effAddrValid(), inst->strictlyOrdered(), inst->isAtCommit());
    DPRINTF(PyMTL3, "executeLoad sn=%llu: BEFORE - translationStarted=%d, translationCompleted=%d, hasRequest=%d, savedRequest=%p\n",
            inst->seqNum, inst->translationStarted(), inst->translationCompleted(),
            inst->hasRequest(), inst->savedRequest);

    Fault result = LSQUnit::executeLoad(inst);

    // 打印执行后的状态
    // Print request info if available
    if (inst->hasRequest() && inst->savedRequest) {
        DPRINTF(PyMTL3, "executeLoad sn=%llu: AFTER - request vaddr=0x%llx, size=%u\n",
                inst->seqNum, inst->savedRequest->mainReq()->getVaddr(),
                inst->savedRequest->mainReq()->getSize());
    }
    DPRINTF(PyMTL3, "executeLoad sn=%llu: AFTER - result=%s, executed=%d, translationDelayed=%d, effAddrValid=%d, rescheduledLoads=%llu\n",
            inst->seqNum, (result == NoFault ? "NoFault" : "Fault"),
            inst->isExecuted(), inst->isTranslationDelayed(), inst->effAddrValid(),
            LSQUnit::stats.rescheduledLoads.value() - prevRescheduledLoads);
    DPRINTF(PyMTL3, "executeLoad sn=%llu: AFTER - translationStarted=%d, translationCompleted=%d, hasRequest=%d, effAddr=%llx\n",
            inst->seqNum, inst->translationStarted(), inst->translationCompleted(),
            inst->hasRequest(), inst->effAddr);

    if (pymtl3Available && pymtl3LSQ) {
        InstSeqNum seq_num = inst->seqNum;
        int lq_idx = inst->lqIdx;

        if (mockIQ) {
            uint64_t curRescheduledLoads = LSQUnit::stats.rescheduledLoads.value();
            if (curRescheduledLoads > prevRescheduledLoads) {
                mockIQ->recordCPReschedule(callCycle, seq_num);
            }
        }

        if (!wasStalled && LSQUnit::isStalled()) {
            stallingLoadSeqNum = seq_num;
        }

        //if (!inst->isExecuted()) {
            // Get vaddr for PyMTL3 - use inst->effAddr if valid (this is the authoritative address)
            // Only calculate if effAddr is not yet available
            Addr vaddr;
            bool vaddrValid;
            if (inst->effAddrValid()) {
                vaddr = inst->effAddr;
                vaddrValid = true;
                DPRINTF(PyMTL3, "executeLoad sn=%llu: using inst->effAddr=0x%llx (valid=%d)\n",
                        seq_num, vaddr, inst->effAddrValid());
            } else if (inst->effAddr != 0) {
                // effAddrValid is false but effAddr is set (e.g., after partial coverage reschedule)
                // Use effAddr as it's the authoritative address from previous execution
                vaddr = inst->effAddr;
                vaddrValid = true;
                DPRINTF(PyMTL3, "executeLoad sn=%llu: using inst->effAddr=0x%llx (valid=%d, but non-zero)\n",
                        seq_num, vaddr, inst->effAddrValid());
            } else {
                vaddr = calculateLoadVaddr(inst);
                vaddrValid = true;
                DPRINTF(PyMTL3, "executeLoad sn=%llu: using calculated vaddr=0x%llx (effAddr not valid)\n",
                        seq_num, vaddr);
            }

            // 打印 C++ 的 SQ 状态用于与 PyMTL3 对比
            DPRINTF(PyMTL3, "executeLoad sn=%llu: C++ SQ state (head=%d, tail=%d, size=%d):\n",
                    seq_num, storeQueue.head(), storeQueue.tail(), storeQueue.size());
            int sq_count = 0;
            for (auto it = storeQueue.begin(); it != storeQueue.end(); ++it) {
                if (it->valid()) {
                    auto sq_inst = it->instruction();
                    Addr sq_addr = sq_inst->effAddrValid() ? sq_inst->effAddr : 0;
                    DPRINTF(PyMTL3, "  C++ SQ[%d]: sn=%llu, vaddr=0x%llx, size=%u, completed=%d\n",
                            sq_count, sq_inst->seqNum, sq_addr, it->size(), it->completed());
                    sq_count++;
                }
            }
            DPRINTF(PyMTL3, "executeLoad sn=%llu: C++ SQ total valid entries: %d\n", seq_num, sq_count);

            // Get request size from C++ request if available
            int request_size = savedEffSize;
            if (inst->hasRequest() && inst->savedRequest) {
                request_size = inst->savedRequest->mainReq()->getSize();
                DPRINTF(PyMTL3, "executeLoad sn=%llu: using request_size=%d (from request)\n",
                        seq_num, request_size);
            }
       
            // 如果 C++ TLB 翻译已完成，驱动 TLB 响应到 PyMTL3
            if (inst->translationCompleted()) {
                Addr paddr = inst->physEffAddr;
                int fault_code = (inst->fault == NoFault) ? 0 : 1;
                DPRINTF(PyMTL3, "executeLoad sn=%llu: C++ TLB completed, driving response to PyMTL3 paddr=0x%llx fault=%d\n",
                        seq_num, paddr, fault_code);
                if (mockTLB && pymtl3Available) {
                    mockTLB->recordCPTLBResp(callCycle, seq_num, paddr, fault_code);
                    sendTLBResp(seq_num, paddr, fault_code);
                }
            }

            // Pass all necessary information directly to execute_load
            // including strictlyOrdered, isAtCommit, vaddr, and effSize
            // vaddr is always computed from instruction, not relying on savedEffAddr
            int py_fault = pymtl3_execute_load(pymtl3LSQ,
                (uint64_t)seq_num, (int)lq_idx,
                (bool)inst->strictlyOrdered(), (bool)inst->isAtCommit(),
                (uint64_t)vaddr, (bool)vaddrValid,
                (int)savedEffSize, request_size);

            if ((result == NoFault && py_fault != 0) ||
                (result != NoFault && py_fault == 0)) {
                std::cout << "[LSQComparison] Mismatch in executeLoad fault: "
                          << "Gem5=" << (result == NoFault ? "NoFault" : "Fault")
                          << ", PyMTL3=" << (py_fault == 0 ? "NoFault" : "Fault") << " [sn=" << seq_num << ", lq_idx=" << lq_idx << "]"
                          << std::endl;
            }

        //}
        
        compareIQCalls();

        compareStatistics();
    }

    return result;
}

Fault
LSQUnitComparison::executeStore(const DynInstPtr &inst)
{
    InstSeqNum seq_num = inst->seqNum;
    int sq_idx = inst->sqIdx;
    
    // 记录initiateAcc前的translationDelayed状态
    // 这个状态决定了C++是否会检查size==0
    bool was_translation_delayed_before = inst->isTranslationDelayed();
    bool read_predicate_before = inst->readPredicate();
        // Get vaddr for PyMTL3 - use inst->effAddr if valid (this is the authoritative address)
        // Only calculate if effAddr is not yet available
        Addr vaddr;
        bool vaddrValid;
        
        // Get Store data and size from store queue AFTER executeStore
        // because executeStore (via initiateAcc) sets up the store queue entry
        static uint8_t zero_data[32] = {0};  // Static zero data for null case
        const uint8_t* store_data = zero_data;
        bool is_all_zeros = true;
        uint32_t actual_data_size = 0;
        if (sq_idx >= 0 && sq_idx < storeQueue.capacity()) {
            auto& sq_entry = storeQueue[sq_idx];
            if (sq_entry.valid()) {
                const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(sq_entry.data());
                if (raw_data != nullptr) {
                    store_data = raw_data;
                    is_all_zeros = sq_entry.isAllZeros();
                }
                // Always get the actual size from store queue (may be 0 if fault occurred)
                // This is the true size after initiateAcc, used for fault detection
                actual_data_size = sq_entry.size();
                DPRINTF(PyMTL3, "executeStore sn=%llu: store_data=%p, sq_entry.size()=%u\n", 
                        seq_num, raw_data, actual_data_size);
            }
        }

        if (inst->effAddrValid()) {
            vaddr = inst->effAddr;
            vaddrValid = true;
            actual_data_size = inst->effSize > 0 ? inst->effSize : 8;
            DPRINTF(PyMTL3, "executeStore sn=%llu: using inst->effAddr=0x%llx (valid=%d)\n",
                    seq_num, vaddr, inst->effAddrValid());
        } else {
            vaddr = calculateStoreVaddr(inst);
            vaddrValid = false;
            DPRINTF(PyMTL3, "executeStore sn=%llu: using calculated vaddr=0x%llx (effAddr not valid)\n",
                    seq_num, vaddr);
        }

    // 调用基类实现（Gem5）
    Fault result = LSQUnit::executeStore(inst);

    // 拦截并记录 C++ executeStore 内部的 TLB 请求
    // 如果发起了 TLB 请求（translationStarted 且 savedRequest 被设置）
    if (inst->translationStarted() && inst->savedRequest && mockTLB) {
        Addr cpp_tlb_vaddr = inst->savedRequest->mainReq()->getVaddr();
        uint32_t cpp_tlb_size = inst->savedRequest->mainReq()->getSize();
        
        // 记录 C++ TLB 请求（用于和 PyMTL3 对比）
        // 使用专门的记录函数，标记为 C++ 内部发起的请求
        mockTLB->recordCPInternalTLBReq(curTick(), cpp_tlb_vaddr, cpp_tlb_size, 
                                        false, seq_num);  // false = store
        
        DPRINTF(PyMTL3, "executeStore sn=%llu: recorded C++ internal TLB req "
                "vaddr=0x%llx, size=%u\n", seq_num, cpp_tlb_vaddr, cpp_tlb_size);
    }

            
        // 如果 C++ TLB 翻译已完成，驱动 TLB 响应到 PyMTL3
        if (inst->translationCompleted()) {
            Addr paddr = inst->physEffAddr;
            int fault_code = (inst->fault == NoFault) ? 0 : 1;
            DPRINTF(PyMTL3, "executeStore sn=%llu: C++ TLB completed, driving response to PyMTL3 paddr=0x%llx fault=%d\n",
                    seq_num, paddr, fault_code);
            if (mockTLB && pymtl3Available) {
                mockTLB->recordCPTLBResp(curTick(), seq_num, paddr, fault_code);
                sendTLBResp(seq_num, paddr, fault_code);
            }
        }

            // Get request size from C++ request if available
            if (inst->hasRequest() && inst->savedRequest) {
                actual_data_size = inst->savedRequest->mainReq()->getSize();
                vaddr = inst->savedRequest->mainReq()->getVaddr();
                DPRINTF(PyMTL3, "executeStore sn=%llu: using request_size=%d  vaddr=0x%llx (from request)\n",
                        seq_num, actual_data_size, vaddr);
            }

    // 如果 PyMTL3 可用，更新 store 地址并调用 PyMTL3 实现
    if (pymtl3Available && pymtl3LSQ) {
        // Pass size info, translation state, and predicate state to PyMTL3
        // PyMTL3 uses these to detect size==0 faults (same as C++ logic)
        // - actual_size: size after initiateAcc (may be 0 if fault occurred)
        // - was_translation_delayed_before: translationDelayed BEFORE initiateAcc (if true, C++ skips size check)
        // - read_predicate_before: readPredicate BEFORE initiateAcc (if false, C++ skips size check)
        
        DPRINTF(PyMTL3, "executeStore sn=%llu: store_data=%p, actual_size=%u, result=%s, wasTranslationDelayed=%d, wasReadPredicate=%d, nowTranslationDelayed=%d\n",
                seq_num, store_data, actual_data_size,
                (result == NoFault ? "NoFault" : "Fault"), was_translation_delayed_before, read_predicate_before,
                inst->isTranslationDelayed());
        
        // Call PyMTL3 execute_store with all necessary information
        // including vaddr (or effAddr), data, and flags
        // When effAddrValid is false, vaddr contains the virtual address for PyMTL3 TLB
        // Pass all info for PyMTL3 to detect size==0 fault
        int py_fault = pymtl3_execute_store(pymtl3LSQ, seq_num, sq_idx,
                                            vaddr, vaddrValid, actual_data_size,
                                            store_data, is_all_zeros,
                                            was_translation_delayed_before, read_predicate_before);
        
        // 对比结果
        if ((result == NoFault && py_fault != 0) || 
            (result != NoFault && py_fault == 0)) {
            std::cout << "[LSQComparison] Mismatch in executeStore fault: sn=" << seq_num
                      << ", Gem5=" << (result == NoFault ? "NoFault" : "Fault")
                      << ", PyMTL3=" << (py_fault == 0 ? "NoFault" : "Fault") << std::endl;
        }

        // 对比 C++ 内部 TLB 请求和 PyMTL3 TLB 请求
        compareCPInternalAndPyMTL3TLBReq(seq_num); 
        
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
    // 记录提交前的状态
    size_t loads_before = loadQueue.size();

    // 调用基类实现（Gem5）
    LSQUnit::commitLoads(youngest_inst);

    // 计算提交了多少个 loads
    size_t loads_committed = loads_before - loadQueue.size();

    DPRINTF(PyMTL3, "commitLoads: youngest_inst=%llu, loads_before=%lu, loads_after=%lu, committed=%lu\n",
            youngest_inst, (unsigned long)loads_before, (unsigned long)loadQueue.size(), (unsigned long)loads_committed);

    // 如果 PyMTL3 可用，调用 PyMTL3 实现相同次数
    if (pymtl3Available && pymtl3LSQ) {
        DPRINTF(PyMTL3, "commitLoads: calling pymtl3_commit_load %lu times\n", (unsigned long)loads_committed);
        for (size_t i = 0; i < loads_committed; ++i) {
            pymtl3_commit_load(pymtl3LSQ);
        }
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

    // First, let PyMTL3 process store writebacks to ensure synchronization
    // This ensures PyMTL3 and C++ process stores in the same cycle
    if (pymtl3Available && pymtl3LSQ) {
        pymtl3_process_store_writebacks(pymtl3LSQ);
    }

    LSQUnit::writebackStores();

    if (mockIQ && wasStalled && !LSQUnit::isStalled()) {
        mockIQ->recordCPReplay(callCycle, stallingLoadSeqNum);
        stallingLoadSeqNum = 0;
    }

    if (pymtl3Available && pymtl3LSQ) {
        compareIQCalls();
    }
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

    // 3. 发送 DCache 响应给 PyMTL3（Load 或 Store）
    // Load: needWB 为 true 时写回数据
    // Store: 总是需要通知完成（即使 needWB 为 false），以便 PyMTL3 从 queue 中移除
    if (pymtl3Available && pymtl3LSQ && !isSquashed) {
        if (inst->isLoad() && needWB) {
            // Load: 获取响应数据
            const uint8_t* data = pkt->getConstPtr<uint8_t>();
            uint32_t data_size = pkt->getSize();
            pymtl3_send_dcache_resp(pymtl3LSQ, inst->seqNum, data, data_size, false);
        } else if (inst->isStore()) {
            // Store: 通知完成，以便 PyMTL3 从 queue 中移除 entry
            // Store 的 needWBToRegister() 通常为 false，但仍需要通知 PyMTL3
            pymtl3_send_dcache_resp(pymtl3LSQ, inst->seqNum, nullptr, 0, true);
        }
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
    std::cout << "[LSQComparison] Mismatch in " << methodName
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

    // 对比 DCache 请求
    compareDCacheCalls();
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
LSQUnitComparison::compareDCacheCalls()
{
    if (!mockDCache) {
        return;
    }

    // Step 1: 遍历 PyMTL3 的请求，在 C++ 请求中查找相同 seqNum 的请求并比较
    DCacheCallRecord pyRecord;
    while (mockDCache->getNextPyMTL3Call(pyRecord)) {
        DCacheCallRecord cpRecord;
        
        // 在 C++ 请求中查找相同 seqNum 的请求
        if (mockDCache->findAndRemoveCPCallBySeqNum(pyRecord.seqNum, cpRecord)) {
            // 找到了匹配的 C++ 请求，进行比较
            std::string reason;
            if (MockDCachePort::compareCalls(cpRecord, pyRecord, reason)) {
                // 比较通过
                DPRINTF(PyMTL3, "[DCacheCompare] sn=%lu: Match OK\n", pyRecord.seqNum);
            } else {
                // 比较失败，记录不匹配
                logMismatch("DCache.req", reason);
                std::cout << "[LSQComparison] Mismatch in DCache req: sn="
                          << pyRecord.seqNum << ", " << reason << std::endl;
            }
        } else {
            // 没有找到匹配的 C++ 请求
            logMismatch("DCache.req",
                csprintf("PyMTL3 has DCache req but C++ does not [sn=%lu, addr=0x%lx, isWrite=%d]",
                         pyRecord.seqNum, pyRecord.addr, pyRecord.isWrite));
            std::cout << "[LSQComparison] PyMTL3 DCache req not matched: sn="
                      << pyRecord.seqNum << ", addr=0x" << std::hex << pyRecord.addr
                      << std::dec << ", isWrite=" << pyRecord.isWrite << std::endl;
        }
    }

    // Step 2: 清理超时的请求（超过 200 个 cycle 未匹配）
    const uint64_t timeoutCycles = 200;
    int expiredCount = mockDCache->removeExpiredCalls(curTick(), timeoutCycles);
    if (expiredCount > 0) {
        std::cout << "[LSQComparison] Removed " << expiredCount << " expired DCache requests (timeout=" << timeoutCycles << " cycles)" << std::endl;
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
    
    // 记录 PyMTL3 的 TLB 请求到 mockTLB，以便后续对比
    if (mockTLB) {
        mockTLB->recordPyMTL3TLBReq(callCycle, vaddr, size, is_load, seq_num);
    }
    // 首先检查是否有缓存的TLB响应（C++转换已完成但PyMTL3请求未到）
    std::cout << "[LSQComparison-DEBUG] handleTLBReq sn=" << seq_num << ": checking " << cachedTLBResps.size() << " cached responses" << std::endl;
    for (auto it = cachedTLBResps.begin(); it != cachedTLBResps.end(); ++it) {
        if (it->seqNum == seq_num) {
            std::cout << "[LSQComparison-DEBUG] handleTLBReq sn=" << seq_num << ": Found cached TLB response, sending immediately" << std::endl;
            DPRINTF(PyMTL3, "handleTLBReq sn=%llu: Found cached TLB response, sending immediately\n", seq_num);
            // 发送缓存的响应给PyMTL3
            sendTLBResp(it->seqNum, it->paddr, it->fault);
            // 从缓存中移除
            cachedTLBResps.erase(it);
            // 记录到mockTLB用于对比
            if (mockTLB) {
                mockTLB->recordPyMTL3TLBResp(callCycle, seq_num, it->paddr, it->fault);
            }
            return;
        }
    }
    std::cout << "[LSQComparison-DEBUG] handleTLBReq sn=" << seq_num << ": no cached response found, continuing" << std::endl;

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
    
    // 注意：不再创建 PyTLBRequest 和启动TLB转换
    // C++ 基类 executeStore/executeLoad 已经触发了真实的 TLB 访问
    // 我们只需要等待 C++ 的 TLB 响应到达后通过 sendTLBResp 驱动给 PyMTL3
    DPRINTF(PyMTL3, "handleTLBReq sn=%llu: C++ TLB not completed yet, waiting for response\n", seq_num);
}

void
LSQUnitComparison::sendTLBResp(uint64_t seq_num, Addr paddr, int fault)
{
    // Check if PyMTL3 has sent a TLB request with this seqNum
    if (!mockTLB || !mockTLB->hasPyMTL3TLBReqWithSeqNum(seq_num)) {
        // PyMTL3 request not received yet, cache the response
        DPRINTF(PyMTL3, "sendTLBResp sn=%llu: PyMTL3 req not ready, caching response\n", seq_num);
        cachedTLBResps.emplace_back(seq_num, paddr, fault, curTick());
        return;
    }

    // PyMTL3 request received, send the response
    if (tlbRespCallback) {
        tlbRespCallback(seq_num, paddr, fault);
    }
}

void
LSQUnitComparison::compareCPInternalAndPyMTL3TLBReq(uint64_t seq_num)
{
    if (!mockTLB || !pymtl3Available) {
        return;
    }

    // 获取 C++ 内部 TLB 请求
    TLBCallRecord cppReq;
    if (!mockTLB->getNextCPInternalTLBReq(cppReq)) {
        DPRINTF(PyMTL3, "[TLBCompare] sn=%llu: No C++ internal TLB req\n",
                seq_num);
        return;  // 没有 C++ 内部 TLB 请求
    }

    // 获取 PyMTL3 TLB 请求
    TLBCallRecord pymtl3Req;
    if (!mockTLB->getNextPyMTL3TLBReq(pymtl3Req)) {
        // PyMTL3 没有对应的请求，可能是 C++ 独有的
        DPRINTF(PyMTL3, "[TLBCompare] sn=%llu: C++ internal TLB req but no PyMTL3 req\n",
                seq_num);
        return;
    }

    // 对比请求
    std::string reason;
    if (!MockTLBPort::compareCalls(cppReq, pymtl3Req, reason)) {
        std::cout << "[LSQComparison] Mismatch in TLB req (C++ internal vs PyMTL3): sn=" 
                  << seq_num << ", " << reason << std::endl;
        DPRINTF(PyMTL3, "[TLBCompare] sn=%llu: MISMATCH - %s\n", seq_num, reason);
    } else {
        DPRINTF(PyMTL3, "[TLBCompare] sn=%llu: TLB req match (C++ internal vs PyMTL3)\n",
                seq_num);
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
