#include "generators.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace ds {

TaskDAG gen_dag(std::mt19937& rng, const DagGenParams& p) {
  std::uniform_real_distribution<double> work(p.work_min, p.work_max);
  std::uniform_real_distribution<double> mem(p.mem_min, p.mem_max);
  std::uniform_real_distribution<double> data(p.data_min, p.data_max);
  std::uniform_real_distribution<double> ctx(p.ctx_min, p.ctx_max);
  std::uniform_real_distribution<double> uni(0.0, 1.0);

  // Assign tasks to layers; first and last layers are always non-empty.
  int L = std::max(2, std::min(p.n_layers, p.n_tasks));
  std::vector<int> layer(p.n_tasks);
  layer[0] = 0;
  layer[p.n_tasks - 1] = L - 1;
  for (int i = 1; i + 1 < p.n_tasks; ++i)
    layer[i] = 1 + (int)(rng() % (L - 1));  // layers 1..L-1

  std::vector<std::vector<int>> earlier(p.n_tasks);
  std::vector<TaskEdge> edges;
  std::unordered_set<long long> seen;
  auto add_edge = [&](int a, int b) {
    long long k = (long long)a * p.n_tasks + b;
    if (a == b || layer[a] >= layer[b] || seen.count(k)) return;
    seen.insert(k);
    edges.push_back({a, b, data(rng)});
  };

  std::vector<int> indeg(p.n_tasks, 0);
  for (int i = 1; i < p.n_tasks; ++i) {
    // Candidate parents: every task in an earlier layer.
    std::vector<int> cands;
    for (int u = 0; u < p.n_tasks; ++u)
      if (layer[u] < layer[i]) cands.push_back(u);
    if (cands.empty()) { layer[i] = 0; continue; }
    add_edge(cands[rng() % cands.size()], i);  // guarantee >=1 parent
    ++indeg[i];
    for (int u : cands)
      if (uni(rng) < p.edge_prob) add_edge(u, i);
  }
  // Link childless non-sink tasks forward so the DAG stays connected.
  std::vector<int> outdeg(p.n_tasks, 0);
  for (auto& e : edges) outdeg[e.from]++;
  for (int u = 0; u < p.n_tasks; ++u) {
    if (outdeg[u] != 0 || layer[u] == L - 1) continue;
    std::vector<int> cands;
    for (int v = 0; v < p.n_tasks; ++v)
      if (layer[v] > layer[u]) cands.push_back(v);
    if (!cands.empty()) add_edge(u, cands[rng() % cands.size()]);
  }

  std::vector<Task> tasks(p.n_tasks);
  for (int i = 0; i < p.n_tasks; ++i)
    tasks[i] = {i, work(rng), mem(rng), ctx(rng)};

  TaskDAG g;
  g.build(p.n_tasks, std::move(edges), std::move(tasks));
  return g;
}

InfraGraph gen_infra(std::mt19937& rng, const InfraGenParams& p) {
  std::uniform_real_distribution<double> mips(p.mips_min, p.mips_max);
  std::uniform_real_distribution<double> mem(p.mem_min, p.mem_max);
  std::uniform_real_distribution<double> lat(p.lat_min, p.lat_max);
  std::uniform_real_distribution<double> bw(p.bw_min, p.bw_max);
  std::uniform_real_distribution<double> cost(p.cost_min, p.cost_max);
  std::uniform_real_distribution<double> pb(p.pbusy_min, p.pbusy_max);
  std::uniform_real_distribution<double> pi(p.pidle_min, p.pidle_max);
  std::uniform_real_distribution<double> uni(0.0, 1.0);

  std::vector<FogNode> nodes(p.n_nodes);
  for (int i = 0; i < p.n_nodes; ++i)
    nodes[i] = {i, mips(rng), mem(rng), pb(rng), pi(rng), true};

  std::vector<Link> links;
  // Spanning tree for connectivity.
  std::vector<int> perm(p.n_nodes);
  std::iota(perm.begin(), perm.end(), 0);
  std::shuffle(perm.begin(), perm.end(), rng);
  for (int i = 1; i < p.n_nodes; ++i) {
    int a = perm[rng() % i], b = perm[i];
    links.push_back({a, b, lat(rng), bw(rng), cost(rng)});
  }
  for (int i = 0; i < p.n_nodes; ++i)
    for (int j = i + 1; j < p.n_nodes; ++j)
      if (uni(rng) < p.link_prob) {
        bool dup = false;
        for (auto& l : links)
          if ((l.a == i && l.b == j) || (l.a == j && l.b == i)) dup = true;
        if (!dup) links.push_back({i, j, lat(rng), bw(rng), cost(rng)});
      }

  InfraGraph g;
  g.build(p.n_nodes, std::move(nodes), std::move(links));
  return g;
}

}  // namespace ds
