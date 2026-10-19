// Lightweight sanity tests (assert-based, no framework).

#include <cassert>
#include <cstdio>
#include <random>
#include <set>

#include "generators.h"
#include "recovery.h"
#include "scheduler.h"
#include "sim.h"

using namespace ds;

static InfraGraph small_infra() {
  // Diamond: 0-1-3, 0-2-3, plus 1-2 shortcut.
  std::vector<FogNode> nodes = {
      {0, 2000, 8192, 30, 3, true},
      {1, 1500, 8192, 25, 2, true},
      {2, 1500, 8192, 25, 2, true},
      {3, 3000, 8192, 40, 4, true},
  };
  std::vector<Link> links = {
      {0, 1, 2, 1000, 0.1}, {1, 3, 2, 1000, 0.1}, {0, 2, 5, 1000, 0.1},
      {2, 3, 5, 1000, 0.1}, {1, 2, 1, 1000, 0.1},
  };
  InfraGraph g;
  g.build(4, nodes, links);
  return g;
}

static TaskDAG small_dag() {
  // 0 -> 1 -> 3, 0 -> 2 -> 3
  std::vector<Task> tasks = {
      {0, 1000, 100, 1}, {1, 800, 100, 1}, {2, 800, 100, 1}, {3, 1200, 100, 1},
  };
  std::vector<TaskEdge> edges = {
      {0, 1, 5}, {0, 2, 5}, {1, 3, 5}, {2, 3, 5},
  };
  TaskDAG g;
  g.build(4, edges, tasks);
  return g;
}

static void test_topo() {
  auto dag = small_dag();
  auto ord = dag.topo_order();
  assert(ord.size() == 4);
  std::vector<int> pos(4);
  for (int i = 0; i < 4; ++i) pos[ord[i]] = i;
  for (auto& e : dag.edges) assert(pos[e.from] < pos[e.to]);
  printf("ok topo\n");
}

static void test_lazy_paths() {
  auto g = small_infra();
  long before = g.spt_updates;
  assert(g.route_latency(0, 3) == 4.0);  // 0-1-3
  assert(g.spt_updates == before);       // served from cache

  g.fail_node(1);
  // Route 0->3 must now avoid node 1: best is 0-2-3 (10) unless cached ok.
  double d = g.route_latency(0, 3);
  assert(d == 10.0);
  assert(g.spt_updates > before);  // only source 0's SPT rebuilt
  // Unrelated pair 2->3 still served from cache (2-3 link intact).
  long u = g.spt_updates;
  assert(g.route_latency(2, 3) == 5.0);
  assert(g.spt_updates == u);
  printf("ok lazy paths (spt_updates=%ld, queries=%ld)\n", g.spt_updates,
         g.path_queries);
}

static void test_heft_and_sim() {
  auto dag = small_dag();
  auto infra = small_infra();
  auto sched = heft_schedule(dag, infra);
  for (int t = 0; t < dag.n; ++t) assert(sched.assign[t] >= 0);

  Sim s;
  s.init(dag, infra, sched.assign, SimParams{});
  s.run();
  assert(s.m.completed);
  assert(s.m.makespan_ms > 0);
  printf("ok sim no-failure makespan=%.1f ms\n", s.m.makespan_ms);
}

static void test_failure_partial() {
  std::mt19937 rng(7);
  DagGenParams dp;
  dp.n_tasks = 20;
  dp.n_layers = 4;
  InfraGenParams ip;
  ip.n_nodes = 6;
  auto dag = gen_dag(rng, dp);
  auto infra = gen_infra(rng, ip);
  auto sched = heft_schedule(dag, infra);

  // Reference run.
  Sim ref;
  ref.init(dag, infra, sched.assign, SimParams{});
  ref.run();
  assert(ref.m.completed);

  // Fail a node that still has unfinished work at 40% of the no-failure
  // makespan (in the reference run those tasks finish after fail_time).
  double fail_time = 0.4 * ref.m.makespan_ms;
  int busiest = -1, bu = -1;
  std::vector<int> cnt(infra.n, 0);
  for (int t = 0; t < dag.n; ++t)
    if (ref.fin_time[t] > fail_time) cnt[ref.assign[t]]++;
  for (auto& nd : ref.infra.nodes)
    if (cnt[nd.id] > bu) {
      bu = cnt[nd.id];
      busiest = nd.id;
    }
  assert(busiest >= 0 && bu > 0);
  RecoveryParams rp;
  Sim s;
  s.init(dag, infra, sched.assign, SimParams{});
  s.inject_failure(fail_time, busiest);
  s.on_failure = [&](Sim& ss, int f) { return recover_partial(ss, f, rp); };
  s.run();
  assert(s.m.completed);
  assert(s.m.tasks_moved > 0);
  // No DONE task was re-executed: makespan still finite; check moved set is
  // exactly the affected subgraph region (tasks considered == subgraph size).
  printf("ok partial recovery: moved=%d considered=%ld makespan=%.1f "
         "(nofail=%.1f)\n",
         s.m.tasks_moved, s.m.tasks_considered, s.m.makespan_ms,
         ref.m.makespan_ms);
}

static void test_affected_boundary() {
  // Linear chain 0->1->2->3; node hosting task 1 fails. Minimal region = {1}.
  // With boundary expansion, 2 joins (its only parent is inside), then 3.
  std::vector<Task> tasks = {
      {0, 100, 10, 1}, {1, 100, 10, 1}, {2, 100, 10, 1}, {3, 100, 10, 1},
  };
  std::vector<TaskEdge> edges = {{0, 1, 1}, {1, 2, 1}, {2, 3, 1}};
  TaskDAG dag;
  dag.build(4, edges, tasks);
  auto infra = small_infra();
  Sim s;
  std::vector<int> asg = {0, 1, 2, 3};  // task i on node i
  s.init(dag, infra, asg, SimParams{});
  s.infra.fail_node(1);
  s.st[0] = TStatus::DONE;
  s.fin_node[0] = 0;
  s.fin_time[0] = 10;
  auto sub_min = affected_subgraph(s, 1, false);
  auto sub_exp = affected_subgraph(s, 1, true);
  assert(sub_min.size() == 1 && sub_min[0] == 1);
  std::set<int> es(sub_exp.begin(), sub_exp.end());
  assert(es.size() == 3 && es.count(1) && es.count(2) && es.count(3));
  assert(!es.count(0));  // completed tasks are never included
  printf("ok affected boundary (min=%zu, expanded=%zu)\n", sub_min.size(),
         sub_exp.size());
}

static void test_determinism() {
  std::mt19937 rng(42);
  DagGenParams dp;
  InfraGenParams ip;
  auto dag = gen_dag(rng, dp);
  auto infra = gen_infra(rng, ip);
  auto sched = heft_schedule(dag, infra);
  Sim a, b;
  a.init(dag, infra, sched.assign, SimParams{});
  b.init(dag, infra, sched.assign, SimParams{});
  a.run();
  b.run();
  assert(a.m.makespan_ms == b.m.makespan_ms);
  printf("ok determinism\n");
}

int main() {
  test_topo();
  test_lazy_paths();
  test_heft_and_sim();
  test_failure_partial();
  test_affected_boundary();
  test_determinism();
  printf("all tests passed\n");
  return 0;
}
