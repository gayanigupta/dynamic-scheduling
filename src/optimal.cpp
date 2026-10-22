#include <algorithm>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include "recovery.h"
#include "scheduler.h"

namespace ds {

namespace {

// Evaluate a complete/partial assignment of subset tasks to candidate nodes.
// Returns (est subgraph makespan + movement penalties). Uses the same
// cost model as the simulator: ready = max over parents(fin + transfer).
double eval_plan(Sim& s, const std::vector<int>& order,
                 const std::vector<int>& chosen,  // aligned with `order`
                 int depth,                       // how many are fixed
                 double move_penalty_ms) {
  std::unordered_map<int, double> avail;
  std::unordered_map<int, double> estfin;
  std::unordered_map<int, int> placed;
  double mk = 0.0, pen = 0.0;
  for (int i = 0; i < depth; ++i) {
    int t = order[i];
    int c = chosen[i];
    double est = 0.0;
    for (size_t k = 0; k < s.dag->in_edges[t].size(); ++k) {
      const auto& e = s.dag->edges[s.dag->in_edges[t][k]];
      double pfin;
      int pnode;
      if (placed.count(e.from)) {
        pfin = estfin[e.from];
        pnode = placed[e.from];
      } else {
        pfin = anchor_fin_est(s, e.from);
        pnode = s.edge_data_node(t, (int)k);
      }
      double comm = s.edge_transfer_ms(pnode, c, e.data_mb);
      est = std::max(est, pfin + comm);
    }
    double stt = std::max(avail.count(c) ? avail[c] : s.node_free[c], est);
    double fin = stt + s.dag->tasks[t].work_mi / s.infra.nodes[c].mips * 1000.0;
    avail[c] = fin;
    estfin[t] = fin;
    placed[t] = c;
    mk = std::max(mk, fin);
    int home = s.assign[t];
    if (s.infra.alive(home) && c != home)
      pen += move_penalty_ms + migration_ms(*s.dag, s.params, t);
  }
  return mk + pen;
}

struct BB {
  Sim& s;
  const RecoveryParams& rp;
  std::vector<int> order;
  std::vector<std::vector<int>> cand_of;
  std::vector<char> in_subset;
  std::vector<int> chosen, best_assign;
  double best = InfraGraph::INF;
  long evals = 0;
  bool capped = false;

  void dfs(int depth) {
    if (capped) return;
    if (++evals > rp.opt_max_evals) {
      capped = true;
      return;
    }
    if (depth == (int)order.size()) {
      double v = eval_plan(s, order, chosen, depth, rp.move_penalty_ms);
      if (v < best) {
        best = v;
        best_assign = chosen;
      }
      return;
    }
    int t = order[depth];
    for (int c : cand_of[depth]) {
      if (s.infra.nodes[c].mem_mb < s.dag->tasks[t].mem_mb) continue;
      chosen[depth] = c;
      // Lower bound: partial plan with committed tasks only.
      double lb = eval_plan(s, order, chosen, depth + 1, rp.move_penalty_ms);
      if (lb < best) dfs(depth + 1);
      if (capped) return;
    }
  }
};

}  // namespace

RecoveryResult recover_optimal(Sim& s, int failed, const RecoveryParams& rp) {
  double t0 = std::chrono::duration<double, std::milli>(
                  std::chrono::steady_clock::now().time_since_epoch())
                  .count();
  auto sub = affected_subgraph(s, failed, rp.expand_boundary);
  if ((int)sub.size() > rp.opt_max_tasks) {
    // Too large for exact search: fall back to the heuristic partial method.
    auto r = recover_partial(s, failed, rp);
    r.algo_wall_ms += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now().time_since_epoch())
                          .count() -
                      t0;
    return r;
  }
  std::vector<char> in(s.dag->n, 0);
  for (int t : sub) in[t] = 1;

  // Topo order keeps parents placed before children during the search.
  std::unordered_set<int> inset(sub.begin(), sub.end());
  std::vector<int> order;
  for (int t : s.dag->topo_order())
    if (inset.count(t)) order.push_back(t);

  // Candidates: all alive nodes (small cases only, so this stays cheap).
  BB bb{s, rp, order, {}, in, {}, {}, InfraGraph::INF, 0, false};
  bb.cand_of.assign(order.size(), {});
  bb.chosen.assign(order.size(), -1);
  for (size_t i = 0; i < order.size(); ++i) {
    int t = order[i];
    for (const auto& nd : s.infra.nodes)
      if (nd.alive) bb.cand_of[i].push_back(nd.id);
    if (s.infra.alive(s.assign[t]) &&
        std::find(bb.cand_of[i].begin(), bb.cand_of[i].end(), s.assign[t]) ==
            bb.cand_of[i].end())
      bb.cand_of[i].push_back(s.assign[t]);
  }
  bb.dfs(0);

  RecoveryResult r;
  if (!bb.best_assign.empty()) {
    for (size_t i = 0; i < order.size(); ++i)
      r.moves.push_back({order[i], bb.best_assign[i]});
  }
  r.tasks_considered = (long)sub.size();  // problem size; evals show in wall time
  r.replan_ms = rp.replan_base_ms + rp.replan_per_task_ms * sub.size();
  r.algo_wall_ms = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count() -
                   t0;
  return r;
}

}  // namespace ds
