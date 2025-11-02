#!/bin/bash
# Example: Resource Monitoring Demo for Hyprlax
#
# This script demonstrates the resource monitoring capabilities of hyprlax.

set -e

echo "=== Hyprlax Resource Monitoring Example ==="
echo ""

# Check if hyprlax is running
if ! pgrep -x hyprlax > /dev/null; then
    echo "Error: hyprlax is not running"
    echo "Start hyprlax first, then run this script"
    exit 1
fi

# Function to run IPC command
run_ctl() {
    hyprlax ctl "$@" 2>&1
}

echo "1. Query current resource status:"
echo "   $ hyprlax ctl resource_status"
echo ""
run_ctl resource_status
echo ""

echo "2. Get general status (includes FPS, layer count, etc.):"
echo "   $ hyprlax ctl status"
echo ""
run_ctl status
echo ""

echo "3. Monitor resource growth over time:"
echo "   Simulating activity by adding/removing layers..."
echo ""

# Baseline
echo "   [Baseline]"
baseline=$(run_ctl resource_status | grep "Current:" | head -1)
echo "   $baseline"
echo ""

# Add some layers
if [ -f /usr/share/pixmaps/archlinux-logo.png ]; then
    TEST_IMAGE="/usr/share/pixmaps/archlinux-logo.png"
elif [ -f /usr/share/backgrounds/default.png ]; then
    TEST_IMAGE="/usr/share/backgrounds/default.png"
else
    echo "   [Skipping layer test - no test image available]"
    TEST_IMAGE=""
fi

if [ -n "$TEST_IMAGE" ]; then
    echo "   Adding 3 layers..."
    run_ctl add "$TEST_IMAGE" scale=1.0 opacity=0.8 > /dev/null
    run_ctl add "$TEST_IMAGE" scale=1.2 opacity=0.6 > /dev/null
    run_ctl add "$TEST_IMAGE" scale=0.8 opacity=0.9 > /dev/null
    echo ""

    # Wait a moment for resources to be allocated
    sleep 1

    echo "   [After adding layers]"
    after=$(run_ctl resource_status | grep "Current:" | head -1)
    echo "   $after"
    echo ""

    # Clean up
    echo "   Clearing layers..."
    run_ctl clear > /dev/null
    sleep 1

    echo "   [After cleanup]"
    cleanup=$(run_ctl resource_status | grep "Current:" | head -1)
    echo "   $cleanup"
    echo ""
fi

echo "4. Environment variable configuration:"
echo ""
echo "   Disable monitoring:"
echo "   $ export HYPRLAX_RESOURCE_MONITOR_DISABLE=1"
echo ""
echo "   Set check interval to 1 minute:"
echo "   $ export HYPRLAX_RESOURCE_MONITOR_INTERVAL=60"
echo ""
echo "   Enable debug output:"
echo "   $ export HYPRLAX_RESOURCE_MONITOR_DEBUG=1"
echo ""

echo "5. Production monitoring script example:"
echo ""
cat << 'EOF'
   #!/bin/bash
   # Continuous resource monitoring

   while true; do
       # Get current stats
       stats=$(hyprlax ctl resource_status 2>&1)

       # Extract FD count
       fd_count=$(echo "$stats" | grep "Current:" | head -1 | awk '{print $2}')

       # Extract memory usage
       mem_kb=$(echo "$stats" | grep "Current:" | tail -1 | awk '{print $2}')

       # Log to syslog or monitoring system
       logger -t hyprlax "Resources: FDs=$fd_count Memory=${mem_kb}KB"

       # Check for warnings
       if [ "$fd_count" -gt 50 ]; then
           logger -p user.warning -t hyprlax "High FD count: $fd_count"
       fi

       if [ "$mem_kb" -gt 100000 ]; then
           logger -p user.warning -t hyprlax "High memory usage: ${mem_kb}KB"
       fi

       sleep 300  # Check every 5 minutes
   done
EOF
echo ""

echo "=== Resource Monitoring Demo Complete ==="
echo ""
echo "For more information, see:"
echo "  - docs/resource_monitoring.md"
echo "  - hyprlax ctl --help"
