/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * pybind11 bindings for PyMTL3 LSQUnitCL
 *
 * This file provides pybind11 bindings to embed Python and call
 * PyMTL3's LSQUnitCL from C++ for the comparison framework.
 */

#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>
#include <string>
#include <cstdlib>  // for getenv

namespace py = pybind11;

namespace gem5
{

namespace o3
{

// 全局 Python 解释器状态
static bool python_initialized = false;
static bool interpreter_was_initialized_by_us = false;
static py::module* gem5_pymtl3_module = nullptr;
static py::object* py_wrapper_class = nullptr;

/**
 * Get the project root directory.
 * Tries different methods to find the project root.
 */
std::string
get_project_root()
{
    // 方法1: 尝试从环境变量获取
    const char* env_path = std::getenv("PYMTL3_PROJECT_ROOT");
    if (env_path) {
        return std::string(env_path);
    }

    // 方法2: 尝试从 Python 的 site-packages 或其他已知位置推断
    // 默认假设 gem5 可执行文件在 gem5/build/RISCV/ 目录下
    // 项目根目录是 gem5/ 的父目录
    return "/mnt/c/Users/chenpeng/Documents/PyMTL3";
}

/**
 * Initialize Python interpreter and import PyMTL3 module.
 * This should be called once at startup.
 */
void
init_pymtl3_module()
{
    if (python_initialized) {
        return;
    }

    try {
        // 检查 Python 解释器是否已经初始化（gem5 可能已经初始化了）
        if (!Py_IsInitialized()) {
            // 只有当解释器未初始化时才初始化
            py::initialize_interpreter();
            interpreter_was_initialized_by_us = true;
        }
        python_initialized = true;

        std::cout << "[LSQComparison] Python interpreter initialized" << std::endl;

        // 添加项目路径到 sys.path
        py::module sys = py::module::import("sys");
        py::list path = sys.attr("path");

        // 获取项目根目录
        std::string project_path = get_project_root();
        path.attr("insert")(0, project_path);

        std::cout << "[LSQComparison] Added project path to sys.path: " << project_path << std::endl;

        // 打印当前的 sys.path 以便调试
        std::cout << "[LSQComparison] sys.path:" << std::endl;
        for (auto item : path) {
            std::cout << "  " << item.cast<std::string>() << std::endl;
        }

        // 导入 gem5_pymtl3.lsq_unit.pybind_wrapper
        gem5_pymtl3_module = new py::module(
            py::module::import("gem5_pymtl3.lsq_unit.pybind_wrapper")
        );

        std::cout << "[LSQComparison] Imported gem5_pymtl3.lsq_unit.pybind_wrapper" << std::endl;

        // 获取 PyMTL3LSQWrapper 类
        py_wrapper_class = new py::object(
            gem5_pymtl3_module->attr("PyMTL3LSQWrapper")
        );

        std::cout << "[LSQComparison] PyMTL3 module initialization complete" << std::endl;

    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    } catch (const std::exception& e) {
        std::cerr << "[LSQComparison] Error: " << e.what() << std::endl;
    }
}

/**
 * Create a PyMTL3 LSQUnitCL instance via wrapper.
 * @param lqEntries Number of load queue entries.
 * @param sqEntries Number of store queue entries.
 * @return Pointer to PyMTL3 wrapper instance (as void*).
 */
void*
create_pymtl3_lsq(uint32_t lqEntries, uint32_t sqEntries)
{
    if (!python_initialized) {
        init_pymtl3_module();
    }

    if (!py_wrapper_class) {
        std::cerr << "[LSQComparison] PyMTL3 wrapper class not available" << std::endl;
        return nullptr;
    }

    try {
        // 创建 PyMTL3LSQWrapper 实例
        py::object wrapper = (*py_wrapper_class)(lqEntries, sqEntries);

        // 返回新分配的 py::object 指针
        py::object* wrapper_ptr = new py::object(wrapper);

        std::cout << "[LSQComparison] Created PyMTL3LSQWrapper instance" << std::endl;
        return static_cast<void*>(wrapper_ptr);

    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error creating wrapper: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return nullptr;
    }
}

/**
 * Call insert_load on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param seq_num Instruction sequence number.
 * @param pc Instruction PC.
 * @param ea Effective address.
 * @param size Access size.
 * @return True if successful.
 */
bool
pymtl3_insert_load(void* lsq, uint64_t seq_num, uint64_t pc, uint64_t ea, uint32_t size)
{
    if (!lsq) {
        return false;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);

        // 创建 DynInst (通过 Python)
        py::module dyn_inst_module = py::module::import("gem5_pymtl3.common.dyn_inst");
        py::object DynInst = dyn_inst_module.attr("DynInst");
        py::object inst = DynInst(seq_num, pc, ea, size, true, false);

        // 调用 insert_load
        bool result = (*wrapper).attr("insert_load")(inst).cast<bool>();
        return result;

    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in insert_load: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return false;
    }
}

/**
 * Call insert_store on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param seq_num Instruction sequence number.
 * @param pc Instruction PC.
 * @param ea Effective address.
 * @param size Access size.
 * @return True if successful.
 */
bool
pymtl3_insert_store(void* lsq, uint64_t seq_num, uint64_t pc, uint64_t ea, uint32_t size)
{
    if (!lsq) {
        return false;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);

        // 创建 DynInst
        py::module dyn_inst_module = py::module::import("gem5_pymtl3.common.dyn_inst");
        py::object DynInst = dyn_inst_module.attr("DynInst");
        py::object inst = DynInst(seq_num, pc, ea, size, false, true);

        // 调用 insert_store
        bool result = (*wrapper).attr("insert_store")(inst).cast<bool>();
        return result;

    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in insert_store: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return false;
    }
}

