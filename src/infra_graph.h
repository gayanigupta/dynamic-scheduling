#pragma once

#include <vector>
#include <unordered_map>

namespace ds {

struct FogNode {
  int id;
  double mips;       // processing capacity
  double mem_mb;     // total memory
  double p_busy_w;   // power draw while executing
  double p_idle_w;   // power draw while idle
  bool alive = true;
};

struct Link {
  int a, b;            // endpoints (undirected)
  double lat_ms;       // propagation/queueing latency
  double bw_mbps;      // available bandwidth
  double cost_per_mb;  // monetary/energy cost per MB transferred
};

// Fog infrastructure graph. Maintains all-pairs shortest-path *latency* routing
// with lazy incremental updates: when a node fails, cached routes are NOT
// recomputed eagerly. Instead, a route query (i -> j) walks the cached
// next-hop chain; only if the cached path traverses a dead node is the
// single-source shortest-path tree for source i recomputed (Dijkstra). This
// mirrors Bhowmick's incremental SPT update idea: only the trees/paths affected
// by the change are touched.
struct InfraGraph {
  int n = 0;
  std::vector<FogNode> nodes;
  std::vector<Link> links;
  std::vector<std::vector<std::pair<int, int>>> adj;  // node -> (nbr, link idx)

  // Routing cache.
  std::vector<std::vector<double>> dist;      // latency ms, INF = unreachable
  std::vector<std::vector<int>> next_hop;     // next hop toward dest, -1 none
  long spt_updates = 0;   // number of single-source recomputations
  long path_queries = 0;  // total route queries served
  bool eager_repair = false;  // ablation: rebuild all SPT rows per failure

  void build(int n_nodes, std::vector<FogNode> node_list,
             std::vector<Link> link_list);

  void compute_all_pairs();             // initial Floyd-Warshall
  void fail_node(int f);                // mark dead; lazy repair, or eager
                                        // rebuild of all rows if eager_repair
  void recompute_row(int src);          // Dijkstra from src over alive nodes

  // Prim's minimum spanning tree over alive nodes, weighted by link latency.
  // Returns indices into `links`; empty if the alive graph is disconnected.
  std::vector<int> mst() const;

  bool alive(int u) const { return nodes[u].alive; }

  // Latency (ms) of the route i -> j; INF if unreachable/dead. Lazily repairs
  // stale routes. `spt_updates` counts incremental repair work.
  double route_latency(int i, int j);

  // The actual node sequence of the route (including i and j).
  std::vector<int> route_path(int i, int j);

  // Min-bandwidth (Mbps) bottleneck along the route i -> j.
  double route_bw(int i, int j);

  // Cost per MB summed over route links.
  double route_cost_per_mb(int i, int j);

  // Transfer time (ms) of `mb` megabytes i -> j.
  double transfer_ms(int i, int j, double mb) {
    if (i == j) return 0.0;
    double lat = route_latency(i, j);
    if (lat >= INF) return INF;
    double bw = route_bw(i, j);
    return lat + mb * 8.0 / bw * 1000.0;
  }

  int link_between(int a, int b) const;  // link index or -1

  static constexpr double INF = 1e18;

 private:
  std::unordered_map<long long, int> link_index_;  // a*n+b -> link idx
  bool ensure_route(int i, int j);  // lazily repair stale rows on i -> j
};

}  // namespace ds
