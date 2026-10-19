#include "sim.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace ds {

double migration_ms(const TaskDAG& dag, const SimParams& p, int task) {
  return dag.tasks[task].context_mb * 8.0 / p.deploy_bw_mbps * 1000.0;
}

void Sim::init(const TaskDAG& dag_, const InfraGraph& infra_,
               const std::vector<int>& assignment, const SimParams& p) {
  dag = &dag_;
  infra = infra_;
  params = p;
  int n = dag->n;
  assign = assignment;
  st.assign(n, TStatus::WAITING);
  inbound_pending.assign(n, 0);
  ready_time.assign(n, 0.0);
  deploy_at.assign(n, 0.0);
  fin_time.assign(n, 0.0);
  fin_node.assign(n, -1);
  parrec.assign(n, {});
  for (int t = 0; t < n; ++t) {
    parrec[t].resize(dag->in_edges[t].size());
    inbound_pending[t] = (int)dag->in_edges[t].size();
    if (inbound_pending[t] == 0) ready_time[t] = 0.0;
  }
  node_free.assign(infra.n, 0.0);
  node_cur.assign(infra.n, -1);
  busy_time.assign(infra.n, 0.0);
  run_gen.assign(n, 0);
  now = 0.0;
  done_count = 0;
  m = RunMetrics{};

  // Designated backup for the static baseline: alive node with max MIPS.
  double best = -1;
  for (const auto& nd : infra.nodes)
    if (nd.alive && nd.mips > best) {
      best = nd.mips;
      backup_node = nd.id;
    }

  // Seed dispatch on every node that hosts a source task.
  for (int t = 0; t < n; ++t)
    if (inbound_pending[t] == 0) eq.push({0.0, 2, assign[t], 0});
  for (auto [ft, fn] : failures) eq.push({ft, 1, fn, 0});
}

void Sim::inject_failure(double t, int node) {
  failures.push_back({t, node});
  eq.push({t, 1, node, 0});
}

int Sim::edge_data_node(int task, int k) const {
  const auto& rec = parrec[task][k];
  if (rec.set) return infra.alive(rec.node) ? rec.node : -1;  // durable store
  return assign[dag->edges[dag->in_edges[task][k]].from];
}

double Sim::edge_transfer_ms(int src, int dst, double mb) const {
  if (src == dst && src >= 0) return 0.0;
  if (src < 0 || !infra.alive(src))
    return params.store_lat_ms + mb * 8.0 / params.store_bw_mbps * 1000.0;
  if (!infra.alive(dst)) return InfraGraph::INF;
  return const_cast<InfraGraph&>(infra).transfer_ms(src, dst, mb);
}

double Sim::edge_cost_per_mb(int src, int dst) const {
  if (src == dst && src >= 0) return 0.0;
  if (src < 0 || !infra.alive(src)) return params.store_cost_per_mb;
  return const_cast<InfraGraph&>(infra).route_cost_per_mb(src, dst);
}

double Sim::task_ready_on(int task, int node) const {
  double r = 0.0;
  for (size_t k = 0; k < dag->in_edges[task].size(); ++k) {
    const auto& rec = parrec[task][k];
    if (!rec.set) return InfraGraph::INF;  // parent not done yet
    const auto& e = dag->edges[dag->in_edges[task][k]];
    int src = edge_data_node(task, k);
    double tr = edge_transfer_ms(src, node, e.data_mb);
    r = std::max(r, rec.fin + tr);
  }
  return r;
}

void Sim::dispatch(int task, int node) {
  st[task] = TStatus::RUNNING;
  node_cur[node] = task;
  double exec = dag->tasks[task].work_mi / infra.nodes[node].mips * 1000.0;
  double fin = now + exec;
  node_free[node] = fin;
  busy_time[node] += exec;
  // Charge communication for all inbound edges at consumption time.
  for (size_t k = 0; k < dag->in_edges[task].size(); ++k) {
    const auto& e = dag->edges[dag->in_edges[task][k]];
    int pn = edge_data_node(task, k);
    if (pn != node) {
      m.data_transferred_mb += e.data_mb;
      double c = edge_cost_per_mb(pn, node);
      if (c < InfraGraph::INF) m.comm_cost += e.data_mb * c;
    }
  }
  eq.push({fin, 0, task, run_gen[task]});
}

