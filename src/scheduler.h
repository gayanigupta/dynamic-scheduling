#pragma once

#include "infra_graph.h"
#include "task_graph.h"

namespace ds {

// Result of an initial partitioning/scheduling pass.
struct InitialSchedule {
  std::vector<int> assign;      // task -> fog node
  std::vector<double> est_fin;  // estimated finish time per task (static plan)
  double est_makespan = 0.0;
};

// HEFT-style list scheduling. Tasks are ordered by upward rank and each is
// placed on the node minimizing its estimated earliest finish time. Tasks
// assigned to the same node form a "partition"; intra-partition edges have
// zero communication cost.
InitialSchedule heft_schedule(const TaskDAG& dag, InfraGraph& infra);

// Upward rank of each task: mean execution time plus max over children of
// (mean comm time + child rank). `fixed_node` optionally pins tasks whose
// position is already decided (anchors); their comm is charged to the pinned
// node.
std::vector<double> upward_ranks(const TaskDAG& dag, InfraGraph& infra,
                                 const std::vector<int>* fixed_node);

}  // namespace ds
