/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * LSQUnitComparison - Comparison wrapper for Gem5 LSQUnit and PyMTL3 LSQUnitCL
 *
 * This class provides a comparison framework that wraps both the original
 * Gem5 LSQUnit and the PyMTL3 LSQUnitCL, invoking both in parallel and
 * comparing their results.
 *
 * Compilation macro: LSQ_COMPARISON_MODE
 *   - Define LSQ_COMPARISON_MODE=1 to enable comparison mode
 *   - Undefine or set to 0 to use original LSQUnit
 */

#ifndef __CPU_O3_LSQ_UNIT_COMPARISON_HH__
#define __CPU_O3_LSQ_UNIT_COMPARISON_HH__

#include "cpu/o3/lsq_unit.hh"
#include "cpu/o3/dyn_inst_ptr.hh"
#include "cpu/o3/mock_dcache.hh"
#include "mem/packet.hh"

// 编译开关 - 启用对比模式
#ifndef LSQ_COMPARISON_MODE
#define LSQ_COMPARISON_MODE 1
#endif

namespace gem5
{

namespace o3
{

#if LSQ_COMPARISON_MODE

// pybind11 绑定函数前向声明 - 只在对比模式启用时声明
void init_pymtl3_module();
void* create_pymtl3_lsq(uint32_t lqEntries, uint32_t sqEntries);
bool pymtl3_insert_load(void* lsq, uint64_t seq_num, uint64_t pc, uint64_t ea, uint32_t size);
bool pymtl3_insert_store(void* lsq, uint64_t seq_num, uint64_t pc, uint64_t ea, uint32_t size);
int pymtl3_execute_load(void* lsq, int lq_idx);
int pymtl3_execute_store(void* lsq, int sq_idx);
void* pymtl3_commit_load(void* lsq);
int pymtl3_commit_stores(void* lsq, uint64_t youngest_sn);
int pymtl3_squash(void* lsq, uint64_t squash_sn);
int pymtl3_num_loads(void* lsq);
int pymtl3_num_stores(void* lsq);
uint64_t pymtl3_get_stat(void* lsq, const std::string &stat_name);
void pymtl3_set_dcache_callback(void* lsq,
    void (*callback)(uint64_t, uint64_t, uint32_t, bool,
                    const std::vector<uint8_t>&,
                    const std::string&));
uint64_t pymtl3_get_cycle(void* lsq);

/**
 * LSQUnitComparison - Simplified comparison wrapper
 *
 * This class inherits from LSQUnit and uses the base class as the Gem5 implementation.
 * It optionally creates a PyMTL3 LSQUnitCL instance for comparison.
 * If PyMTL3 is not available, it gracefully degrades to just using the base class.
 */
class LSQUnitComparison : public LSQUnit
{
  public:
    /**
     * Constructs an LSQUnitComparison.
     * @param lqEntries Number of load queue entries.
     * @param sqEntries Number of store queue entries.
     */
    LSQUnitComparison(uint32_t lqEntries, uint32_t sqEntries);

    /**
     * Destroys the LSQUnitComparison.
     */
    ~LSQUnitComparison();

    /**
     * Initializes the LSQ unit with the specified number of entries.
     */
    void init(CPU *cpu_ptr, IEW *iew_ptr, const BaseO3CPUParams &params,
              LSQ *lsq_ptr, unsigned id);

    /** Returns the name of the LSQ unit. */
    std::string name() const;

    // ==== 包装方法 - 并行调用和对比 ====

    /** Inserts a load instruction. */
    void insertLoad(const DynInstPtr &load_inst);

    /** Inserts a store instruction. */
    void insertStore(const DynInstPtr &store_inst);

    /** Executes a load instruction. */
    Fault executeLoad(const DynInstPtr &inst);

    /** Executes a store instruction. */
    Fault executeStore(const DynInstPtr &inst);

    /** Commits the head load. */
    void commitLoad();

    /** Commits loads older than a specific sequence number. */
    void commitLoads(InstSeqNum &youngest_inst);

    /** Commits stores older than a specific sequence number. */
    void commitStores(InstSeqNum &youngest_inst);

    /** Writes back stores. */
    void writebackStores();

    /** Squashes all instructions younger than a specific sequence number. */
    void squash(const InstSeqNum &squashed_num);

    /** Check if an incoming invalidate hits in the lsq. */
    void checkSnoop(PacketPtr pkt);

    /** Start stale translation flush. */
    void startStaleTranslationFlush();

    /** Check for stale translations. */
    bool checkStaleTranslations();

    /** Complete data access. */
    void completeDataAccess(PacketPtr pkt);

    /** Receive timing response. */
    bool recvTimingResp(PacketPtr pkt);

    /** Receive retry. */
    void recvRetry();

    /** Try to send a packet to the cache. */
    bool trySendPacket(bool isLoad, PacketPtr data_pkt);

    /**
     * Notify that PyMTL3 made a DCache call.
     * Called from pybind11 callback.
     * Public to allow access from static callback function.
     */
    void notifyPyMTL3DCacheCall(uint64_t cycle, Addr addr, uint32_t size,
                               bool isWrite, const std::vector<uint8_t>& data,
                               const std::string& methodName);

  private:
    /** PyMTL3 LSQUnitCL handle (nullptr if PyMTL3 is not available) */
    void* pymtl3LSQ;

    /** Flag indicating if PyMTL3 is available */
    bool pymtl3Available;

    /** Mismatch counter */
    uint64_t mismatchCount;

    /** Mock DCache port for capturing output calls */
    MockDCachePort* mockDCache;

    /**
     * Log a mismatch.
     * @param methodName Name of the method.
     * @param message Detailed mismatch message.
     */
    void logMismatch(const std::string &methodName,
                    const std::string &message);

    /**
     * Compare queue states between implementations.
     */
    void compareQueueStates();

    /**
     * Compare statistics between implementations.
     */
    void compareStatistics();

    /**
     * Compare DCache output calls between C++ and PyMTL3.
     */
    void compareDCacheCalls();

    // Friend declaration for callback access
    friend void notify_dcache_call_from_pymtl3(uint64_t, Addr, uint32_t, bool,
                                               const std::vector<uint8_t>&,
                                               const std::string&);
};

#else

// 对比模式禁用时，使用原始 LSQUnit
using LSQUnitComparison = LSQUnit;

#endif // LSQ_COMPARISON_MODE

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_LSQ_UNIT_COMPARISON_HH__
