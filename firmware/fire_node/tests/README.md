# Host tests (not part of the Arduino build)

The Arduino IDE ignores this folder. Run from `tests/` on a PC with g++:

    # packet format, HMAC, replay window (+ cross-check against Python's hmac)
    g++ -std=c++11 -Wall -I.. test_proto.cpp ../mesh_proto.cpp -o test_proto && ./test_proto
    python3 golden.py <hex printed on the FIRE line>

    # multi-node mesh simulator: runs the real mesh.cpp / alarm.cpp / mesh_proto.cpp
    # on 3-4 virtual nodes over a simulated radio (range, airtime, collisions, loss)
    g++ -std=c++14 -Wall -Wno-unused-variable -Wno-unused-function -Istubs sim_mesh.cpp -o sim_mesh && ./sim_mesh
    ./sim_mesh -v     # also prints every node's serial output

    # V6 battery filter (burst trimmed-mean -> median window -> EMA -> display deadband),
    # uses the real constants from ../config.h and a simulated noisy ADC + TX current sag
    g++ -std=c++11 -Wall -I.. test_battery.cpp -o test_battery && ./test_battery

The simulator covers: heartbeat/peer discovery over a relay, FIRE across a
line topology, silence, network-wide clear, re-arm hold-off, new episode after
clear, replayed CLEAR, forged/foreign packets, command-node clear, hidden-node
collisions and 30% packet loss, plus (V5) node-to-node commands: CMD/ACK across
a relay, result codes, de-duplication of repeated copies, no-reply timeout,
replayed/forged CMD rejection and command-node -> fire-node over 3 hops. It does NOT test the Heltec radio driver or RF
behaviour - that needs real boards.
