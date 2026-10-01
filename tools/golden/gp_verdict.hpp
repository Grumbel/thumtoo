// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "Who wins?" judgement for golden-tool comparisons (archive backends,
// tile codecs, …). Pure data in, verdict out — no I/O dependencies beyond
// <ostream>, so it is unit-testable (tests/test_gp_verdict.cpp).
//
// Rules:
//   * Per metric, every candidate gets a cost ratio >= 1 relative to the best
//     candidate (1.0 = best). Lower-is-better: v / best. Higher-is-better:
//     best / v.
//   * Candidates within `tie_pct` percent of the best are tied with it.
//   * Noise: when candidates carry per-metric spreads (min/max of the timed
//     runs), a candidate is also tied with the best if even its best run is
//     no better than the best candidate's value (median): the measured gap
//     is within run-to-run noise. Deterministic metrics (bytes) use a
//     zero-width spread and are decided by the band alone.
//   * Overall score = geometric mean of a candidate's per-metric ratios
//     (all metrics weighted equally; scale-free so ms and bytes mix).
//     The same tie band applies to the overall score.
//
// Callers are responsible for only passing candidates whose results are
// valid (verified output, codec supported, …). A failed run must never be
// a candidate: it would "win" with zero time / zero bytes.

#pragma once

#include "gp_common.hpp"
#include "gp_json.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <ostream>
#include <string>
#include <vector>

namespace gp {

enum class Better { Lower, Higher };

struct MetricSpec {
  std::string name;
  Better better = Better::Lower;
};

struct Spread {
  double min = 0;
  double max = 0;
};

struct Candidate {
  std::string name;
  std::vector<double> values;  // aligned with the MetricSpec list (medians)
  std::vector<Spread> spread = {};  // empty, or aligned with values (min/max of runs)
};

struct Ranked {
  std::string name;
  double ratio = 1.0;  // >= 1; 1.0 = best
};

struct MetricVerdict {
  std::string metric;
  Better better = Better::Lower;
  std::vector<Ranked> ranking;  // best first
  // Names tied with the best (incl. best): within the band, or within noise.
  std::vector<std::string> tied;

  bool decided() const { return ranking.size() > 1 && tied.size() == 1; }
  const std::string& winner() const { return ranking.front().name; }
};

struct Verdict {
  double tie_pct = 5.0;
  std::vector<MetricVerdict> metrics;
  std::vector<Ranked> overall;  // geomean score ranking, best first
  std::vector<std::string> overall_tied;

  bool empty() const { return overall.empty(); }
  bool overall_decided() const {
    return overall.size() > 1 && overall_tied.size() == 1;
  }
};

namespace detail {

// Guard against 0 ms / 0 bytes producing infinities. Values this small are
// far below timer resolution and never meaningful as a "win".
inline constexpr double kFloor = 1e-9;

inline double cost_ratio(double v, double best, Better better) {
  v = std::max(v, kFloor);
  best = std::max(best, kFloor);
  return better == Better::Lower ? v / best : best / v;
}

inline std::vector<std::string> tie_band(const std::vector<Ranked>& ranking,
                                         double tie_pct) {
  std::vector<std::string> out;
  const double limit = 1.0 + tie_pct / 100.0;
  for (const auto& r : ranking) {
    if (r.ratio <= limit) out.push_back(r.name);
  }
  return out;
}

/// Is `c`'s best run no better than `best`'s typical (median) value?
inline bool within_noise(const Candidate& c, const Candidate& best, std::size_t m,
                         Better better) {
  if (c.spread.size() != c.values.size()) return false;
  const double typical = best.values[m];
  return better == Better::Lower ? c.spread[m].min <= typical
                                 : c.spread[m].max >= typical;
}

inline void sort_ranking(std::vector<Ranked>& ranking) {
  std::stable_sort(ranking.begin(), ranking.end(),
                   [](const Ranked& a, const Ranked& b) { return a.ratio < b.ratio; });
}

}  // namespace detail

/// Judge `candidates` on `metrics`. Candidates with a non-finite value or a
/// value count that does not match `metrics` are ignored.
inline Verdict judge(const std::vector<MetricSpec>& metrics,
                     const std::vector<Candidate>& candidates,
                     double tie_pct = 5.0) {
  Verdict v;
  v.tie_pct = tie_pct;

  std::vector<const Candidate*> valid;
  for (const auto& c : candidates) {
    if (c.values.size() != metrics.size()) continue;
    if (!c.spread.empty() && c.spread.size() != c.values.size()) continue;
    const bool finite = std::all_of(c.values.begin(), c.values.end(),
                                    [](double x) { return std::isfinite(x); });
    if (finite) valid.push_back(&c);
  }
  if (valid.empty() || metrics.empty()) return v;

  std::vector<double> log_sum(valid.size(), 0.0);
  for (std::size_t m = 0; m < metrics.size(); ++m) {
    const Better better = metrics[m].better;
    double best = valid.front()->values[m];
    for (const auto* c : valid) {
      const double x = c->values[m];
      best = better == Better::Lower ? std::min(best, x) : std::max(best, x);
    }
    MetricVerdict mv;
    mv.metric = metrics[m].name;
    mv.better = better;
    for (std::size_t i = 0; i < valid.size(); ++i) {
      const double r = detail::cost_ratio(valid[i]->values[m], best, better);
      mv.ranking.push_back({valid[i]->name, r});
      log_sum[i] += std::log(r);
    }
    detail::sort_ranking(mv.ranking);
    mv.tied = detail::tie_band(mv.ranking, tie_pct);
    const auto by_name = [&](const std::string& n) {
      return *std::find_if(valid.begin(), valid.end(),
                           [&](const Candidate* c) { return c->name == n; });
    };
    const Candidate* best_c = by_name(mv.ranking.front().name);
    for (const auto& r : mv.ranking) {
      if (std::find(mv.tied.begin(), mv.tied.end(), r.name) != mv.tied.end()) continue;
      if (detail::within_noise(*by_name(r.name), *best_c, m, better)) {
        mv.tied.push_back(r.name);
      }
    }
    v.metrics.push_back(std::move(mv));
  }

  for (std::size_t i = 0; i < valid.size(); ++i) {
    const double geo = std::exp(log_sum[i] / static_cast<double>(metrics.size()));
    v.overall.push_back({valid[i]->name, geo});
  }
  detail::sort_ranking(v.overall);
  // Normalize so the best overall score is exactly 1.0 (geomeans of
  // per-metric ratios need not include a candidate that is best everywhere).
  const double best_score = v.overall.front().ratio;
  for (auto& r : v.overall) r.ratio /= best_score;
  v.overall_tied = detail::tie_band(v.overall, tie_pct);
  return v;
}

// --- output -------------------------------------------------------------------

inline std::string join_names(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& n : names) {
    if (!out.empty()) out += " = ";
    out += n;
  }
  return out;
}

