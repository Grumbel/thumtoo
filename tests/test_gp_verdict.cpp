// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gp_verdict.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <sstream>

namespace {

int g_fails = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::cerr << "FAIL: " << msg << "\n";
    ++g_fails;
  }
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

}  // namespace

int main() {
  using gp::Better;

  const std::vector<gp::MetricSpec> lower2 = {{"toc_ms", Better::Lower},
                                              {"extract_ms", Better::Lower}};

  // Clear winner on both metrics.
  {
    auto v = gp::judge(lower2, {{"a", {1.0, 10.0}}, {"b", {2.0, 30.0}}}, 5.0);
    expect(v.metrics.size() == 2, "two metric verdicts");
    expect(v.metrics[0].decided() && v.metrics[0].winner() == "a", "a wins toc");
    expect(near(v.metrics[0].ranking[1].ratio, 2.0), "b is 2x on toc");
    expect(near(v.metrics[1].ranking[1].ratio, 3.0), "b is 3x on extract");
    expect(v.overall_decided() && v.overall[0].name == "a", "a wins overall");
    expect(near(v.overall[0].ratio, 1.0), "best overall normalized to 1");
    expect(near(v.overall[1].ratio, std::sqrt(6.0)), "geomean of 2 and 3");
  }

  // Tie band: 3% apart with 5% band -> tie; 3% apart with 1% band -> decided.
  {
    auto v = gp::judge({{"ms", Better::Lower}}, {{"a", {1.00}}, {"b", {1.03}}}, 5.0);
    expect(!v.metrics[0].decided(), "within band is a tie");
    expect(v.metrics[0].tied.size() == 2, "both tied");
    expect(!v.overall_decided(), "overall tie");
    auto w = gp::judge({{"ms", Better::Lower}}, {{"a", {1.00}}, {"b", {1.03}}}, 1.0);
    expect(w.metrics[0].decided(), "outside band is decided");
  }

  // Higher-is-better metric.
  {
    auto v = gp::judge({{"psnr", Better::Higher}}, {{"a", {30.0}}, {"b", {40.0}}}, 5.0);
    expect(v.metrics[0].winner() == "b", "higher wins");
    expect(near(v.metrics[0].ranking[1].ratio, 40.0 / 30.0), "higher ratio");
  }

  // Split decision: a wins one metric, b the other; overall by geomean.
  {
    auto v = gp::judge(lower2, {{"a", {1.0, 4.0}}, {"b", {2.0, 1.0}}}, 5.0);
    expect(v.metrics[0].winner() == "a" && v.metrics[1].winner() == "b", "split");
    // a: sqrt(1*4)=2, b: sqrt(2*1)=1.414 -> b wins overall
    expect(v.overall[0].name == "b", "geomean picks b");
    expect(near(v.overall[1].ratio, 2.0 / std::sqrt(2.0)), "normalized score");
  }

  // Invalid candidates are excluded, never winners.
  {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    auto v = gp::judge(lower2,
                       {{"ok", {1.0, 1.0}}, {"broken", {nan, 0.0}}, {"short", {0.5}}});
    expect(v.overall.size() == 1 && v.overall[0].name == "ok", "only valid candidate");
    std::ostringstream os;
    gp::print_verdict(os, v);
    expect(os.str().find("only one valid candidate") != std::string::npos,
           "single-candidate message");
  }

  // Zero values do not produce infinities.
  {
    auto v = gp::judge({{"ms", Better::Lower}}, {{"a", {0.0}}, {"b", {1.0}}});
    expect(std::isfinite(v.overall[1].ratio), "zero floor keeps ratios finite");
  }

  // Empty input.
  {
    auto v = gp::judge(lower2, {});
    expect(v.empty(), "no candidates -> empty");
    std::ostringstream os;
    gp::write_verdict_json(os, v, "");
    expect(os.str().find("\"winner\": \"\"") != std::string::npos, "empty winner json");
  }

  // JSON shape smoke.
  {
    auto v = gp::judge(lower2, {{"a", {1.0, 10.0}}, {"b", {2.0, 30.0}}});
    std::ostringstream os;
    gp::write_verdict_json(os, v, "  ");
    const std::string s = os.str();
    expect(s.find("\"toc_ms\": {\"better\": \"lower\", \"winner\": \"a\"") != std::string::npos,
           "metric json");
    expect(s.find("\"overall\": {\"winner\": \"a\"") != std::string::npos, "overall json");
  }

  if (g_fails) {
    std::cerr << g_fails << " failure(s)\n";
    return 1;
  }
  std::cout << "ok: gp_verdict\n";
  return 0;
}
