// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden-path archive timings (no thumtoo Client/Store).
// Backends: libarchive (always); libunarr when built with THUMTOO_HAVE_UNARR.
// unarr reads ZIP, TAR, RAR4 (incl. solid) and — if libunarr was built with
// the 7z SDK — 7z. RAR5 is libarchive-only.
//
// Usage:
//   thumtoo-gp-archive [--repeat N] [--backend B] [--tie-pct P] [--json] ARCHIVE
//
//   B = auto        thumtoo dispatcher choice (unarr for RAR4, else libarchive)
//     | libarchive | unarr
//     | all         every compiled-in backend, then judge which wins
//     | a,b,…       explicit comparison list
//
// Every backend is verified before it is timed: extracted bytes must match
// the sizes its own TOC declares, and in comparison mode all backends must
// agree on member count and total bytes. A backend that fails verification
// is reported but never ranked.

#include <archive.h>
#include <archive_entry.h>

#include "gp_common.hpp"
#include "gp_verdict.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(THUMTOO_HAVE_UNARR) && THUMTOO_HAVE_UNARR
#include <unarr.h>
#define GP_ARCHIVE_HAVE_UNARR 1
#else
#define GP_ARCHIVE_HAVE_UNARR 0
#endif

namespace {

namespace fs = std::filesystem;

// --- format sniffing ------------------------------------------------------------

enum class Format { Zip, Rar4, Rar5, SevenZip, Tar, Other };

const char* format_name(Format f) {
  switch (f) {
    case Format::Zip: return "zip";
    case Format::Rar4: return "rar4";
    case Format::Rar5: return "rar5";
    case Format::SevenZip: return "7z";
    case Format::Tar: return "tar";
    case Format::Other: return "other";
  }
  return "other";
}

/// Container format by magic bytes (extension is not trusted: .cbz/.cbr lie).
/// Compressed tarballs (tar.gz, …) report Other: only libarchive has filters.
Format sniff_format(const fs::path& path) {
  std::FILE* f = std::fopen(path.string().c_str(), "rb");
  if (!f) return Format::Other;
  unsigned char buf[512] = {};
  const std::size_t n = std::fread(buf, 1, sizeof(buf), f);
  std::fclose(f);
  auto starts = [&](const char* magic, std::size_t len) {
    return n >= len && std::memcmp(buf, magic, len) == 0;
  };
  if (starts("PK\x03\x04", 4) || starts("PK\x05\x06", 4)) return Format::Zip;
  if (starts("Rar!\x1a\x07\x01\x00", 8)) return Format::Rar5;
  if (starts("Rar!\x1a\x07\x00", 7)) return Format::Rar4;
  if (starts("7z\xbc\xaf\x27\x1c", 6)) return Format::SevenZip;
  if (n >= 262 && std::memcmp(buf + 257, "ustar", 5) == 0) return Format::Tar;
  return Format::Other;
}

// --- backend interface ----------------------------------------------------------

struct Member {
  std::string path;
  std::optional<std::uint64_t> size;  // nullopt: TOC does not declare it
};

/// One archive library, used the obvious way: open, walk, read.
/// Every call re-opens the archive (that is the cost being measured).
class Backend {
 public:
  virtual ~Backend() = default;
  virtual const char* name() const = 0;
  /// Reason this backend cannot read `format`, or "" when it should.
  virtual std::string unsupported_reason(Format format) const = 0;
  /// True when support for `format` depends on how the library was built,
  /// so an open failure means "not compiled in" rather than "bad archive".
  virtual bool build_dependent(Format) const { return false; }
  /// Regular-file members in archive order; nullopt if the archive cannot be opened.
  virtual std::optional<std::vector<Member>> list() = 0;
  /// Bytes of one member; nullopt on open/read error or member not found.
  virtual std::optional<std::uint64_t> extract_one(const std::string& member) = 0;
  /// Bytes of all members in one sequential pass; nullopt on error.
  virtual std::optional<std::uint64_t> extract_all() = 0;
};

// --- libarchive -----------------------------------------------------------------

class LibarchiveBackend final : public Backend {
 public:
  explicit LibarchiveBackend(fs::path path) : path_(std::move(path)) {}
  const char* name() const override { return "libarchive"; }
  std::string unsupported_reason(Format) const override { return {}; }

