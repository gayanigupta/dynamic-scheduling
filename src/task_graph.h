#pragma once

#include <vector>
#include <unordered_set>

namespace ds {

// A task in the workflow DAG.
struct Task {
  int id;
  double work_mi;     // computational demand (million instructions)
  double mem_mb;      // memory required on the fog resource
  double context_mb;  // state/context that must be moved if the task migrates
};

// Dependency edge: `from` must finish (and its output transferred) before `to`
// can start.
struct TaskEdge {
  int from, to;
  double data_mb;  // amount of data transferred along this edge
};

struct TaskDAG {
  int n = 0;
  std::vector<Task> tasks;
  std::vector<TaskEdge> edges;
  std::vector<std::vector<int>> children;   // task -> child task ids
  std::vector<std::vector<int>> parents;    // task -> parent task ids
  std::vector<std::vector<int>> in_edges;   // task -> edge indices
  std::vector<std::vector<int>> out_edges;  // task -> edge indices

  void build(int n_tasks, std::vector<TaskEdge> edge_list,
             std::vector<Task> task_list);
  std::vector<int> topo_order() const;

  // Collect all tasks in `seeds` plus optionally unfinished ancestors /
  // descendants. `filter` restricts which tasks may be included (return true to
  // allow). Returns a set of task ids.
  std::unordered_set<int> closure(const std::vector<int>& seeds,
                                  bool with_ancestors,
                                  bool with_descendants) const;
};

}  // namespace ds
