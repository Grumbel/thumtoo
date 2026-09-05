<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Tags — thumtoo ↔ dirtoo

## Identity

Both systems key tags by **file content hash**, not path:

| System | Content key | Store location |
|--------|-------------|----------------|
| **dirtoo** `TagStore` | bare SHA-256 hex (64 chars) | `$XDG_DATA_HOME/dirtoo/tags.sqlite` |
| **thumtoo** | `content_id` = `sha256:` + hex | `$XDG_CACHE_HOME/thumtoo/index.sqlite` (`tags` table) |

Conversion:

```text
dirtoo_sha256  = content_id.substr(strlen("sha256:"))   // when prefix matches
thumtoo_id     = "sha256:" + dirtoo_sha256
```

Provisional ids (`prov:…`) must not receive durable tags; promote to SHA-256 first
(prepare / probe does this).

## Schema comparison

**dirtoo** (rich Tag Manager):

```text
tag_defs(id, name, label, color, badge, created)
files(id, sha256 UNIQUE)
paths(path, file_id, last_seen)
file_tags(file_id, tag_id, tagged_at)
```

**thumtoo** (minimal, ladder-cache sibling):

```text
tags(content_id, tag, source, created_at)
  PRIMARY KEY (content_id, tag)
```

No tag catalog, colors, or rename indirection in thumtoo. UI for those stays in
dirtoo. thumtoo only needs “this content carries label X” for consumers such as
biltoo filters.

## API sketch (implemented)

```text
Client::get_tags(uri) -> [tag…]
Client::add_tag(uri, tag, source="user") -> bool
Client::remove_tag(uri, tag) -> bool

Database::tags_for_content(content_id)
Database::content_ids_for_tag(tag)
Database::add_tag / remove_tag
```

`source` is free text (`user`, `auto`, app id); not validated yet.

## Interop (later)

- Import/export between stores via SHA-256 mapping (no shared SQLite file).
- dirtoo remains authoritative for Tag Manager metadata (color/badge).
- Optional: thumtoo-status `tags` subcommand; dirtoo `dt_tag` already exists.

## Non-goals

- Value-bearing / namespaced tags (`namespace:key=value`) — deferred in both
  designs until needed.
- Tagging by path alone when a content hash is known.