/**
 * Call execute_load on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param lq_idx Load queue index.
 * @return Fault value (as int).
 */
int
pymtl3_execute_load(void* lsq, int lq_idx)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int fault = (*wrapper).attr("execute_load")(lq_idx).cast<int>();
        return fault;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in execute_load: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Call execute_store on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param sq_idx Store queue index.
 * @return Fault value (as int).
 */
int
pymtl3_execute_store(void* lsq, int sq_idx)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int fault = (*wrapper).attr("execute_store")(sq_idx).cast<int>();
        return fault;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in execute_store: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Call commit_load on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @return Always nullptr for now (simplified).
 */
void*
pymtl3_commit_load(void* lsq)
{
    if (!lsq) {
        return nullptr;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        (*wrapper).attr("commit_load")();
        return nullptr;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in commit_load: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return nullptr;
    }
}

/**
 * Call commit_stores on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param youngest_sn Youngest sequence number.
 * @return Number of stores committed.
 */
int
pymtl3_commit_stores(void* lsq, uint64_t youngest_sn)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int count = (*wrapper).attr("commit_stores")(youngest_sn).cast<int>();
        return count;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in commit_stores: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Call squash on PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param squash_sn Squash sequence number.
 * @return Number of instructions squashed.
 */
int
pymtl3_squash(void* lsq, uint64_t squash_sn)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int count = (*wrapper).attr("squash")(squash_sn).cast<int>();
        return count;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in squash: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Get number of loads in LQ.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @return Number of loads.
 */
int
pymtl3_num_loads(void* lsq)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int count = (*wrapper).attr("num_loads")().cast<int>();
        return count;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in num_loads: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Get number of stores in SQ.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @return Number of stores.
 */
int
pymtl3_num_stores(void* lsq)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int count = (*wrapper).attr("num_stores")().cast<int>();
        return count;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in num_stores: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Get statistics from PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param stat_name Name of the statistic.
 * @return Statistic value.
 */
uint64_t
pymtl3_get_stat(void* lsq, const std::string &stat_name)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        uint64_t value = (*wrapper).attr("get_stat")(stat_name).cast<uint64_t>();
        return value;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in get_stat: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Set DCache callback for PyMTL3 LSQUnitCL.
 * This callback is called when PyMTL3 makes a DCache access.
 * 
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param callback Callback function pointer.
 *        Signature: void callback(uint64_t cycle, uint64_t addr, 
 *                                 uint32_t size, bool is_write,
 *                                 const std::vector<uint8_t>& data,
 *                                 const std::string& method_name)
 */
void
pymtl3_set_dcache_callback(void* lsq,
    void (*callback)(uint64_t, uint64_t, uint32_t, bool,
                    const std::vector<uint8_t>&,
                    const std::string&))
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        
        // Create a Python callable that wraps the C++ callback
        py::cpp_function py_callback = 
            [callback](uint64_t cycle, uint64_t addr, uint32_t size,
                      bool is_write, py::bytes data,
                      const std::string& method_name) {
                // Convert Python bytes to vector<uint8_t>
                std::string data_str = data.cast<std::string>();
                std::vector<uint8_t> data_vec(data_str.begin(), data_str.end());
                
                // Call C++ callback
                callback(cycle, addr, size, is_write, data_vec, method_name);
            };
        
        // Set the callback on the wrapper
        (*wrapper).attr("set_dcache_callback")(py_callback);
        
        std::cout << "[LSQComparison] DCache callback set for PyMTL3" << std::endl;
        
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in set_dcache_callback: " 
                  << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

/**
 * Get current cycle from PyMTL3 LSQUnitCL.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @return Current cycle.
 */
uint64_t
pymtl3_get_cycle(void* lsq)
{
    if (!lsq) {
        return 0;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        uint64_t cycle = (*wrapper).attr("get_current_cycle")().cast<uint64_t>();
        return cycle;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in get_cycle: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;
    }
}

/**
 * Tick PyMTL3 LSQUnitCL by one cycle.
 * This should be called every cycle to keep PyMTL3 in sync with Gem5.
 * Synchronization is achieved by calling this function the same number
 * of times as Gem5's tick, not by passing cycle values.
 * 
 * @param lsq Pointer to PyMTL3 wrapper instance.
 */
