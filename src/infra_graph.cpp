#include "infra_graph.h"

#include <algorithm>
#include <functional>
#include <queue>
#include <stdexcept>

namespace ds {

void InfraGraph::build(int n_nodes, std::vector<FogNode> node_list,
                       std::vector<Link> link_list) {
  n = n_nodes;
  nodes = std::move(node_list);
  links = std::move(link_list);
  adj.assign(n, {});
  link_index_.clear();
  for (int i = 0; i < (int)links.size(); ++i) {
    const auto& l = links[i];
    adj[l.a].push_back({l.b, i});
    adj[l.b].push_back({l.a, i});
    link_index_[(long long)l.a * n + l.b] = i;
    link_index_[(long long)l.b * n + l.a] = i;
  }
  compute_all_pairs();
}

void InfraGraph::compute_all_pairs() {
  dist.assign(n, std::vector<double>(n, INF));
  next_hop.assign(n, std::vector<int>(n, -1));
  for (int i = 0; i < n; ++i) {
    dist[i][i] = 0.0;
    next_hop[i][i] = i;
  }
  for (const auto& l : links) {
    if (l.lat_ms < dist[l.a][l.b]) {
      dist[l.a][l.b] = l.lat_ms;
      next_hop[l.a][l.b] = l.b;
    }
    if (l.lat_ms < dist[l.b][l.a]) {
      dist[l.b][l.a] = l.lat_ms;
      next_hop[l.b][l.a] = l.a;
    }
  }
  for (int k = 0; k < n; ++k)
    for (int i = 0; i < n; ++i) {
      if (dist[i][k] >= INF) continue;
      for (int j = 0; j < n; ++j) {
        double d = dist[i][k] + dist[k][j];
        if (d < dist[i][j]) {
          dist[i][j] = d;
          next_hop[i][j] = next_hop[i][k];
        }
      }
    }
}

void InfraGraph::fail_node(int f) {
  nodes[f].alive = false;
  // No eager recomputation: cached routes that traverse f are repaired lazily
  // on the next query that touches them (incremental SPT update).
}

void InfraGraph::recompute_row(int src) {
  // Dijkstra over alive nodes only; rebuilds the SPT rooted at src.
  ++spt_updates;
  std::vector<double> d(n, INF);
  std::vector<int> nh(n, -1), prev(n, -1);
  d[src] = 0.0;
  nh[src] = src;
  using P = std::pair<double, int>;
  std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
  pq.push({0.0, src});
  while (!pq.empty()) {
    auto [du, u] = pq.top();
    pq.pop();
    if (du != d[u]) continue;
    for (auto [v, li] : adj[u]) {
      if (!nodes[v].alive) continue;
      double nd = du + links[li].lat_ms;
      if (nd < d[v]) {
        d[v] = nd;
        prev[v] = u;
        pq.push({nd, v});
      }
    }
  }
  // next hop from src toward v = first node after src on path src..v.
  for (int v = 0; v < n; ++v) {
    if (v == src) {
      nh[v] = src;
      continue;
    }
    if (d[v] >= INF) {
      nh[v] = -1;
      continue;
    }
    int cur = v;
    while (prev[cur] != src) cur = prev[cur];
    nh[v] = cur;
  }
  dist[src] = d;
  next_hop[src] = nh;
}

// Walk the cached next-hop chain i -> j. Whenever a row's cached hop is
// missing or dead, recompute just that row's SPT (the incremental update).
// After all intermediate rows are consistent, the source row is refreshed
// once so dist[i][j]/next_hop[i][j] match the post-failure optimum, then one
// final pass verifies the (possibly changed) first hop. A recomputed row can
// never go stale again for the same failure set, so this terminates.
// Returns false if i and j are disconnected.
bool InfraGraph::ensure_route(int i, int j) {
  bool repaired = false;
  for (int outer = 0; outer <= 2 * n + 2; ++outer) {
    int cur = i;
    bool pass_repaired = false;
    for (int hops = 0; cur != j; ++hops) {
      if (hops > n || !nodes[cur].alive) return false;
      int nx = next_hop[cur][j];
      if (nx < 0 || !nodes[nx].alive) {
        recompute_row(cur);  // stale row: repair this SPT only
        pass_repaired = repaired = true;
        nx = next_hop[cur][j];
        if (nx < 0) return false;  // unreachable
      }
      cur = nx;
    }
    if (pass_repaired) continue;      // verify repaired chain again
    if (repaired) {                   // chain alive but source row may be stale
      recompute_row(i);
      repaired = false;
      continue;
    }
    return true;                      // clean pass, nothing repaired
  }
  return false;
}

double InfraGraph::route_latency(int i, int j) {
  ++path_queries;
  if (i == j) return nodes[i].alive ? 0.0 : INF;
  if (!nodes[i].alive || !nodes[j].alive) return INF;
  if (!ensure_route(i, j)) return INF;
  return dist[i][j];
}

std::vector<int> InfraGraph::route_path(int i, int j) {
  if (route_latency(i, j) >= INF) return {};
  std::vector<int> p{i};
  int cur = i;
  for (int hops = 0; cur != j; ++hops) {
    if (hops > n) return {};
    cur = next_hop[cur][j];
    p.push_back(cur);
  }
  return p;
}

double InfraGraph::route_bw(int i, int j) {
  if (i == j) return 1e18;
  auto p = route_path(i, j);
  if (p.size() < 2) return 0.0;
  double bw = 1e18;
  for (size_t k = 0; k + 1 < p.size(); ++k) {
    int li = link_between(p[k], p[k + 1]);
    if (li < 0) return 0.0;
    bw = std::min(bw, links[li].bw_mbps);
  }
  return bw;
}

double InfraGraph::route_cost_per_mb(int i, int j) {
  if (i == j) return 0.0;
  auto p = route_path(i, j);
  if (p.size() < 2) return INF;
  double c = 0.0;
  for (size_t k = 0; k + 1 < p.size(); ++k) {
    int li = link_between(p[k], p[k + 1]);
    if (li < 0) return INF;
    c += links[li].cost_per_mb;
  }
  return c;
}

int InfraGraph::link_between(int a, int b) const {
  auto it = link_index_.find((long long)a * n + b);
  return it == link_index_.end() ? -1 : it->second;
}

}  // namespace ds
