/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * TypeConverter - Type conversion between Gem5 and PyMTL3
 *
 * This class provides type conversion utilities between Gem5 types
 * and PyMTL3 types for the LSQ comparison framework.
 */

#ifndef __CPU_O3_TYPE_CONVERTER_HH__
#define __CPU_O3_TYPE_CONVERTER_HH__

#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "sim/faults.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/dyn_inst_ptr.hh"

namespace gem5
{

namespace o3
{

/**
 * Type converter between Gem5 and PyMTL3 types.
 */
class TypeConverter
{
  public:
    TypeConverter() = default;
    ~TypeConverter() = default;

    /**
     * Convert Gem5 DynInstPtr to PyMTL3 DynInst.
     * @param gem5Inst Gem5 DynInstPtr.
     * @return PyMTL3 DynInst (as void* for pybind11).
     */
    void* gem5_to_pymtl3_dyninst(const DynInstPtr &gem5Inst);

    /**
     * Convert PyMTL3 Fault to Gem5 Fault.
     * @param pymtl3Fault PyMTL3 Fault enum value.
     * @return Corresponding Gem5 Fault.
     */
    Fault pymtl3_to_gem5_fault(int pymtl3Fault);

    /**
     * Extract sequence number from DynInst.
     * @param inst DynInstPtr.
     * @return Sequence number.
     */
    InstSeqNum get_seq_num(const DynInstPtr &inst);

    /**
     * Extract PC from DynInst.
     * @param inst DynInstPtr.
     * @return PC value.
     */
    Addr get_pc(const DynInstPtr &inst);

    /**
     * Extract effective address from DynInst.
     * @param inst DynInstPtr.
     * @return Effective address.
     */
    Addr get_eff_addr(const DynInstPtr &inst);

    /**
     * Extract effective size from DynInst.
     * @param inst DynInstPtr.
     * @return Effective size.
     */
    uint32_t get_eff_size(const DynInstPtr &inst);

    /**
     * Check if instruction is a load.
     * @param inst DynInstPtr.
     * @return True if load instruction.
     */
    bool is_load(const DynInstPtr &inst);

    /**
     * Check if instruction is a store.
     * @param inst DynInstPtr.
     * @return True if store instruction.
     */
    bool is_store(const DynInstPtr &inst);
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_TYPE_CONVERTER_HH__
