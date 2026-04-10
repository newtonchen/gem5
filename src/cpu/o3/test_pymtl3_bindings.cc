/*
 * Simple standalone test for PyMTL3 bindings
 *
 * This program tests the pybind11 bindings without needing
 * the full Gem5 simulation environment.
 */

#include <iostream>
#include <string>

// 包含我们的绑定头文件
#include "lsq_unit_comparison.hh"

using namespace gem5::o3;

int main() {
    std::cout << "=" << std::endl;
    std::cout << "PyMTL3 Bindings Standalone Test" << std::endl;
    std::cout << "=" << std::endl;

    try {
        // Step 1: Initialize Python module
        std::cout << "\n[Step 1] Initializing Python module..." << std::endl;
        init_pymtl3_module();
        std::cout << "✅ Python module initialized" << std::endl;

        // Step 2: Create PyMTL3 LSQ
        std::cout << "\n[Step 2] Creating PyMTL3 LSQ..." << std::endl;
        void* lsq = create_pymtl3_lsq(16, 16);
        if (lsq) {
            std::cout << "✅ PyMTL3 LSQ created" << std::endl;
        } else {
            std::cerr << "❌ Failed to create PyMTL3 LSQ" << std::endl;
            return 1;
        }

        // Step 3: Test insert_load
        std::cout << "\n[Step 3] Testing insert_load..." << std::endl;
        bool result = pymtl3_insert_load(lsq, 1, 0x1000, 0x8000, 4);
        std::cout << "insert_load result: " << (result ? "true" : "false") << std::endl;

        // Step 4: Test num_loads
        std::cout << "\n[Step 4] Testing num_loads..." << std::endl;
        int lq_size = pymtl3_num_loads(lsq);
        std::cout << "LQ size: " << lq_size << std::endl;

        // Step 5: Test insert_store
        std::cout << "\n[Step 5] Testing insert_store..." << std::endl;
        result = pymtl3_insert_store(lsq, 2, 0x1004, 0x8000, 4);
        std::cout << "insert_store result: " << (result ? "true" : "false") << std::endl;

        // Step 6: Test num_stores
        std::cout << "\n[Step 6] Testing num_stores..." << std::endl;
        int sq_size = pymtl3_num_stores(lsq);
        std::cout << "SQ size: " << sq_size << std::endl;

        // Step 7: Test stats
        std::cout << "\n[Step 7] Testing get_stat..." << std::endl;
        uint64_t stat = pymtl3_get_stat(lsq, "forw_loads");
        std::cout << "forw_loads stat: " << stat << std::endl;

        std::cout << "\n" << std::endl;
        std::cout << "=" << std::endl;
        std::cout << "All tests completed!" << std::endl;
        std::cout << "=" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