void
pymtl3_tick(void* lsq)
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        
        // Call tick without parameters - PyMTL3 will increment its own cycle counter
        // This ensures synchronization by tick count, not by cycle value
        (*wrapper).attr("tick")();
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in tick: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

/**
 * Set TLB request callback for PyMTL3 LSQUnitCL (新异步方式).
 * This callback is called when PyMTL3 sends a TLB translation request.
 * 
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param callback Callback function pointer.
 *        Signature: void callback(uint64_t seq_num, uint64_t vaddr, 
 *                                 uint32_t size, bool is_load)
 */
void
pymtl3_set_tlb_req_callback(void* lsq,
    void (*callback)(uint64_t, uint64_t, uint32_t, bool))
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        
        // Create a Python callable that wraps the C++ callback
        py::cpp_function py_callback = 
            [callback](uint64_t seq_num, uint64_t vaddr, 
                      uint32_t size, bool is_load) {
                // Call C++ callback to handle TLB request
                callback(seq_num, vaddr, size, is_load);
            };
        
        // Set the callback on the wrapper
        (*wrapper).attr("set_tlb_req_callback")(py_callback);
        
        std::cout << "[LSQComparison] TLB request callback set for PyMTL3" << std::endl;
        
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in set_tlb_req_callback: " 
                  << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

/**
 * Translate virtual address to physical address using PyMTL3's TLB callback.
 * This is called when PyMTL3 needs to translate an address.
 * 
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param vaddr Virtual address to translate.
 * @return Physical address.
 */
uint64_t
pymtl3_translate_address(void* lsq, uint64_t vaddr)
{
    if (!lsq) {
        return vaddr;  // Identity mapping if no wrapper
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        uint64_t paddr = (*wrapper).attr("translate_address")(vaddr).cast<uint64_t>();
        return paddr;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in translate_address: " 
                  << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return vaddr;  // Return vaddr on error
    }
}

/**
 * Execute a load instruction in PyMTL3 LSQUnitCL.
 * This should be called when the load is ready to execute.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param seq_num Instruction sequence number.
 * @param lq_idx Load queue index.
 * @return Fault code (0 = NoFault).
 */
int
pymtl3_execute_load(void* lsq, uint64_t seq_num, int lq_idx)
{
    if (!lsq) {
        return 0;  // NoFault
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int fault = (*wrapper).attr("execute_load")(lq_idx).cast<int>();
        return fault;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in execute_load: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;  // Return NoFault on error
    }
}

/**
 * Execute a store instruction in PyMTL3 LSQUnitCL.
 * This should be called when the store is ready to execute.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param seq_num Instruction sequence number.
 * @param sq_idx Store queue index.
 * @return Fault code (0 = NoFault).
 */
int
pymtl3_execute_store(void* lsq, uint64_t seq_num, int sq_idx)
{
    if (!lsq) {
        return 0;  // NoFault
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        int fault = (*wrapper).attr("execute_store")(sq_idx).cast<int>();
        return fault;
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in execute_store: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return 0;  // Return NoFault on error
    }
}

/**
 * Update store address in PyMTL3 LSQUnitCL.
 * This should be called after the store address is calculated.
 * @param lsq Pointer to PyMTL3 wrapper instance.
 * @param seq_num Instruction sequence number.
 * @param addr Effective address.
 * @param size Access size in bytes.
 */
void
pymtl3_update_store_addr(void* lsq, uint64_t seq_num, uint64_t addr, uint32_t size)
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        (*wrapper).attr("execute_store_with_addr")(seq_num, addr, size);
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in execute_store_with_addr: " << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

// ===== 异步TLB转换接口实现 =====

/**
 * Set TLB response callback for PyMTL3 LSQUnitCL.
 * This callback is called by C++ to send TLB translation results back to PyMTL3.
 */
void
pymtl3_set_tlb_resp_callback(void* lsq,
    void (*callback)(uint64_t, uint64_t, int))
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        
        // Create a Python callable that wraps the C++ callback
        py::cpp_function py_callback = 
            [callback](uint64_t inst_id, uint64_t paddr, int fault) {
                // Call C++ callback to send TLB result
                callback(inst_id, paddr, fault);
            };
        
        // Set the callback on the wrapper
        (*wrapper).attr("set_tlb_resp_callback")(py_callback);
        
        std::cout << "[LSQComparison] TLB response callback set for PyMTL3" << std::endl;
        
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in set_tlb_resp_callback: " 
                  << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

/**
 * Send TLB translation response to PyMTL3.
 * Called by C++ when TLB translation is complete.
 */
void
pymtl3_send_tlb_resp(void* lsq, uint64_t seq_num, uint64_t paddr, int fault)
{
    if (!lsq) {
        return;
    }

    try {
        py::object* wrapper = static_cast<py::object*>(lsq);
        
        // Send TLB result to PyMTL3 wrapper
        (*wrapper).attr("handle_tlb_resp")(seq_num, paddr, fault);
        
    } catch (const py::error_already_set& e) {
        std::cerr << "[LSQComparison] Python error in send_tlb_resp: " 
                  << e.what() << std::endl;
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

} // namespace o3
} // namespace gem5
