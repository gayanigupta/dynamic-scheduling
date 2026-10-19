#include "task_graph.h"

#include <algorithm>
#include <queue>
#include <stdexcept>

namespace ds {

void TaskDAG::build(int n_tasks, std::vector<TaskEdge> edge_list,
                    std::vector<Task> task_list) {
  n = n_tasks;
  tasks = std::move(task_list);
  edges = std::move(edge_list);
  children.assign(n, {});
  parents.assign(n, {});
  in_edges.assign(n, {});
  out_edges.assign(n, {});
  for (int i = 0; i < (int)edges.size(); ++i) {
    const auto& e = edges[i];
    if (e.from < 0 || e.from >= n || e.to < 0 || e.to >= n)
      throw std::runtime_error("edge endpoint out of range");
    children[e.from].push_back(e.to);
    parents[e.to].push_back(e.from);
    out_edges[e.from].push_back(i);
    in_edges[e.to].push_back(i);
  }
}

std::vector<int> TaskDAG::topo_order() const {
  std::vector<int> indeg(n, 0);
  for (const auto& e : edges) indeg[e.to]++;
  std::queue<int> q;
  for (int i = 0; i < n; ++i)
    if (indeg[i] == 0) q.push(i);
  std::vector<int> order;
  order.reserve(n);
  while (!q.empty()) {
    int u = q.front();
    q.pop();
    order.push_back(u);
    for (int v : children[u])
      if (--indeg[v] == 0) q.push(v);
  }
  return order;
}

std::unordered_set<int> TaskDAG::closure(const std::vector<int>& seeds,
                                         bool with_ancestors,
                                         bool with_descendants) const {
  std::unordered_set<int> out(seeds.begin(), seeds.end());
  std::vector<int> stack(seeds.begin(), seeds.end());
  while (!stack.empty()) {
    int u = stack.back();
    stack.pop_back();
    if (with_descendants) {
      for (int v : children[u])
        if (out.insert(v).second) stack.push_back(v);
    }
    if (with_ancestors) {
      for (int v : parents[u])
        if (out.insert(v).second) stack.push_back(v);
    }
  }
  return out;
}

}  // namespace ds
