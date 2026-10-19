#include "scheduler.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace ds {

static double mean_exec(const TaskDAG& dag, const InfraGraph& infra, int t) {
  double sum = 0.0;
  int cnt = 0;
  for (const auto& nd : infra.nodes)
    if (nd.alive) {
      sum += dag.tasks[t].work_mi / nd.mips * 1000.0;
      ++cnt;
    }
  return cnt ? sum / cnt : 0.0;
}

std::vector<double> upward_ranks(const TaskDAG& dag, InfraGraph& infra,
                                 const std::vector<int>* fixed_node) {
  std::vector<double> rank(dag.n, 0.0);
  auto order = dag.topo_order();
  for (auto it = order.rbegin(); it != order.rend(); ++it) {
    int t = *it;
    double best_child = 0.0;
    for (int ei : dag.out_edges[t]) {
      const auto& e = dag.edges[ei];
      double comm = 0.0;
      if (fixed_node && (*fixed_node)[e.to] >= 0) {
        // Child pinned: comm from an average node is approximated by the mean
        // latency to the pinned node. Good enough for ranking.
        double sum = 0.0;
        int cnt = 0;
        for (const auto& nd : infra.nodes)
          if (nd.alive) {
            sum += infra.route_latency(nd.id, (*fixed_node)[e.to]);
            ++cnt;
          }
        comm = cnt ? sum / cnt : 0.0;
      } else {
        // Mean comm time across all alive node pairs.
        double sum = 0.0;
        int cnt = 0;
        for (const auto& a : infra.nodes)
          if (a.alive)
            for (const auto& b : infra.nodes)
              if (b.alive && a.id != b.id) {
                sum += infra.transfer_ms(a.id, b.id, e.data_mb);
                ++cnt;
              }
        comm = cnt ? sum / cnt : 0.0;
      }
      best_child = std::max(best_child, comm + rank[e.to]);
    }
    rank[t] = mean_exec(dag, infra, t) + best_child;
  }
  return rank;
}

InitialSchedule heft_schedule(const TaskDAG& dag, InfraGraph& infra) {
  InitialSchedule sched;
  sched.assign.assign(dag.n, -1);
  sched.est_fin.assign(dag.n, 0.0);

  auto rank = upward_ranks(dag, infra, nullptr);
  std::vector<int> order(dag.n);
  std::iota(order.begin(), order.end(), 0);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return rank[a] > rank[b]; });

  std::vector<double> avail(infra.n, 0.0);

  for (int t : order) {
    double best_eft = InfraGraph::INF;
    int best_node = -1;
    for (const auto& nd : infra.nodes) {
      if (!nd.alive || nd.mem_mb < dag.tasks[t].mem_mb) continue;
      double est = 0.0;  // earliest start due to parents + comm
      for (int ei : dag.in_edges[t]) {
        const auto& e = dag.edges[ei];
        int pn = sched.assign[e.from];
        double comm = (pn == nd.id) ? 0.0
                                    : infra.transfer_ms(pn, nd.id, e.data_mb);
        est = std::max(est, sched.est_fin[e.from] + comm);
      }
      double st = std::max(avail[nd.id], est);
      double eft = st + dag.tasks[t].work_mi / nd.mips * 1000.0;
      if (eft < best_eft) {
        best_eft = eft;
        best_node = nd.id;
      }
    }
    if (best_node < 0) throw std::runtime_error("no feasible node for task");
    sched.assign[t] = best_node;
    sched.est_fin[t] = best_eft;
    avail[best_node] = best_eft;
    sched.est_makespan = std::max(sched.est_makespan, best_eft);
  }
  return sched;
}

}  // namespace ds
