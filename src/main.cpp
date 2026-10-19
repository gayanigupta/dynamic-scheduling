// Experiment driver: generates random workflow DAGs and federated fog
// topologies, schedules the workflow, injects a resource failure mid-run, and
// compares recovery strategies on identical pre-failure states.
//
// Output: one CSV row per (scenario, strategy).

#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "generators.h"
#include "recovery.h"
#include "scheduler.h"
#include "sim.h"

using namespace ds;

namespace {

struct Config {
  int seeds = 10;
  std::vector<int> task_counts = {20, 40, 60};
  int n_nodes = 10;
  std::vector<double> fail_fracs = {0.3, 0.6};
  double deadline_factor = 1.3;
  bool run_optimal = true;
  std::string out = "results.csv";
  unsigned base_seed = 12345;
};

Config parse(int argc, char** argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        fprintf(stderr, "missing value for %s\n", name);
        exit(2);
      }
      return argv[++i];
    };
    if (a == "--seeds") c.seeds = std::stoi(next("--seeds"));
    else if (a == "--nodes") c.n_nodes = std::stoi(next("--nodes"));
    else if (a == "--tasks") {
      c.task_counts.clear();
      std::string v = next("--tasks");
      size_t pos = 0;
      while (pos <= v.size()) {
        size_t comma = v.find(',', pos);
        c.task_counts.push_back(
            std::stoi(v.substr(pos, comma - pos)));
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
    } else if (a == "--fail-fracs") {
      c.fail_fracs.clear();
      std::string v = next("--fail-fracs");
      size_t pos = 0;
      while (pos <= v.size()) {
        size_t comma = v.find(',', pos);
        c.fail_fracs.push_back(std::stod(v.substr(pos, comma - pos)));
        if (comma == std::string::npos) break;
        pos = comma + 1;
      }
    } else if (a == "--deadline-factor")
      c.deadline_factor = std::stod(next("--deadline-factor"));
    else if (a == "--no-optimal")
      c.run_optimal = false;
    else if (a == "--out")
      c.out = next("--out");
    else if (a == "--seed")
      c.base_seed = std::stoul(next("--seed"));
    else {
      fprintf(stderr, "unknown arg %s\n", a.c_str());
      exit(2);
    }
  }
  return c;
}

// Pick a node that actually hosts >=1 task (uniformly among such nodes).
int pick_failed_node(std::mt19937& rng, const Sim& s) {
  std::vector<int> hosting;
  for (const auto& nd : s.infra.nodes) {
    if (!nd.alive) continue;
    for (int t = 0; t < s.dag->n; ++t)
      if (s.assign[t] == nd.id) {
        hosting.push_back(nd.id);
        break;
      }
  }
  if (hosting.empty()) return -1;
  return hosting[rng() % hosting.size()];
}

}  // namespace

int main(int argc, char** argv) {
  Config cfg = parse(argc, argv);
  RecoveryParams rp;
  SimParams sp;
  sp.replan_base_ms = rp.replan_base_ms;
  sp.replan_per_task_ms = rp.replan_per_task_ms;

  FILE* f = fopen(cfg.out.c_str(), "w");
  if (!f) {
    perror("fopen");
    return 1;
  }
  fprintf(f,
          "scenario,seed,n_tasks,n_nodes,n_edges,fail_node,fail_frac,strategy,"
          "completed,makespan_ms,recovery_time_ms,tasks_moved,tasks_considered,"
          "comm_cost,data_mb,energy_j,spt_updates,algo_wall_ms,deadline_ms,"
          "deadline_met,quality\n");

  struct Strategy {
    const char* name;
    std::function<RecoveryResult(Sim&, int)> fn;
  };

  int scenario = 0;
  for (int n_tasks : cfg.task_counts) {
    for (double ff : cfg.fail_fracs) {
      for (int seed = 0; seed < cfg.seeds; ++seed) {
        std::mt19937 rng(cfg.base_seed + scenario * 7919);
        DagGenParams dp;
        dp.n_tasks = n_tasks;
        dp.n_layers = std::max(3, n_tasks / 7);
        InfraGenParams ip;
        ip.n_nodes = cfg.n_nodes;
        TaskDAG dag = gen_dag(rng, dp);
        InfraGraph infra = gen_infra(rng, ip);
        InitialSchedule init = heft_schedule(dag, infra);

        // Baseline run without failure -> makespan reference + deadline.
        Sim ref;
        ref.init(dag, infra, init.assign, sp);
        ref.run();
        double nofail_ms = ref.m.makespan_ms;
        double deadline = nofail_ms * cfg.deadline_factor;

        // Determine the failure from a probe state.
        std::mt19937 frng(cfg.base_seed + scenario * 104729);
        int fail_node = pick_failed_node(frng, ref);
        double fail_time = ff * nofail_ms;
        ++scenario;
        if (fail_node < 0) continue;

        std::vector<Strategy> strategies = {
            {"static",
             [&](Sim& s, int fn) { return recover_static(s, fn, rp); }},
            {"greedy",
             [&](Sim& s, int fn) { return recover_greedy(s, fn, rp); }},
            {"full",
             [&](Sim& s, int fn) { return recover_full(s, fn, rp); }},
            {"partial",
             [&](Sim& s, int fn) { return recover_partial(s, fn, rp); }},
        };
        if (cfg.run_optimal)
          strategies.push_back(
              {"optimal",
               [&](Sim& s, int fn) { return recover_optimal(s, fn, rp); }});

        for (auto& strat : strategies) {
          Sim sim;
          sim.init(dag, infra, init.assign, sp);
          sim.inject_failure(fail_time, fail_node);
          sim.on_failure = strat.fn;
          sim.run();
          const auto& m = sim.m;
          fprintf(f,
                  "%d,%d,%d,%d,%zu,%d,%.2f,%s,%d,%.3f,%.3f,%d,%ld,%.3f,%.3f,"
                  "%.3f,%ld,%.3f,%.3f,%d,%.4f\n",
                  scenario, seed, n_tasks, cfg.n_nodes, dag.edges.size(),
                  fail_node, ff, strat.name, m.completed ? 1 : 0,
                  m.makespan_ms >= InfraGraph::INF ? -1.0 : m.makespan_ms,
                  m.recovery_time_ms, m.tasks_moved, m.tasks_considered,
                  m.comm_cost, m.data_transferred_mb,
                  m.energy_j >= InfraGraph::INF ? -1.0 : m.energy_j,
                  m.spt_updates, m.algo_wall_ms, deadline,
                  (m.completed && m.makespan_ms <= deadline) ? 1 : 0,
                  m.completed ? m.makespan_ms / nofail_ms : -1.0);
        }
      }
    }
  }
  fclose(f);
  printf("wrote %s (%d scenarios)\n", cfg.out.c_str(), scenario);
  return 0;
}
