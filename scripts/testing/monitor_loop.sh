#!/bin/bash
PID=$1
DURATION=$2

echo "timestamp,rss_kb,vsz_kb,fd_count" > /home/sandwich/Develop/hyprlax/resource_tracking.csv

for i in $(seq 1 $DURATION); do
    TIMESTAMP=$(date +%s)
    RSS=$(grep VmRSS /proc/$PID/status 2>/dev/null | awk '{print $2}')
    VSZ=$(grep VmSize /proc/$PID/status 2>/dev/null | awk '{print $2}')
    FD_COUNT=$(ls /proc/$PID/fd 2>/dev/null | wc -l)

    echo "$TIMESTAMP,$RSS,$VSZ,$FD_COUNT" >> /home/sandwich/Develop/hyprlax/resource_tracking.csv

    if [ $((i % 3)) -eq 0 ]; then
        echo "Check $i/$DURATION: RSS=${RSS}KB VSZ=${VSZ}KB FDs=$FD_COUNT"
    fi

    sleep 60
done

echo "Monitoring complete"
