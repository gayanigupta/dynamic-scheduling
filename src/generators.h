#pragma once

#include <random>

#include "infra_graph.h"
#include "task_graph.h"

namespace ds {

struct DagGenParams {
  int n_tasks = 40;
  int n_layers = 6;
  double edge_prob = 0.25;    // extra random edges between consecutive layers
  double work_min = 200.0, work_max = 5000.0;   // MI
  double mem_min = 64.0, mem_max = 1024.0;      // MB
  double data_min = 0.5, data_max = 40.0;       // MB per edge
  double ctx_min = 0.5, ctx_max = 15.0;         // MB migration context
};

struct InfraGenParams {
  int n_nodes = 10;
  double link_prob = 0.45;
  double mips_min = 800.0, mips_max = 4000.0;
  double mem_min = 2048.0, mem_max = 16384.0;
  double lat_min = 1.0, lat_max = 25.0;     // ms
  double bw_min = 100.0, bw_max = 2000.0;   // Mbps
  double cost_min = 0.01, cost_max = 0.3;   // per MB
  double pbusy_min = 15.0, pbusy_max = 60.0;
  double pidle_min = 1.0, pidle_max = 6.0;
};

// Layered random DAG: every non-source task gets >=1 parent; extra edges are
// sampled between adjacent layers. Guarantees a connected DAG.
TaskDAG gen_dag(std::mt19937& rng, const DagGenParams& p);

// Random connected infrastructure graph (spanning tree + extra links).
InfraGraph gen_infra(std::mt19937& rng, const InfraGenParams& p);

}  // namespace ds