/// Human-readable verdict block.
inline void print_verdict(std::ostream& os, const Verdict& v) {
  if (v.empty()) {
    os << "verdict: no valid candidates\n";
    return;
  }
  if (v.overall.size() == 1) {
    os << "verdict: only one valid candidate (" << v.overall.front().name
       << ") — nothing to compare\n";
    return;
  }
  char line[256];
  os << "winner per metric (tie band " << v.tie_pct << "%):\n";
  for (const auto& m : v.metrics) {
    const char* dir = m.better == Better::Lower ? "lower" : "higher";
    if (m.decided()) {
      const Ranked& runner = m.ranking[1];
      std::snprintf(line, sizeof(line), "  %-24s %-14s %.2fx better than %s (%s is better)\n",
                    m.metric.c_str(), m.winner().c_str(), runner.ratio,
                    runner.name.c_str(), dir);
    } else {
      std::snprintf(line, sizeof(line), "  %-24s tie: %s\n", m.metric.c_str(),
                    join_names(m.tied).c_str());
    }
    os << line;
  }
  if (v.overall_decided()) {
    std::snprintf(line, sizeof(line), "overall: %s (geomean %.2fx better than %s)\n",
                  v.overall[0].name.c_str(), v.overall[1].ratio,
                  v.overall[1].name.c_str());
  } else {
    std::snprintf(line, sizeof(line), "overall: tie: %s\n",
                  join_names(v.overall_tied).c_str());
  }
  os << line;
}

inline void json_names(JsonWriter& w, const std::vector<std::string>& names) {
  w.begin_array(JsonWriter::Compact);
  for (const auto& n : names) w.value(n);
  w.end_array();
}

inline void json_ranking(JsonWriter& w, const std::vector<Ranked>& ranking) {
  w.begin_array(JsonWriter::Compact);
  for (const auto& r : ranking) {
    w.begin_object().field("name", r.name).field("ratio", r.ratio).end_object();
  }
  w.end_array();
}

/// Verdict as a JSON object. Fields: tie_pct; metrics{name: {better, winner,
/// tied, ranking}}; overall{winner, tied, ranking}. "winner" is "" when tied
/// or with fewer than two candidates; ranking ratios are >= 1 (1 = best).
inline void write_verdict_json(JsonWriter& w, const Verdict& v) {
  w.begin_object();
  w.field("tie_pct", v.tie_pct);
  w.key("metrics").begin_object();
  for (const auto& m : v.metrics) {
    w.key(m.metric).begin_object(JsonWriter::Compact);
    w.field("better", m.better == Better::Lower ? "lower" : "higher");
    w.field("winner", m.decided() ? m.winner() : std::string());
    w.key("tied");
    json_names(w, m.tied);
    w.key("ranking");
    json_ranking(w, m.ranking);
    w.end_object();
  }
  w.end_object();
  w.key("overall").begin_object(JsonWriter::Compact);
  w.field("winner", v.overall_decided() ? v.overall.front().name : std::string());
  w.key("tied");
  json_names(w, v.overall_tied);
  w.key("ranking");
  json_ranking(w, v.overall);
  w.end_object();
  w.end_object();
}

}  // namespace gp
