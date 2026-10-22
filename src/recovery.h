#pragma once

#include "sim.h"

namespace ds {

struct RecoveryParams {
  // --- boundary of the affected subgraph (our method) ---
  // If true, a WAITING task on a healthy node joins the affected subgraph
  // when ALL of its unfinished parents are already in the subgraph (it
  // "follows" the failure); the repartitioner may still leave it in place.
  bool expand_boundary = true;
  bool use_steiner = true;       // MST/Steiner-region replacement candidates
  int max_candidates = 8;        // replacement resources shortlisted
  double move_penalty_ms = 50.0; // extra cost of moving a healthy-node task
  // --- replanning cost model ---
  double replan_base_ms = 5.0;
  double replan_per_task_ms = 0.5;
  // --- optimal solver ---
  int opt_max_tasks = 9;         // B&B only below this subgraph size
  long opt_max_evals = 200000;   // safety cap on objective evaluations
};

// Baseline: no repartitioning. All unfinished tasks on the failed node go to
// the single statically designated backup resource.
RecoveryResult recover_static(Sim& s, int failed, const RecoveryParams& rp);

// Baseline: each affected task is independently placed on the alive node with
// the best estimated finish time (no grouping, no path awareness).
RecoveryResult recover_greedy(Sim& s, int failed, const RecoveryParams& rp);

// Baseline: re-partition and re-list-schedule ALL unfinished tasks over all
// alive resources. Anchored to completed/running tasks but free to move any
// waiting task.
RecoveryResult recover_full(Sim& s, int failed, const RecoveryParams& rp);

// Proposed method: identify the smallest affected region of the task DAG,
// shortlist replacement resources by shortest-path proximity to the anchor
// tasks that stayed put, repartition only that region, and keep every other
// assignment unchanged. Routes are repaired lazily via the incremental SPT
// update inside InfraGraph.
RecoveryResult recover_partial(Sim& s, int failed, const RecoveryParams& rp);

// Baseline: exact optimization of the affected-subgraph assignment via
// branch-and-bound. Only valid for small regions; falls back to the partial
// method beyond `opt_max_tasks`. Counts objective evaluations in
// tasks_considered.
RecoveryResult recover_optimal(Sim& s, int failed, const RecoveryParams& rp);

// --- shared helpers (exposed for the optimal solver and tests) ---

// WAITING tasks currently mapped to `node`.
std::vector<int> unfinished_on(const Sim& s, int node);

// Affected subgraph: unfinished tasks on the failed node, optionally expanded
// by the boundary rule to waiting tasks on healthy nodes all of whose
// unfinished parents are inside the set (fixpoint).
std::vector<int> affected_subgraph(const Sim& s, int failed, bool expand);

// Estimated finish contribution of a non-subgraph (anchor) parent.
double anchor_fin_est(const Sim& s, int task);

// List-schedule `subset` (given in topo order) onto `candidates`.
// `home[t]` is the task's current node (anchor for stay-penalty). Tasks whose
// home node is dead MUST move. Returns task -> new node.
// est_ready for a task on node c = max over parents(anchor fin + transfer).
std::vector<int> local_list_schedule(
    Sim& s, const std::vector<char>& in_subset,
    const std::vector<int>& candidates, const std::vector<int>& order,
    double move_penalty_ms, double* out_est_makespan = nullptr);

}  // namespace ds
