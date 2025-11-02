#!/bin/bash

HYPRLAX_PID=1539036

echo "=== Baseline Measurements ===" > /home/sandwich/Develop/hyprlax/resource_baseline.txt
echo "PID: $HYPRLAX_PID" >> /home/sandwich/Develop/hyprlax/resource_baseline.txt
echo "" >> /home/sandwich/Develop/hyprlax/resource_baseline.txt

ps -o pid,rss,vsz,etime -p $HYPRLAX_PID >> /home/sandwich/Develop/hyprlax/resource_baseline.txt 2>&1

echo "" >> /home/sandwich/Develop/hyprlax/resource_baseline.txt

FD_COUNT=$(ls /proc/$HYPRLAX_PID/fd 2>/dev/null | wc -l)
echo "FD Count: $FD_COUNT" >> /home/sandwich/Develop/hyprlax/resource_baseline.txt

echo "" >> /home/sandwich/Develop/hyprlax/resource_baseline.txt

grep "VmRSS\|VmSize" /proc/$HYPRLAX_PID/status >> /home/sandwich/Develop/hyprlax/resource_baseline.txt 2>&1

cat /home/sandwich/Develop/hyprlax/resource_baseline.txt
