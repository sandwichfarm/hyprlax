/*
 * hyprlax-test.cpp - Simplest possible Wayfire plugin to test loading
 */

#define WAYFIRE_PLUGIN

#include <wayfire/plugin.hpp>
#include <iostream>

class hyprlax_test_t : public wf::plugin_interface_t {
public:
    void init() override {
        std::cerr << "[HYPRLAX-TEST] Plugin initialized successfully!" << std::endl;
    }
    
    void fini() override {
        std::cerr << "[HYPRLAX-TEST] Plugin shutting down!" << std::endl;
    }
};

DECLARE_WAYFIRE_PLUGIN(hyprlax_test_t);