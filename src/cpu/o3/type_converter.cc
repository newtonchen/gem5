/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * TypeConverter - Implementation
 */

#include "cpu/o3/type_converter.hh"
#include "cpu/o3/dyn_inst.hh"

namespace gem5
{

namespace o3
{

void*
TypeConverter::gem5_to_pymtl3_dyninst(const DynInstPtr &gem5Inst)
{
    // TODO: Implement conversion from Gem5 DynInstPtr to PyMTL3 DynInst
    // This will:
    // 1. Extract all necessary fields from gem5Inst
    // 2. Create a Python DynInst object via pybind11
    // 3. Return a pointer/handle to it
    return nullptr;
}

Fault
TypeConverter::pymtl3_to_gem5_fault(int pymtl3Fault)
{
    // Map PyMTL3 Fault enum values to Gem5 Fault
    // Assuming:
    // 0 = Fault.NO_FAULT
    // 1 = Fault.FAULT
    switch (pymtl3Fault) {
        case 0:
            return NoFault;
        case 1:
        default:
            return std::make_shared<UnimpFault>("PyMTL3 fault");
    }
}

InstSeqNum
TypeConverter::get_seq_num(const DynInstPtr &inst)
{
    if (!inst) {
        return 0;
    }
    return inst->seqNum;
}

Addr
TypeConverter::get_pc(const DynInstPtr &inst)
{
    if (!inst) {
        return 0;
    }
    return inst->pcState().instAddr();
}

Addr
TypeConverter::get_eff_addr(const DynInstPtr &inst)
{
    if (!inst) {
        return 0;
    }
    // TODO: Get effective address from DynInst
    // This depends on how Gem5 stores effective address
    return 0;
}

uint32_t
TypeConverter::get_eff_size(const DynInstPtr &inst)
{
    if (!inst) {
        return 0;
    }
    // TODO: Get effective size from DynInst
    return 0;
}

bool
TypeConverter::is_load(const DynInstPtr &inst)
{
    if (!inst) {
        return false;
    }
    return inst->isLoad();
}

bool
TypeConverter::is_store(const DynInstPtr &inst)
{
    if (!inst) {
        return false;
    }
    return inst->isStore();
}

} // namespace o3
} // namespace gem5
