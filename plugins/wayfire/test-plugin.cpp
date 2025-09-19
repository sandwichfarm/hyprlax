#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <syslog.h>

class test_plugin : public wf::per_output_plugin_instance_t {
public:
    void init() override {
        openlog("test-wayfire", LOG_PID | LOG_CONS | LOG_PERROR, LOG_USER);
        syslog(LOG_WARNING, "TEST PLUGIN INITIALIZED!");
        
        // Also try to write to a file
        FILE *f = fopen("/tmp/wayfire-test-plugin.txt", "w");
        if (f) {
            fprintf(f, "Test plugin loaded!\n");
            fclose(f);
        }
    }
    
    void fini() override {
        syslog(LOG_WARNING, "TEST PLUGIN DESTROYED!");
        closelog();
    }
};

DECLARE_WAYFIRE_PLUGIN(wf::per_output_plugin_t<test_plugin>);