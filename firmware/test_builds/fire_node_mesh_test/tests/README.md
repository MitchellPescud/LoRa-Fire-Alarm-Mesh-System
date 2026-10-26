# Host tests (not part of the Arduino build)

The Arduino IDE ignores this folder. Run from `tests/` on a PC with g++:

    # packet format, HMAC, replay window (+ cross-check against Python's hmac)
    g++ -std=c++11 -Wall -I.. test_proto.cpp ../mesh_proto.cpp -o test_proto && ./test_proto
    python3 golden.py <hex printed on the FIRE line>

    # multi-node mesh simulator: runs the real mesh.cpp / alarm.cpp / mesh_proto.cpp
    # on 3-4 virtual nodes over a simulated radio (range, airtime, collisions, loss)
    g++ -std=c++14 -Wall -Wno-unused-variable -Wno-unused-function -Istubs sim_mesh.cpp -o sim_mesh && ./sim_mesh
    ./sim_mesh -v     # also prints every node's serial output

The simulator covers: heartbeat/peer discovery over a relay, FIRE across a
line topology, silence, network-wide clear, re-arm hold-off, new episode after
clear, replayed CLEAR, forged/foreign packets, command-node clear, hidden-node
collisions and 30% packet loss, plus (V5) node-to-node commands: CMD/ACK across
a relay, result codes, de-duplication of repeated copies, no-reply timeout,
replayed/forged CMD rejection and command-node -> fire-node over 3 hops. It does NOT test the Heltec radio driver or RF
behaviour - that needs real boards.

## Test build (Fire_Node_v5_Test)

    # three virtual nodes (ids 1, 100, 2) running the mesh test layer; writes a merged log in mesh_logger.py format
    g++ -std=c++14 -Wall -Wno-unused-variable -Wno-unused-function -Istubs sim_trace.cpp -o sim_trace
    ./sim_trace sim_line.log line 0.0     # software line topology 1 - 100 - 2 (block 2 / block 1), loss probability 0.0
    ./sim_trace sim_full.log full 0.0     # everyone hears everyone
    python3 ../../Mesh_Test_v1/analyze_mesh.py line=sim_line.log full=sim_full.log --out sim_results --plots

`sim_trace.cpp` delivers each transmission once; the original `sim_mesh.cpp` medium re-delivers a frame every millisecond for a second (harmless
for its checks because duplicates are dropped, but it would swamp a packet trace). `node_body_trace.inc` adds mesh_test.cpp and the TX hook.
