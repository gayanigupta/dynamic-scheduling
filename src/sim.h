#pragma once

#include <functional>
#include <queue>
#include <vector>

#include "infra_graph.h"
#include "task_graph.h"

namespace ds {

enum class TStatus : char { WAITING, RUNNING, DONE };

// What a recovery strategy decided, and how much work it took to decide.
struct RecoveryResult {
  std::vector<std::pair<int, int>> moves;  // (task, new node)
  long tasks_considered = 0;               // size of the replanning problem
  double replan_ms = 0.0;                  // modeled replanning delay
  double algo_wall_ms = 0.0;               // measured wall time of planner
};

struct SimParams {
  double replan_base_ms = 5.0;       // fixed cost of invoking a replanner
  double replan_per_task_ms = 0.5;   // per task in the replanned region
  double deploy_bw_mbps = 200.0;     // control-channel bandwidth for migration
  double mig_cost_per_mb = 0.01;     // comm cost of migrating 1 MB of context
  // Durable store (federation controller): fallback replica for task outputs
  // whose staging node has failed. src == -1 denotes the store.
  double store_lat_ms = 25.0;
  double store_bw_mbps = 1000.0;
  double store_cost_per_mb = 0.05;
};

struct RunMetrics {
  double makespan_ms = 0.0;
  double comm_cost = 0.0;         // sum data_mb * route cost/MB
  double data_transferred_mb = 0.0;
  double energy_j = 0.0;
  int tasks_moved = 0;            // tasks whose assignment changed post-plan
  double recovery_time_ms = 0.0;  // replan + redeploy delay at failure
  long tasks_considered = 0;      // replanning problem size
  double algo_wall_ms = 0.0;      // recovery algorithm wall time
  long spt_updates = 0;           // incremental path recomputations
  bool aborted = false;           // true if some task could not be placed
  bool completed = false;
};

// Discrete-event simulator. Executes a workflow DAG on the infra graph under a
// fixed task->node assignment; supports failure injection and in-flight
// reassignment via a recovery callback.
//
// Simplifications (documented for the paper):
//  - each fog node executes one task at a time (single-slot execution)
//  - network is modeled as shortest-path latency + bandwidth bottleneck, no
//    bandwidth contention between flows
//  - edge communication cost is charged at the consuming task's start, using
//    the parent's finish location and the child's node at that time
//  - a task running on a failed node loses all progress (no checkpointing)
struct Sim {
  const TaskDAG* dag = nullptr;
  InfraGraph infra;
  SimParams params;

  std::vector<int> assign;  // task -> node
  std::vector<TStatus> st;
  std::vector<int> inbound_pending;            // parents not yet finished
  std::vector<double> ready_time;              // earliest data-ready time
  std::vector<double> deploy_at;               // earliest start after migration
  std::vector<double> fin_time;
  std::vector<int> fin_node;
  // Per in-edge: parent's finish time and the node where its output data is
  // staged. Data is pushed to the child's node at parent finish; if that
  // staging node later dies, reads fall back to the durable replica at
  // `backup_node` (completed tasks are never re-executed).
  struct ParRec { double fin = 0.0; int node = -1; bool set = false; };
  std::vector<std::vector<ParRec>> parrec;     // task -> per in-edge

  std::vector<double> node_free;
  std::vector<int> node_cur;
  std::vector<double> busy_time;
  std::vector<int> run_gen;  // generation counter to invalidate stale events

  double now = 0.0;
  int done_count = 0;
  int backup_node = -1;  // statically designated fallback (for static policy)

  RunMetrics m;
  std::vector<std::pair<double, int>> failures;  // (time, node)

  // Recovery hook invoked at each failure. May be null (no recovery -> abort).
  std::function<RecoveryResult(Sim&, int)> on_failure;

  void init(const TaskDAG& dag_, const InfraGraph& infra_,
            const std::vector<int>& assignment, const SimParams& p);
  // Queue a node-failure event. May be called before or after init().
  void inject_failure(double t, int node);
  void run();  // run to completion or abort

  double task_ready_on(int task, int node) const;  // data-ready time on `node`
  // Node from which edge `k` of `task` will source its data: the staged
  // location for finished parents (with backup fallback), else the parent's
  // current assignment (data not yet produced).
  int edge_data_node(int task, int k) const;
  // Transfer time/cost of `mb` MB from `src` (-1 = durable store) to `dst`.
  double edge_transfer_ms(int src, int dst, double mb) const;
  double edge_cost_per_mb(int src, int dst) const;
  void try_dispatch(int node);

 private:
  struct Ev {
    double t;
    int kind;  // 0=finish(task), 1=failure(node), 2=node_check(node)
    int id;
    int gen;
    bool operator>(const Ev& o) const { return t > o.t; }
  };
  std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> eq;

  void on_task_finish(int task, int gen);
  void on_failure_event(int node);
  void dispatch(int task, int node);
};

// Migration delay (ms) for moving `task` onto a new node.
double migration_ms(const TaskDAG& dag, const SimParams& p, int task);

}  // namespace ds
