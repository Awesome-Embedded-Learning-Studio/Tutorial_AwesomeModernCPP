#!/bin/bash
# E3 驱动:匿名继承 → 命名对象 server/client(两个独立进程)→ fd 传递
set -u
cd "$(dirname "$0")"
echo "== E3 两条共享途径对照 =="
echo
./e3_two_ways anon
echo
echo "--- 命名对象:server 与 client 是两个独立进程(无 fork 亲缘)---"
./e3_two_ways server &
SERVER=$!
sleep 0.3
./e3_two_ways client
wait $SERVER
echo
./e3_two_ways fdpass