  std::optional<std::vector<Member>> list() override {
    Handle h(path_);
    if (!h.ok()) return std::nullopt;
    std::vector<Member> out;
    struct archive_entry* entry = nullptr;
    int rc = ARCHIVE_OK;
    while ((rc = archive_read_next_header(h.a, &entry)) == ARCHIVE_OK) {
      const char* p = archive_entry_pathname(entry);
      if (p && archive_entry_filetype(entry) == AE_IFREG) {
        Member m;
        m.path = p;
        if (archive_entry_size_is_set(entry)) {
          m.size = static_cast<std::uint64_t>(archive_entry_size(entry));
        }
        out.push_back(std::move(m));
      }
      archive_read_data_skip(h.a);
    }
    if (rc != ARCHIVE_EOF) return std::nullopt;
    return out;
  }

  std::optional<std::uint64_t> extract_one(const std::string& member) override {
    Handle h(path_);
    if (!h.ok()) return std::nullopt;
    struct archive_entry* entry = nullptr;
    while (archive_read_next_header(h.a, &entry) == ARCHIVE_OK) {
      const char* p = archive_entry_pathname(entry);
      if (p && member == p && archive_entry_filetype(entry) == AE_IFREG) {
        return drain(h.a);
      }
      archive_read_data_skip(h.a);
    }
    return std::nullopt;
  }

  std::optional<std::uint64_t> extract_all() override {
    Handle h(path_);
    if (!h.ok()) return std::nullopt;
    struct archive_entry* entry = nullptr;
    std::uint64_t bytes = 0;
    int rc = ARCHIVE_OK;
    while ((rc = archive_read_next_header(h.a, &entry)) == ARCHIVE_OK) {
      if (archive_entry_filetype(entry) != AE_IFREG) continue;
      auto n = drain(h.a);
      if (!n) return std::nullopt;
      bytes += *n;
    }
    if (rc != ARCHIVE_EOF) return std::nullopt;
    return bytes;
  }

 private:
  struct Handle {
    struct archive* a = nullptr;
    bool opened = false;
    explicit Handle(const fs::path& path) : a(archive_read_new()) {
      archive_read_support_filter_all(a);
      archive_read_support_format_all(a);
      opened = archive_read_open_filename(a, path.string().c_str(), 10240) == ARCHIVE_OK;
    }
    ~Handle() { archive_read_free(a); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool ok() const { return opened; }
  };

  static std::optional<std::uint64_t> drain(struct archive* a) {
    char buf[16384];
    std::uint64_t bytes = 0;
    for (;;) {
      const la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
      if (n == 0) return bytes;
      if (n < 0) return std::nullopt;
      bytes += static_cast<std::uint64_t>(n);
    }
  }

  fs::path path_;
};

// --- unarr (optional) -----------------------------------------------------------

#if GP_ARCHIVE_HAVE_UNARR

class UnarrBackend final : public Backend {
 public:
  UnarrBackend(fs::path path, Format format)
      : path_(std::move(path)), format_(format) {}
  const char* name() const override { return "unarr"; }

  std::string unsupported_reason(Format format) const override {
    switch (format) {
      case Format::Zip:
      case Format::Rar4:
      case Format::Tar:
      case Format::SevenZip:  // see build_dependent()
        return {};
      case Format::Rar5: return "unarr does not support RAR5";
      case Format::Other: return "unarr has no reader for this container/filter";
    }
    return "unknown format";
  }

  bool build_dependent(Format format) const override {
    return format == Format::SevenZip;  // needs libunarr built with HAVE_7Z
  }