void Sim::try_dispatch(int node) {
  if (!infra.alive(node) || node_cur[node] >= 0) return;
  int best = -1;
  double best_key = InfraGraph::INF;
  double next_wake = InfraGraph::INF;
  for (int t = 0; t < dag->n; ++t) {
    if (st[t] != TStatus::WAITING || assign[t] != node ||
        inbound_pending[t] != 0)
      continue;
    double eligible = std::max(ready_time[t], deploy_at[t]);
    if (eligible <= now) {
      if (ready_time[t] < best_key) {
        best_key = ready_time[t];
        best = t;
      }
    } else if (eligible < next_wake) {
      next_wake = eligible;
    }
  }
  if (best >= 0) {
    dispatch(best, node);
  } else if (next_wake < InfraGraph::INF) {
    eq.push({next_wake, 2, node, 0});
  }
}

void Sim::on_task_finish(int task, int gen) {
  if (st[task] != TStatus::RUNNING || gen != run_gen[task]) return;
  int node = assign[task];
  st[task] = TStatus::DONE;
  node_cur[node] = -1;
  fin_time[task] = now;
  fin_node[task] = node;
  ++done_count;

  for (int ei : dag->out_edges[task]) {
    const auto& e = dag->edges[ei];
    int c = e.to;
    // Stage the output at the child's node; if that node is already dead the
    // copy lives only in the durable store (edge_data_node falls back).
    int stage = assign[c];
    for (size_t k = 0; k < dag->in_edges[c].size(); ++k)
      if (dag->in_edges[c][k] == ei) {
        parrec[c][k] = {now, stage, true};
        break;
      }
    if (--inbound_pending[c] == 0) {
      ready_time[c] = task_ready_on(c, assign[c]);
      eq.push({std::max(now, ready_time[c]), 2, assign[c], 0});
    }
  }
  try_dispatch(node);
}

void Sim::on_failure_event(int f) {
  infra.fail_node(f);
  m.spt_updates = infra.spt_updates;  // snapshot; updated again at end

  // Kill the running task, if any (progress lost).
  int victim = node_cur[f];
  if (victim >= 0) {
    ++run_gen[victim];  // invalidate its pending FINISH event
    node_cur[f] = -1;
    st[victim] = TStatus::WAITING;
  }

  if (!on_failure) {
    m.aborted = true;
    eq = decltype(eq)();
    return;
  }

  RecoveryResult res = on_failure(*this, f);
  m.tasks_considered += res.tasks_considered;
  m.algo_wall_ms += res.algo_wall_ms;

  double max_mig = 0.0;
  for (auto [t, nn] : res.moves) {
    if (st[t] != TStatus::WAITING) continue;      // never move DONE/RUNNING
    if (!infra.alive(nn)) { m.aborted = true; continue; }
    if (nn == assign[t]) {
      deploy_at[t] = std::max(deploy_at[t], now + res.replan_ms);
      continue;
    }
    ++m.tasks_moved;
    m.data_transferred_mb += dag->tasks[t].context_mb;
    m.comm_cost += dag->tasks[t].context_mb * params.mig_cost_per_mb;
    assign[t] = nn;
    double mig = migration_ms(*dag, params, t);
    max_mig = std::max(max_mig, mig);
    deploy_at[t] = now + res.replan_ms + mig;
    if (inbound_pending[t] == 0)
      ready_time[t] = task_ready_on(t, nn);
    eq.push({deploy_at[t], 2, nn, 0});
  }
  m.recovery_time_ms += res.replan_ms + max_mig;

  // Any WAITING task still mapped to the dead node is unschedulable.
  for (int t = 0; t < dag->n; ++t)
    if (st[t] == TStatus::WAITING && assign[t] == f) m.aborted = true;
}

void Sim::run() {
  while (!eq.empty()) {
    if (done_count == dag->n || m.aborted) break;
    Ev ev = eq.top();
    eq.pop();
    now = ev.t;
    switch (ev.kind) {
      case 0:
        on_task_finish(ev.id, ev.gen);
        break;
      case 1:
        on_failure_event(ev.id);
        break;
      case 2:
        try_dispatch(ev.id);
        break;
    }
  }
  m.completed = (done_count == dag->n) && !m.aborted;
  if (m.completed) {
    m.makespan_ms = now;
    for (const auto& nd : infra.nodes) {
      double busy = busy_time[nd.id];
      // Dead nodes and never-used nodes draw no idle power.
      double idle = (nd.alive && busy > 0) ? std::max(0.0, now - busy) : 0.0;
      m.energy_j += (nd.p_busy_w * busy + nd.p_idle_w * idle) / 1000.0;
    }
  } else {
    m.makespan_ms = InfraGraph::INF;
    m.energy_j = InfraGraph::INF;
  }
  m.spt_updates = infra.spt_updates;
}

}  // namespace ds
