#include <wayfire/plugin.hpp>
#include <wayfire/per-output-plugin.hpp>

class minimal : public wf::per_output_plugin_instance_t {
public:
    void init() override {}
    void fini() override {}
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<minimal>);