  std::optional<std::vector<Member>> list() override {
    Handle h(path_, format_);
    if (!h.ok()) return std::nullopt;
    std::vector<Member> out;
    while (ar_parse_entry(h.ar)) {
      const char* name = ar_entry_get_name(h.ar);
      if (!name || is_dir_name(name)) continue;
      out.push_back(Member{name, static_cast<std::uint64_t>(ar_entry_get_size(h.ar))});
    }
    if (!ar_at_eof(h.ar)) return std::nullopt;
    return out;
  }

  std::optional<std::uint64_t> extract_one(const std::string& member) override {
    Handle h(path_, format_);
    if (!h.ok()) return std::nullopt;
    while (ar_parse_entry(h.ar)) {
      const char* name = ar_entry_get_name(h.ar);
      if (name && member == name) return uncompress_current(h.ar);
    }
    return std::nullopt;
  }

  std::optional<std::uint64_t> extract_all() override {
    Handle h(path_, format_);
    if (!h.ok()) return std::nullopt;
    std::uint64_t bytes = 0;
    while (ar_parse_entry(h.ar)) {
      const char* name = ar_entry_get_name(h.ar);
      if (!name || is_dir_name(name)) continue;
      auto n = uncompress_current(h.ar);
      if (!n) return std::nullopt;
      bytes += *n;
    }
    if (!ar_at_eof(h.ar)) return std::nullopt;
    return bytes;
  }

 private:
  struct Handle {
    ar_stream* stream = nullptr;
    ar_archive* ar = nullptr;
    Handle(const fs::path& path, Format format) {
      stream = ar_open_file(path.string().c_str());
      if (!stream) return;
      switch (format) {
        case Format::Zip: ar = ar_open_zip_archive(stream, false); break;
        case Format::Rar4: ar = ar_open_rar_archive(stream); break;
        case Format::Tar: ar = ar_open_tar_archive(stream); break;
        case Format::SevenZip: ar = ar_open_7z_archive(stream); break;
        case Format::Rar5:
        case Format::Other: break;
      }
    }
    ~Handle() {
      if (ar) ar_close_archive(ar);
      if (stream) ar_close(stream);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    bool ok() const { return ar != nullptr; }
  };

  static bool is_dir_name(const char* name) {
    const std::size_t len = std::strlen(name);
    return len > 0 && (name[len - 1] == '/' || name[len - 1] == '\\');
  }

  static std::optional<std::uint64_t> uncompress_current(ar_archive* ar) {
    const std::size_t declared = ar_entry_get_size(ar);
    if (declared == 0) return 0;
    std::vector<unsigned char> buf(declared);
    if (!ar_entry_uncompress(ar, buf.data(), declared)) return std::nullopt;
    return declared;
  }

