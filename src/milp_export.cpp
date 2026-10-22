#include "milp_export.h"

#include <cstdio>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace ds {

bool export_recovery_instance(Sim& s, int failed, int scenario,
                              const RecoveryParams& rp,
                              const std::string& path, double deadline_ms) {
  auto sub = affected_subgraph(s, failed, rp.expand_boundary);
  std::unordered_map<int, int> idx;  // task gid -> subgraph index
  for (size_t i = 0; i < sub.size(); ++i) idx[sub[i]] = (int)i;

  // Candidates: all alive nodes (shortlist flag marks the heuristic's picks).
  // Reuse the heuristic shortlist to flag preferred nodes.
  std::vector<int> cand_all;
  for (const auto& nd : s.infra.nodes)
    if (nd.alive) cand_all.push_back(nd.id);

  // Anchor sources: every node/edge location a subgraph task may read from.
  std::set<int> srcs;
  for (int t : sub)
    for (size_t k = 0; k < s.dag->in_edges[t].size(); ++k) {
      const auto& e = s.dag->edges[s.dag->in_edges[t][k]];
      if (idx.count(e.from)) continue;
      int src = s.edge_data_node(t, (int)k);
      if (src >= 0) srcs.insert(src);
    }

  FILE* f = fopen(path.c_str(), "w");
  if (!f) return false;

  fprintf(f, "{\n");
  fprintf(f,
          "  \"scenario\": %d,\n  \"failed_node\": %d,\n  \"now\": %.6f,\n"
          "  \"deadline_ms\": %.6f,\n",
          scenario, failed, s.now, deadline_ms);

  // Subgraph tasks.
  fprintf(f, "  \"tasks\": [\n");
  for (size_t i = 0; i < sub.size(); ++i) {
    int t = sub[i];
    const auto& tk = s.dag->tasks[t];
    fprintf(f,
            "    {\"gid\": %d, \"work_mi\": %.4f, \"mem_mb\": %.4f, "
            "\"context_mb\": %.4f, \"home\": %d, \"home_alive\": %s}%s\n",
            t, tk.work_mi, tk.mem_mb, tk.context_mb, s.assign[t],
            s.infra.alive(s.assign[t]) ? "true" : "false",
            i + 1 < sub.size() ? "," : "");
  }
  fprintf(f, "  ],\n");

  // Edges between subgraph tasks (parent and child both inside).
  fprintf(f, "  \"sub_edges\": [\n");
  bool first = true;
  for (int t : sub)
    for (int ei : s.dag->in_edges[t]) {
      const auto& e = s.dag->edges[ei];
      if (!idx.count(e.from)) continue;
      fprintf(f, "%s    {\"p\": %d, \"t\": %d, \"data_mb\": %.4f}",
              first ? "\n" : ",\n", idx[e.from], idx[t], e.data_mb);
      first = false;
    }
  fprintf(f, "%s  ],\n", first ? "" : "\n");

  // Edges from anchors (parents outside the subgraph): fixed fin + src.
  fprintf(f, "  \"anchor_edges\": [\n");
  first = true;
  for (int t : sub)
    for (size_t k = 0; k < s.dag->in_edges[t].size(); ++k) {
      const auto& e = s.dag->edges[s.dag->in_edges[t][k]];
      if (idx.count(e.from)) continue;
      fprintf(f,
              "%s    {\"t\": %d, \"data_mb\": %.4f, \"fin\": %.4f, "
              "\"src\": %d}",
              first ? "\n" : ",\n", idx[t], e.data_mb,
              anchor_fin_est(s, e.from), s.edge_data_node(t, (int)k));
      first = false;
    }
  fprintf(f, "%s  ],\n", first ? "" : "\n");

  // Candidate nodes.
  fprintf(f, "  \"candidates\": [\n");
  for (size_t i = 0; i < cand_all.size(); ++i) {
    int c = cand_all[i];
    const auto& nd = s.infra.nodes[c];
    fprintf(f,
            "    {\"gid\": %d, \"mips\": %.4f, \"mem_mb\": %.4f, "
            "\"avail\": %.4f}%s\n",
            c, nd.mips, nd.mem_mb, s.node_free[c],
            i + 1 < cand_all.size() ? "," : "");
  }
  fprintf(f, "  ],\n");

  // Route latency/bandwidth from every anchor source to every candidate,
  // and between candidates (for subgraph-internal edges).
  std::vector<int> all_src(srcs.begin(), srcs.end());
  all_src.insert(all_src.end(), cand_all.begin(), cand_all.end());
  fprintf(f, "  \"lat\": {\n");
  first = true;
  for (int a : all_src)
    for (int c : cand_all) {
      double d = s.infra.route_latency(a, c);
      if (d >= InfraGraph::INF) d = -1.0;
      fprintf(f, "%s    \"%d,%d\": %.4f", first ? "\n" : ",\n", a, c, d);
      first = false;
    }
  fprintf(f, "%s  },\n", first ? "" : "\n");
  fprintf(f, "  \"bw\": {\n");
  first = true;
  for (int a : all_src)
    for (int c : cand_all) {
      double b = (a == c) ? 1e18 : s.infra.route_bw(a, c);
      fprintf(f, "%s    \"%d,%d\": %.4f", first ? "\n" : ",\n", a, c, b);
      first = false;
    }
  fprintf(f, "%s  },\n", first ? "" : "\n");

  fprintf(f,
          "  \"params\": {\"store_lat_ms\": %.4f, \"store_bw_mbps\": %.4f, "
          "\"move_penalty_ms\": %.4f, \"deploy_bw_mbps\": %.4f}\n",
          s.params.store_lat_ms, s.params.store_bw_mbps, rp.move_penalty_ms,
          s.params.deploy_bw_mbps);
  fprintf(f, "}\n");
  fclose(f);
  return true;
}

}  // namespace ds
