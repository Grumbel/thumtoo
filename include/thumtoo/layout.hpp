// SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

namespace thumtoo {

/// Store files always live at cache_root (index.sqlite, bulk.sqlite).
/// Classic dual-path trees are moved under cache_root/legacy/ on open.
/// THUMTOO_STORE_ROOT is ignored (nested store/ opt-out removed).

/// Destination for relocated pre-cutover files (layout migrate only).
[[nodiscard]] std::filesystem::path legacy_db_root(
    const std::filesystem::path& cache_root);

/// Store root: always cache_root (top-level layout).
[[nodiscard]] std::filesystem::path redesign_store_root(
    const std::filesystem::path& cache_root);

/// Move classic dual-path files into the root layout.
/// Safe to call repeatedly; skips renames when destinations exist.
void migrate_dual_path_to_store_root(const std::filesystem::path& cache_root);

}  // namespace thumtoo