  fs::path path_;
  Format format_;
};

#endif  // GP_ARCHIVE_HAVE_UNARR

std::vector<std::string> compiled_backends() {
  std::vector<std::string> out{"libarchive"};
#if GP_ARCHIVE_HAVE_UNARR
  out.push_back("unarr");
#endif
  return out;
}

std::unique_ptr<Backend> make_backend(const std::string& name, const fs::path& path,
                                      Format format) {
  if (name == "libarchive") return std::make_unique<LibarchiveBackend>(path);
#if GP_ARCHIVE_HAVE_UNARR
  if (name == "unarr") return std::make_unique<UnarrBackend>(path, format);
#else
  (void)format;
#endif
  return nullptr;
}

/// Mirrors thumtoo's dispatcher (archive_prefers_unarr): unarr for RAR4 only.
std::string auto_backend(Format format) {
#if GP_ARCHIVE_HAVE_UNARR
  if (format == Format::Rar4) return "unarr";
#else
  (void)format;
#endif
  return "libarchive";
}

// --- measurement ----------------------------------------------------------------

enum class Status { Ok, Unsupported, Failed };

const char* status_name(Status s) {
  switch (s) {
    case Status::Ok: return "ok";
    case Status::Unsupported: return "unsupported";
    case Status::Failed: return "failed";
  }
  return "failed";
}

constexpr int kScatterCount = 10;

struct Result {
  std::string backend;
  Status status = Status::Failed;
  std::string reason;
  std::size_t members = 0;
  std::uint64_t bytes = 0;  // verified extract-all total
  gp::Timing toc, all, first, last, scattered;
};

const std::vector<gp::MetricSpec>& metric_specs() {
  static const std::vector<gp::MetricSpec> specs = {
      {"toc_ms", gp::Better::Lower},
      {"extract_all_ms", gp::Better::Lower},
      {"extract_first_ms", gp::Better::Lower},
      {"extract_last_ms", gp::Better::Lower},
      {"extract_scattered10_ms", gp::Better::Lower},
  };
  return specs;
}

std::vector<double> metric_values(const Result& r) {
  return {r.toc.median, r.all.median, r.first.median, r.last.median,
          r.scattered.median};
}

std::vector<std::size_t> scattered_indices(std::size_t n) {
  std::vector<std::size_t> out;
  const std::size_t step = std::max<std::size_t>(1, n / kScatterCount);
  for (std::size_t i = 0; i < n; i += step) out.push_back(i);
  return out;
}

/// Verify `b` reads the archive correctly, then time it.
Result measure(Backend& b, Format format, int repeats) {
  Result r;
  r.backend = b.name();
  if (auto why = b.unsupported_reason(format); !why.empty()) {
    r.status = Status::Unsupported;
    r.reason = why;
    return r;
  }
  auto listed = b.list();
  if (!listed) {
    if (b.build_dependent(format)) {
      r.status = Status::Unsupported;
      r.reason = std::string(b.name()) + " cannot open " + format_name(format) +
                 " (this libunarr build may lack 7z support)";
    } else {
      r.status = Status::Failed;
      r.reason = "could not read the " + std::string(format_name(format)) +
                 " archive (damaged or truncated?)";
    }
    return r;
  }
  const std::vector<Member>& members = *listed;
  if (members.empty()) {
    r.status = Status::Failed;
    r.reason = "no regular-file members";
    return r;
  }
  r.members = members.size();

  // Verification: one full pass plus the single-member probes we will time.
  std::uint64_t declared = 0;
  bool all_declared = true;
  for (const auto& m : members) {
    if (m.size) declared += *m.size;
    else all_declared = false;
  }
  const auto all_bytes = b.extract_all();
  if (!all_bytes) {
    r.status = Status::Failed;
    r.reason = "extract-all failed";
    return r;
  }
  if (all_declared && *all_bytes != declared) {
    std::ostringstream os;
    os << "extract-all produced " << *all_bytes << " bytes, TOC declares " << declared;
    r.status = Status::Failed;
    r.reason = os.str();
    return r;
  }
  for (const Member* m : {&members.front(), &members.back()}) {
    const auto got = b.extract_one(m->path);
    if (!got || (m->size && *got != *m->size)) {
      r.status = Status::Failed;
      r.reason = "extract of member '" + m->path + "' failed or was short";
      return r;
    }
  }
  r.bytes = *all_bytes;

  const auto scatter = scattered_indices(members.size());
  const int heavy = std::max(1, repeats / 2);
  r.toc = gp::time_median(repeats, [&] { (void)b.list(); });
  r.all = gp::time_median(heavy, [&] { (void)b.extract_all(); });
  r.first = gp::time_median(repeats, [&] { (void)b.extract_one(members.front().path); });
  r.last = gp::time_median(repeats, [&] { (void)b.extract_one(members.back().path); });
  r.scattered = gp::time_median(heavy, [&] {
    for (std::size_t i : scatter) (void)b.extract_one(members[i].path);
  });
  r.status = Status::Ok;
  return r;
}

/// In comparison mode every verified backend must see the same archive.
/// Returns "" when consistent, else a description of the disagreement.
std::string cross_check(const std::vector<Result>& results) {
  const Result* ref = nullptr;
  for (const auto& r : results) {
    if (r.status != Status::Ok) continue;
    if (!ref) {
      ref = &r;
      continue;
    }
    if (r.members != ref->members || r.bytes != ref->bytes) {
      std::ostringstream os;
      os << ref->backend << " sees " << ref->members << " members / " << ref->bytes
         << " bytes, " << r.backend << " sees " << r.members << " members / "
         << r.bytes << " bytes";
      return os.str();
    }
  }
  return {};
}

// --- output ---------------------------------------------------------------------

void print_metrics_text(const Result& r) {
  std::printf("toc_ms=%.3f\n", r.toc.median);
  std::printf("extract_all_ms=%.3f\n", r.all.median);
  std::printf("extract_first_ms=%.3f\n", r.first.median);
  std::printf("extract_last_ms=%.3f\n", r.last.median);
  std::printf("extract_scattered10_ms=%.3f\n", r.scattered.median);
}

void write_metrics_json(std::ostream& os, const Result& r, const std::string& indent) {
  os << "{\n"
     << indent << "  \"toc_ms\": " << r.toc.median << ",\n"
     << indent << "  \"extract_all_ms\": " << r.all.median << ",\n"
     << indent << "  \"extract_first_ms\": " << r.first.median << ",\n"
     << indent << "  \"extract_last_ms\": " << r.last.median << ",\n"
     << indent << "  \"extract_scattered10_ms\": " << r.scattered.median << "\n"
     << indent << "}";
}

void write_header_json(std::ostream& os, const fs::path& path, Format format) {
  os << "  \"tool\": \"thumtoo-gp-archive\",\n  \"archive\": ";
  gp::json_string(os, path.string());
  os << ",\n  \"format\": \"" << format_name(format) << "\",\n"
     << "  \"unarr_built\": " << (GP_ARCHIVE_HAVE_UNARR ? "true" : "false") << ",\n";
}

/// Single-backend output. Keys are kept stable for checked-in baselines.
int report_single(const Result& r, const fs::path& path, Format format, bool json) {
  if (r.status != Status::Ok) {
    std::cerr << r.backend << ": " << status_name(r.status) << ": " << r.reason
              << " (" << path.string() << ")\n";
    return 1;
  }
  if (!json) {
    std::cout << "archive=" << path.filename().string() << " format="
              << format_name(format) << " backend=" << r.backend
              << " members=" << r.members << "\n";
    print_metrics_text(r);
    return 0;
  }
  std::cout << "{\n  \"schema\": 1,\n";
  write_header_json(std::cout, path, format);
  std::cout << "  \"backend\": \"" << r.backend << "\",\n"
            << "  \"members\": " << r.members << ",\n  \"metrics\": ";
  write_metrics_json(std::cout, r, "  ");
  std::cout << "\n}\n";
  return 0;
}

int report_compare(const std::vector<Result>& results, const fs::path& path,
                   Format format, int repeats, double tie_pct, bool json) {
  const std::string mismatch = cross_check(results);
  std::vector<gp::Candidate> candidates;
  if (mismatch.empty()) {
    for (const auto& r : results) {
      if (r.status == Status::Ok) candidates.push_back({r.backend, metric_values(r)});
    }
  }
  const gp::Verdict verdict = gp::judge(metric_specs(), candidates, tie_pct);

  if (!json) {
    std::cout << "archive=" << path.filename().string()
              << " format=" << format_name(format) << " repeats=" << repeats << "\n";
    std::printf("%-12s %8s %10s %14s %16s %15s %22s\n", "backend", "members",
                "toc_ms", "extract_all_ms", "extract_first_ms", "extract_last_ms",
                "extract_scattered10_ms");
    for (const auto& r : results) {
      if (r.status == Status::Ok) {
        std::printf("%-12s %8zu %10.3f %14.3f %16.3f %15.3f %22.3f\n",
                    r.backend.c_str(), r.members, r.toc.median, r.all.median,
                    r.first.median, r.last.median, r.scattered.median);
      } else {
        std::printf("%-12s %s: %s\n", r.backend.c_str(), status_name(r.status),
                    r.reason.c_str());
      }
    }
    if (!mismatch.empty()) {
      std::cout << "verdict: refused — backends disagree: " << mismatch << "\n";
    } else {
      gp::print_verdict(std::cout, verdict);
    }
  } else {
    std::cout << "{\n  \"schema\": 1,\n  \"kind\": \"compare\",\n";
    write_header_json(std::cout, path, format);
    std::cout << "  \"repeats\": " << repeats << ",\n  \"variants\": [";
    for (std::size_t i = 0; i < results.size(); ++i) {
      const Result& r = results[i];
      std::cout << (i ? ",\n" : "\n") << "    {\"backend\": \"" << r.backend
                << "\", \"status\": \"" << status_name(r.status) << "\"";
      if (r.status == Status::Ok) {
        std::cout << ", \"members\": " << r.members << ", \"bytes\": " << r.bytes
                  << ", \"metrics\": ";
        write_metrics_json(std::cout, r, "    ");
      } else {
        std::cout << ", \"reason\": ";
        gp::json_string(std::cout, r.reason);
      }
      std::cout << "}";
    }
    std::cout << "\n  ],\n  \"consistency_error\": ";
    gp::json_string(std::cout, mismatch);
    std::cout << ",\n  \"verdict\": ";
    gp::write_verdict_json(std::cout, verdict, "  ");
    std::cout << "\n}\n";
  }
  // Unsupported backends are expected (e.g. unarr on RAR5); a disagreement or
  // nothing measurable is an error.
  return (mismatch.empty() && !candidates.empty()) ? 0 : 1;
}

std::vector<std::string> split_list(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, ',')) {
    if (!part.empty() && std::find(out.begin(), out.end(), part) == out.end()) {
      out.push_back(part);
    }
  }
  return out;
}

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " [--repeat N] [--backend auto|libarchive|unarr|all|a,b] "
               "[--tie-pct P] [--json] ARCHIVE\n"
            << "Golden-path archive TOC / sequential / random member extract.\n"
            << "--backend all (or a list) times every backend and reports which "
               "wins per metric and overall.\n"
            << "Backends in this build:";
  for (const auto& b : compiled_backends()) std::cerr << ' ' << b;
  std::cerr << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  int repeats = 5;
  bool json_out = false;
  double tie_pct = 5.0;
  std::string backend_flag = "auto";
  fs::path path;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--repeat" && i + 1 < argc) {
      repeats = std::max(1, std::atoi(argv[++i]));
    } else if (a == "--backend" && i + 1 < argc) {
      backend_flag = argv[++i];
    } else if (a == "--tie-pct" && i + 1 < argc) {
      tie_pct = std::max(0.0, std::atof(argv[++i]));
    } else if (a == "--json") {
      json_out = true;
    } else if (a == "-h" || a == "--help") {
      usage(argv[0]);
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "unknown option: " << a << "\n";
      usage(argv[0]);
      return 2;
    } else {
      path = a;
    }
  }
  if (path.empty() || !fs::is_regular_file(path)) {
    std::cerr << "need ARCHIVE file\n";
    return 2;
  }

  const Format format = sniff_format(path);

  std::vector<std::string> names;
  bool compare = false;
  if (backend_flag == "all") {
    names = compiled_backends();
    compare = true;
  } else if (backend_flag == "auto") {
    names = {auto_backend(format)};
  } else {
    names = split_list(backend_flag);
    compare = names.size() > 1;
  }
  for (const auto& n : names) {
    const auto& known = compiled_backends();
    if (std::find(known.begin(), known.end(), n) == known.end()) {
      std::cerr << "backend '" << n << "' is not available in this build\n";
      usage(argv[0]);
      return 2;
    }
  }
  if (names.empty()) {
    usage(argv[0]);
    return 2;
  }

  std::vector<Result> results;
  for (const auto& n : names) {
    auto backend = make_backend(n, path, format);
    results.push_back(measure(*backend, format, repeats));
  }

  if (!compare) return report_single(results.front(), path, format, json_out);
  return report_compare(results, path, format, repeats, tie_pct, json_out);
}
