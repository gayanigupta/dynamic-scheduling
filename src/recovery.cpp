#include "recovery.h"

#include <algorithm>
#include <chrono>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "scheduler.h"

namespace ds {

static double wall_now() {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

std::vector<int> unfinished_on(const Sim& s, int node) {
  std::vector<int> out;
  for (int t = 0; t < s.dag->n; ++t)
    if (s.assign[t] == node && s.st[t] == TStatus::WAITING) out.push_back(t);
  return out;
}

std::vector<int> affected_subgraph(const Sim& s, int failed, bool expand) {
  std::unordered_set<int> sub;
  for (int t = 0; t < s.dag->n; ++t)
    if (s.assign[t] == failed && s.st[t] == TStatus::WAITING) sub.insert(t);

  if (expand) {
    // Fixpoint: pull in a waiting task on a healthy node when every parent
    // that still owes it data is inside the affected region.
    bool changed = true;
    while (changed) {
      changed = false;
      for (int t = 0; t < s.dag->n; ++t) {
        if (sub.count(t) || s.st[t] != TStatus::WAITING) continue;
        bool has_unfinished_parent = false, all_in_sub = true;
        for (int p : s.dag->parents[t]) {
          if (s.st[p] == TStatus::DONE) continue;
          if (s.st[p] == TStatus::RUNNING) {
            all_in_sub = false;
            break;
          }
          has_unfinished_parent = true;
          if (!sub.count(p)) {
            all_in_sub = false;
            break;
          }
        }
        if (has_unfinished_parent && all_in_sub) {
          sub.insert(t);
          changed = true;
        }
      }
    }
  }
  return std::vector<int>(sub.begin(), sub.end());
}

double anchor_fin_est(const Sim& s, int task) {
  if (s.st[task] == TStatus::DONE) return s.fin_time[task];
  if (s.st[task] == TStatus::RUNNING) return s.node_free[s.assign[task]];
  // WAITING anchor: crude estimate = its node frees up + its own exec.
  int nd = s.assign[task];
  double exec = s.infra.alive(nd)
                    ? s.dag->tasks[task].work_mi / s.infra.nodes[nd].mips * 1000.0
                    : 0.0;
  return (nd >= 0 ? s.node_free[nd] : 0.0) + exec;
}

std::vector<int> local_list_schedule(
    Sim& s, const std::vector<char>& in_subset,
    const std::vector<int>& candidates, const std::vector<int>& order,
    double move_penalty_ms, double* out_est_makespan) {
  std::unordered_map<int, double> avail;  // candidate node -> free-at
  for (int c : candidates) avail[c] = s.node_free[c];
  std::unordered_map<int, double> estfin;  // task -> est finish
  std::unordered_map<int, int> placed;     // task -> chosen node
  double mk = 0.0;

  for (int t : order) {
    if (!in_subset[t]) continue;
    int home = s.assign[t];
    bool home_dead = !s.infra.alive(home);
    double best = InfraGraph::INF;
    int bestc = -1;
    for (int c : candidates) {
      if (s.infra.nodes[c].mem_mb < s.dag->tasks[t].mem_mb) continue;
      double est = 0.0;
      for (size_t k = 0; k < s.dag->in_edges[t].size(); ++k) {
        const auto& e = s.dag->edges[s.dag->in_edges[t][k]];
        double pfin;
        int pnode;
        if (in_subset[e.from] && placed.count(e.from)) {
          pfin = estfin[e.from];
          pnode = placed[e.from];
        } else {
          pfin = anchor_fin_est(s, e.from);
          pnode = s.edge_data_node(t, (int)k);
        }
        double comm = s.edge_transfer_ms(pnode, c, e.data_mb);
        est = std::max(est, pfin + comm);
      }
      double stt = std::max(avail[c], est);
      double eft = stt + s.dag->tasks[t].work_mi / s.infra.nodes[c].mips * 1000.0;
      double pen = 0.0;
      if (!home_dead && c != home)
        pen = move_penalty_ms + migration_ms(*s.dag, s.params, t);
      if (eft + pen < best) {
        best = eft + pen;
        bestc = c;
      }
    }
    if (bestc < 0) continue;  // no feasible candidate; sim will flag abort
    double eft = best - ((!home_dead && bestc != home)
                             ? move_penalty_ms + migration_ms(*s.dag, s.params, t)
                             : 0.0);
    placed[t] = bestc;
    estfin[t] = eft;
    avail[bestc] = eft;
    mk = std::max(mk, eft);
  }
  std::vector<int> out(s.dag->n, -1);
  for (auto& kv : placed) out[kv.first] = kv.second;
  if (out_est_makespan) *out_est_makespan = mk;
  return out;
}

// Order subset tasks by decreasing upward rank (children outside the subset
// are treated as fixed sinks on their current node).
static std::vector<int> rank_order(Sim& s, const std::vector<int>& subset,
                                   const std::vector<char>& in_subset) {
  std::vector<int> fixed(s.dag->n, -1);
  for (int t = 0; t < s.dag->n; ++t)
    if (!in_subset[t]) fixed[t] = s.assign[t];  // anchors pinned
  auto rank = upward_ranks(*s.dag, s.infra, &fixed);
  std::vector<int> ord = subset;
  std::sort(ord.begin(), ord.end(),
            [&](int a, int b) { return rank[a] > rank[b]; });
  return ord;
}

// Approximate Steiner tree over the anchor nodes: take the MST of the
// metric closure (anchor-pair edges weighted by route latency), then expand
// each MST edge into its real shortest route. Nodes of that tree form the
// preferred replacement region -- repartitioned tasks land on a low-latency
// connected island instead of scattering across the federation.
static std::vector<int> steiner_region(
    Sim& s, const std::unordered_map<int, double>& vol) {
  std::vector<int> anchors;
  for (auto& kv : vol)
    if (kv.first >= 0 && s.infra.alive(kv.first)) anchors.push_back(kv.first);
  if (anchors.size() < 2) return anchors;

  std::vector<Link> closure;
  for (size_t i = 0; i < anchors.size(); ++i)
    for (size_t j = i + 1; j < anchors.size(); ++j) {
      double d = s.infra.route_latency(anchors[i], anchors[j]);
      if (d < InfraGraph::INF)
        closure.push_back({anchors[i], anchors[j], d, 1e9, 0.0});
    }
  InfraGraph clo;
  clo.build(s.infra.n, s.infra.nodes, closure);
  std::vector<int> region;
  std::vector<char> seen(s.infra.n, 0);
  for (int li : clo.mst())
    for (int u : s.infra.route_path(clo.links[li].a, clo.links[li].b))
      if (!seen[u]) {
        seen[u] = 1;
        region.push_back(u);
      }
  return region;
}

// Shortlist replacement nodes: proximity (by route latency) to the nodes
// hosting the subgraph's anchor parents and children, plus spare capacity.
static std::vector<int> shortlist_candidates(Sim& s,
                                             const std::vector<int>& subset,
                                             const RecoveryParams& rp) {
  std::unordered_set<int> sub(subset.begin(), subset.end());
  std::unordered_map<int, double> vol;  // anchor node -> total edge volume
  for (int t : subset) {
    for (int ei : s.dag->in_edges[t]) {
      const auto& e = s.dag->edges[ei];
      if (!sub.count(e.from)) vol[s.assign[e.from]] += e.data_mb;
    }
    for (int ei : s.dag->out_edges[t]) {
      const auto& e = s.dag->edges[ei];
      if (!sub.count(e.to)) vol[s.assign[e.to]] += e.data_mb;
    }
  }
  std::vector<int> cand;
  auto addc = [&](int u) {
    if (u >= 0 && s.infra.alive(u) &&
        std::find(cand.begin(), cand.end(), u) == cand.end())
      cand.push_back(u);
  };
  // Steiner region first: the low-latency connected island around anchors.
  if (rp.use_steiner)
    for (int u : steiner_region(s, vol)) addc(u);
  std::vector<std::pair<double, int>> score;
  for (const auto& nd : s.infra.nodes) {
    if (!nd.alive) continue;
    double prox = 0.0;
    for (auto [an, v] : vol) {
      if (an < 0 || !s.infra.alive(an)) continue;
      prox += v * s.infra.route_latency(an, nd.id);
    }
    score.push_back({prox + 0.01 * s.node_free[nd.id], nd.id});
  }
  std::sort(score.begin(), score.end());
  int n_scored = 0;
  for (auto& kv : score) {
    if (n_scored >= rp.max_candidates) break;
    if (std::find(cand.begin(), cand.end(), kv.second) == cand.end()) {
      cand.push_back(kv.second);
      ++n_scored;
    }
  }
  // Always keep the nodes already hosting healthy subset tasks (stay option).
  for (int t : subset) addc(s.assign[t]);
  return cand;
}

RecoveryResult recover_partial(Sim& s, int failed, const RecoveryParams& rp) {
  double t0 = wall_now();
  auto sub = affected_subgraph(s, failed, rp.expand_boundary);
  std::vector<char> in(s.dag->n, 0);
  for (int t : sub) in[t] = 1;
  auto cand = shortlist_candidates(s, sub, rp);
  auto order = rank_order(s, sub, in);
  auto na = local_list_schedule(s, in, cand, order, rp.move_penalty_ms);

  RecoveryResult r;
  for (int t : sub)
    if (na[t] >= 0) r.moves.push_back({t, na[t]});
  r.tasks_considered = (long)sub.size();
  r.replan_ms = rp.replan_base_ms + rp.replan_per_task_ms * sub.size();
  r.algo_wall_ms = wall_now() - t0;
  return r;
}

RecoveryResult recover_greedy(Sim& s, int failed, const RecoveryParams& rp) {
  double t0 = wall_now();
  auto sub = unfinished_on(s, failed);
  std::vector<char> in(s.dag->n, 0);
  for (int t : sub) in[t] = 1;
  std::vector<int> cand;
  for (const auto& nd : s.infra.nodes)
    if (nd.alive) cand.push_back(nd.id);
  auto order = rank_order(s, sub, in);
  // No stay option (home is dead anyway), no boundary expansion.
  auto na = local_list_schedule(s, in, cand, order, 0.0);
  RecoveryResult r;
  for (int t : sub)
    if (na[t] >= 0) r.moves.push_back({t, na[t]});
  r.tasks_considered = (long)sub.size();
  r.replan_ms = rp.replan_base_ms + rp.replan_per_task_ms * sub.size();
  r.algo_wall_ms = wall_now() - t0;
  return r;
}

RecoveryResult recover_static(Sim& s, int failed, const RecoveryParams& rp) {
  double t0 = wall_now();
  auto sub = unfinished_on(s, failed);
  int dst = s.backup_node;
  if (dst < 0 || !s.infra.alive(dst)) {
    double best = -1;
    for (const auto& nd : s.infra.nodes)
      if (nd.alive && nd.mips > best) {
        best = nd.mips;
        dst = nd.id;
      }
  }
  RecoveryResult r;
  for (int t : sub)
    if (dst >= 0) r.moves.push_back({t, dst});
  r.tasks_considered = (long)sub.size();
  r.replan_ms = rp.replan_base_ms + rp.replan_per_task_ms * sub.size();
  r.algo_wall_ms = wall_now() - t0;
  return r;
}

RecoveryResult recover_full(Sim& s, int failed, const RecoveryParams& rp) {
  (void)failed;
  double t0 = wall_now();
  std::vector<int> sub;
  std::vector<char> in(s.dag->n, 0);
  for (int t = 0; t < s.dag->n; ++t)
    if (s.st[t] == TStatus::WAITING) {
      sub.push_back(t);
      in[t] = 1;
    }
  std::vector<int> cand;
  for (const auto& nd : s.infra.nodes)
    if (nd.alive) cand.push_back(nd.id);
  auto order = rank_order(s, sub, in);
  // Full repartition may relocate any waiting task: no stay penalty.
  auto na = local_list_schedule(s, in, cand, order, 0.0);
  RecoveryResult r;
  for (int t : sub)
    if (na[t] >= 0) r.moves.push_back({t, na[t]});
  r.tasks_considered = (long)sub.size();
  r.replan_ms = rp.replan_base_ms + rp.replan_per_task_ms * sub.size();
  r.algo_wall_ms = wall_now() - t0;
  return r;
}

}  // namespace ds
