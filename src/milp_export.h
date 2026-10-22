#pragma once

#include <string>

#include "recovery.h"
#include "sim.h"

namespace ds {

// Serialize the recovery instance at failure time to JSON for the offline
// MILP solver. The instance mirrors exactly what recover_partial sees:
// affected subgraph, anchor parents (finish time + staged data location),
// candidate nodes, and pairwise route latency/bandwidth.
bool export_recovery_instance(Sim& s, int failed, int scenario,
                              const RecoveryParams& rp,
                              const std::string& path);

}  // namespace ds
