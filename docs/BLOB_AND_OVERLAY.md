<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Blob identity, overlays, and “hypertia” (brainstorm)

Status: **discussion only** — no schema migration or API freeze yet.  
No backwards compatibility with current `index.sqlite` / `blobs.sqlite`; an old
cache may be wiped with a clear “outdated, will be rebuilt” warning.

This note consolidates design talk across thumtoo, biltoo, dirtoo, and a
possible network layer. It is a map of goals and attachment points, not an
implementation plan.

Related in-tree: [DESIGN.md](../DESIGN.md), [TAGS.md](../TAGS.md), [TILES.md](../TILES.md).  
Sister projects: [dirtoo](https://github.com/Grumbel/dirtoo), [biltoo](https://github.com/Grumbel/biltoo).

---

## 1. Problem with the current model (thumtoo today)

- **`content_id` is a full SHA-256 string** (often with `sha256:` prefix) used as
  the primary key on content, levels, tiles, tags, blob rows. That is large,
  repeated on every tile, and awkward as an internal FK.
- **Composite pseudo-ids** such as
  `sha256:b866…3fc0:page:110` mix content addressing with view addressing.
- **Archive members** are listed under the outer archive; **member bytes are not
  hashed**, so tags/tiles cannot follow “the same image inside two zips.”
- **`codec` as TEXT** on every tile/level row is wasteful; a small codec table
  would suffice.
- **Levels + tiles** both store pixel pyramids; levels may be reconstructible
  from tiles (or from one full raster), which is dual source-of-truth risk.
- **Semantic vs bytes** are collapsed into one `content` notion: size, format,
  ladder, and identity share one row shape.

None of this is fatal for a viewer cache; it becomes painful for a shared
filesystem/blob layer used by a file manager, checksum tool, annotator, or
network peer.

---

## 2. Layer split

```text
┌─────────────────────────────────────────────────────────────┐
│  Overlay graph (mutable, non-destructive)                   │
│  tags · links/trails · collections/sets · bookmarks ·       │
│  annotations · user notes · hypertia manifests                │
└────────────────────────────▲────────────────────────────────┘
                             │ refs only (blob / region / URI)
┌────────────────────────────┴────────────────────────────────┐
│  Semantic / media (derived, disposable)                     │
│  probe (w×h, page_count, duration) · tiles · previews ·     │
│  text layers · extracted links · outlines · attention maps  │
└────────────────────────────▲────────────────────────────────┘
                             │ interprets
┌────────────────────────────┴────────────────────────────────┐
│  Blob + locator (immutable bytes + where found)             │
│  blob id · digests · size · locators · container members    │
└─────────────────────────────────────────────────────────────┘
```

**Principle:** bytes never change. Structure is layered **by reference**.
Apps may rewrite overlays freely; they never need to rewrite source files or
blob payloads to add a tag, link, or collection.

| Layer | Owns | Does not own |
|-------|------|----------------|
| **Blob / locator** | Identity of bytes, paths, archive TOC, optional body cache | “Is this a photo?”, tiles, tags |
| **Media / region** | Interpretation + geometry + derived pixels | Path policy, user taxonomies |
| **Overlay** | Tags, trails, sets, bookmarks, user annotations | File mutation |

A checksum CLI or dirtoo-style tagger only needs the blob layer (+ overlay tags).
A pure image viewer needs media + tiles. A reader needs regions + structure.
Hypertia is a transport for the same refs and overlays.

---

## 3. Naming: blob vs “content”

Avoid **`content`** as a table name — it conflates bytes and meaning.

| Name | Meaning |
|------|---------|
| **`blob`** | Opaque byte identity (dumb bytes). Integer primary key. |
| **`blob_hash`** | Digests for a blob (algo + raw digest bytes). |
| **`locator`** | How to find bytes today (path, URL, archive member chain). |
| **`media`** | Semantic object: image, document, audio, video, … |
| **`region`** | Sub-range of media: page, archive member view, CFI, time range. |

Public interop strings can still say “content id” meaning “stable blob digest”;
internal FKs should be integer **`blob_id`** (and **`media_id` / `region_id`**).

---

## 4. Digests: columns vs side table

**Recommendation: separate `blob_hash` table**, not a growing list of columns on
`blob`.

```text
blob (
  id     INTEGER PRIMARY KEY,
  size   INTEGER,
  ...
)

blob_hash (
  blob_id  INTEGER NOT NULL REFERENCES blob(id),
  algo     INTEGER NOT NULL,   -- fk or enum: sha256, sha1, md5, …
  digest   BLOB NOT NULL,      -- raw bytes, never hex TEXT as PK
  UNIQUE (blob_id, algo),
  UNIQUE (algo, digest)        -- lookup by hash
)
```

- New algorithms need no `blob` migration.
- dirtoo already stores multiple algos (`sha256`, `md5`, `sha1`, `crc32`) in
  checksum cache; tags key on **full SHA-256** only.
- Optional denormalized “primary” digest on `blob` for hot paths is an
  optimization, not the extension mechanism.

**Merge rule:** when two locators hash to the same canonical digest (e.g.
SHA-256), they share one `blob.id`. Provisional rows (size known, hash unknown)
upgrade or merge when the digest lands.

---

## 5. Locators and external representation

### Structured in DB, URI on the wire

Apps and hypertia exchange **URIs**; the library resolves them to ids.

Sketch (grammar open):

```text
# Content-addressed (rename-safe)
blob:sha256:<hex>
# optional CID-style encoding later

# Location-addressed
file:///abs/path/photo.jpg
file:///abs/path/book.zip!/chapter/page001.jpg
file:///outer.zip!/inner.rar!/dir/img.png

https://example.com/img.jpg

# Region / view (not a new blob)
blob:sha256:<hex>#page=110
blob:sha256:<hex>#member=path/inside.cbz
file:///book.pdf#page=12
```

**Do not** encode regions into the digest string
(`sha256:…:page:110`). Page/member/time are **region** keys on top of a blob
or media object.

Archive nesting is a **chain of container members**, each of which may itself
be a hashed blob (nested archives supported in the data model even if extract
depth is limited at first).

### WWW as archives of links

A web page is already a weak archive: HTML + embedded image URLs + hyperlinks.
In this model:

- `https://site/page` → locator (and optional cached body blob)
- embedded `https://site/a.jpg` → separate locators/blobs when fetched
- `<a href>` → overlay or extracted **link** edges (same link table as PDF)

Crawling/import is “hydrate locators + optional blobs”; the graph does not
require the site to understand hypertia.

---

## 6. Archives and member hashing

Today (thumtoo): TOC on outer URI; members rarely have their own content rows.

Target:

```text
container blob  -- the zip/rar/… bytes
container_member (
  container_id,      -- blob.id of archive
  member_path,       -- normalized path inside
  content_id NULL,   -- blob.id of member bytes when hashed
  uncompressed_size, ...
)
```

Nested: outer zip → member `inner.rar` hashes to blob B → member of B hashes to
image blob C. Locators record the chain; tiles/tags hang off **C**.

PDF pages are usually **regions** of the document blob (or derived page media),
not separate file hashes, until page rasters are materialized as their own
blobs (optional).

---

## 7. Media, regions, tiles, levels

### Media

Probe results and kind live on **media** (or equivalent), pointing at a blob:

- image: width, height, orientation contract
- document: page_count, layout_key policy
- video/audio: duration, still_count, …

### Region

One abstraction for “place inside a work”:

| Kind | Key examples |
|------|----------------|
| full | whole media |
| page | PDF/DjVu page number |
| archive_member | path inside CBR/CBZ (when treated as document order) |
| cfi / spine | EPUB |
| time_range | video/audio |

Bookmarks, page tags, annotations, text layers, and page tiles should share
**region identity** so they do not invent parallel keys.

### Tiles vs levels

**Direction of travel:** tiles (and optional single preview / LQIP) as durable
pixel source of truth; multi-edge **levels** demoted or removed if soft frames
can be assembled or downscaled from tiles/full.

Open point: Gallery soft ≤512 may still want one cheap whole-image blob for
speed vs assembling many JPEG tiles—allow a single “preview” without a full
ladder table.

Codec: **`codec_id`** → small `codec(id, name)` table, not TEXT per tile.

---

## 8. Document structure: text, links, image maps

Extracted (and later user-authored) structure attaches to **media + region +
layout_key**, not to digests alone.

| Kind | Role |
|------|------|
| **Text layer** | Glyph/word/line quads + text (search, select, copy) |
| **Hyperlink** | Hit geometry + action (URI, goto page, named dest) |
| **Image map** | Polygons/rects on **image** media + action |
| **Outline / ToC** | Tree of title → target region |

`layout_key` namespaces extracts when render parameters change (crop box,
rotation, EPUB CSS). Extracts are **cache**; source bytes remain authoritative.
User annotations use the same geometry model with `source=user`.

Coordinate space must match display (PDF user space, upright image space for
maps/attention—same contract as tiles).

---

## 9. Overlay use cases

Overlays **reference** blob / media / region / external URI. They never rewrite
bytes.

| Use case | Attach primarily to | Notes |
|----------|---------------------|--------|
| **File tags** | blob | dirtoo parity; rename-safe |
| **Sets / collections** | list of blob (or region) refs | dirtoo FileSet / “album” |
| **Folder bookmarks** | locator | path convenience |
| **Reading bookmark** | region | page / CFI / member |
| **Page tags / highlights** | region + geometry | annotation |
| **Attention / saliency / AI caption / embedding** | media (+ aux payload) | extract or user |
| **Trails / two-way links** | edge between two refs | see §10 |
| **Hypertia manifests** | named overlay documents | §12 |

**Session** state (biltoo SessionImageId, live crop, undo) stays in the app—not
in the shared durable overlay store—unless explicitly exported.

### dirtoo coverage (what we must not regress)

| dirtoo piece | Role | Redesign mapping |
|--------------|------|------------------|
| **ChecksumStore** | Path → digests (sha256/md5/sha1/crc32); size+mtime validity; full vs quick sample | `locator` + `blob_hash`; quick samples namespaced or separate |
| **TagStore** | SHA-256 identity; `tag_defs` + `file_tags`; path aliases; never hashes itself | overlay tags on `blob_id`; keep catalog (name, color, badge) in tag defs |
| **FileSetStore** | Ad-hoc persistent sets; path-primary, optional sha256 | overlay collection/set of refs |
| **Archive** | Read-only TOC + extract member (libarchive) | `container_member`; hash members when needed |
| **MediaMetaCache** | Filter attributes (type, duration, aspect, …) | media probe fields |
| **Thumbnailer1 client** | Desktop icons | optional consumer; not the tile pyramid |
| **Filter DSL** | `tag:`, `checksummed:`, size, duration, … | needs stable blob tags + media meta |
| **Places / path bookmarks** | Sidebar locations | locators / app chrome |
| **TAGS.md interop** | dirtoo hex ↔ thumtoo `sha256:` | boundary stays digest-based; internal id integer |

dirtoo today splits **cache** (`checksums.sqlite`) and **data** (`tags.sqlite`).
A unified blob layer can still use multiple files, but **one identity**
(`blob_id` / canonical SHA-256) should be shareable so tags and tiles do not
diverge.

---

## 10. Trails (Memex) and two-way links

Vannevar Bush’s **Memex** imagined associative **trails**: durable paths through
records, shared and annotated, without altering the records themselves. A
**two-sided hyperlink** is the elementary trail edge: A associates with B, and
from B one can find A.

In this design:

```text
link_edge (
  id,
  from_ref,     -- blob | region | external URI
  to_ref,
  rel,          -- seeAlso | cite | trail | tag-ish | …
  source,       -- user | extract | hypertia
  created_at,
  ...
)
-- indexes on from_ref and to_ref  →  backlinks are queries, not file writes
```

- Extracted PDF/`<a href>` links seed edges with `source=extract`.
- User/hypertia trails are the same table.
- **Bidirectional UX** = show outgoing + incoming; storage is directed edges +
  reverse index (avoid mandatory duplicate rows unless performance demands it).

**Trails** as first-class objects can be ordered sequences of edges or nodes
(a named path through refs)—a collection with order and optional commentary—
still pure overlay.

---

## 11. IPFS / IPLD (ideas to borrow, not a dependency)

| Concept | Relevance |
|---------|-----------|
| **Content addressing** | Same as `blob` + digests |
| **CID** | Self-describing wire id (codec + hash); optional export form |
| **IPLD graphs** | Small linked objects (manifests, link edges) whose leaves are blob ids |
| **UnixFS** | Directory-like DAGs; rough analogue of containers/collections |
| **IPNS / mutable names** | Named pointer to a changing collection root; blobs stay immutable |
| **Blocks vs files** | Keep overlay records small; media bytes large and separate |

No requirement to run IPFS. The lesson is: **immutable blob identity + mutable
name/overlay graph**. Local SQLite can implement that; CAR/IPLD export can wait.

---

## 12. Hypertia (network / “web alternative”)

Working name for an HTTP-like access layer over the same model—not a second
database.

### Location bar (examples)

```text
blob:sha256:b8665477a40753e5d056c8650128bdc444a78fbac11aa44701f9fccfe9343fc0
blob:sha256:b866…3fc0#page=110
file:///home/user/Pictures/vacation.zip!/DSC_0001.jpg
https://example.com/gallery/1.jpg
hypertia://library/collections/summer-2024
hypertia://library/blob/sha256/b866…3fc0
tag:vacation
trail:memex-demo-01
```

Resolution order is product policy (local blob → locator fetch → remote).

### UI sketches

**Tags (dirtoo-like)**  
Context menu / Ctrl+T on a file or archive member → tags apply to **blob** once
hashed; badge follows the file across renames and across “same bytes in another
folder.”

**Trail / two-way link**  
In biltoo or a reader: “Link to…” picks another open image/page or pastes a
`blob:` / `file:` / `https:` ref. A sidebar **Backlinks** lists everything that
points here. Creating a trail segment records an edge; it does not edit the
JPEG or PDF.

**Collections / sets**  
“Add to set” stores membership by blob id (path optional). Opening a set is a
virtual folder of refs (dirtoo FileSet direction).

**Document page**  
Location `…#page=12` shows page 12; extracted links are hit-targets; user
bookmark is a region overlay; text layer enables selection.

**Image map / attention**  
Visible hotspots or attention points in content space; stored as media aux /
annotations, not in EXIF.

### `curl`-shaped access (illustrative)

```bash
# Raw bytes when permitted
curl -OJ hypertia://local/blob/sha256/b866…3fc0

# Probe / media meta (JSON)
curl hypertia://local/media/by-blob/sha256/b866…3fc0

# Tile or preview
curl hypertia://local/media/…/tile?scale=3&x=0&y=0 -o tile.jpg

# Overlays
curl hypertia://local/blob/sha256/b866…3fc0/tags
curl hypertia://local/blob/sha256/b866…3fc0/links
curl hypertia://local/blob/sha256/b866…3fc0/backlinks

# Collection manifest
curl hypertia://local/collections/summer-2024

# WWW remains normal HTTP; hypertia may *cite* it
curl -I https://example.com/photo.jpg
# after ingest: same bytes available as blob:sha256:…
```

Write methods (when authorized) touch **overlays only** (`PUT` tag, link,
collection membership)—not the blob body.

### Crosstalk with the WWW

- Every `https:` locator can become a blob after fetch (body cache + hash).
- HTML is a source of **extracted link edges** and embedded resource locators
  (same as PDF link extract).
- Hypertia pages can **embed or cite** `https:` URLs without claiming to host
  them; two-way links to the public web are one-sided unless the remote side
  cooperates (backlinks only for nodes in the local/overlay graph).

---

## 13. Schema sketch (illustrative, non-normative)

```text
blob ( id INTEGER PK, size, kind_hint, status, updated_at )
blob_hash ( blob_id, algo, digest BLOB, UNIQUE(blob_id,algo), UNIQUE(algo,digest) )

locator ( id, uri TEXT UNIQUE, blob_id NULL, parent_locator_id NULL,
          size, mtime_ns, updated_at )

container_member ( container_id, path, blob_id NULL, uncompressed_size,
                   PRIMARY KEY (container_id, path) )

media ( id INTEGER PK, blob_id, kind, width, height, duration_ms,
        page_count, status, ... )

region ( id INTEGER PK, media_id, kind, key, ordinal )

codec ( id INTEGER PK, name TEXT UNIQUE )

tile ( media_id or blob_id, scale, x, y, w, h, codec_id, quality,
       PRIMARY KEY (...) )
-- payloads in a blob store keyed by integer ids

-- overlays
tag_def ( id, name, label, color, badge, ... )
blob_tag ( blob_id, tag_id, tagged_at, source )

link_edge ( id, from_ref, to_ref, rel, source, created_at )
collection ( id, label, ... )
collection_member ( collection_id, ref, ordinal )

annotation ( id, region_id, kind, geom, body, source, ... )

doc_structure ( media_id, region_id, layout_key, kind, payload, source, ... )
-- text_layer | link | image_map | outline | …

media_aux ( media_id, kind, payload, model_id, ... )
-- attention, embedding, caption, …
```

Integer ids internally; digests and URIs at the edges.

---

## 14. Compatibility and migration

- **No** attempt to migrate old thumtoo ladder rows automatically.
- On schema epoch bump: detect old DB → warn → delete/replace cache root (or
  open a new epoch directory).
- dirtoo tag DBs remain valid as long as **SHA-256 identity** is preserved at
  the boundary; a future bridge maps hex ↔ `blob_id`.

---

## 15. Priority order (discussion)

1. Blob + `blob_hash` + locator + container_member (identity + archives)  
2. Media + region + tiles (+ codec table); levels demoted  
3. Blob tags (dirtoo parity) and sets/collections  
4. Doc structure (text, links, maps) + bookmarks/annotations  
5. Link edges / trails + backlinks index  
6. media_aux (attention, AI)  
7. Hypertia transport / IPLD export (shape only until needed)

---

## 16. Open questions

1. Canonical merge digest (SHA-256 only vs multi-algo equality rules).  
2. Region key stability for EPUB reflow and archive renames inside CBR.  
3. Tiles-only vs hybrid single soft preview blob for Gallery performance.  
4. One SQLite vs split (checksum cache vs tags data) while sharing blob ids.  
5. Whether collections are exclusive membership (dirtoo FileSet) or multi-set.  
6. Hypertia auth and multi-user overlay namespaces.  
7. How much extracted WWW structure to store vs on-demand fetch.

---

## 17. Summary

- Replace string `content_id` PKs with **integer blob ids**; digests in
  **`blob_hash`**; name dumb bytes **`blob`**, meaning **`media`/`region`**.
- Hash **archive members**; support nested containers in the model.
- Prefer **tiles** (+ optional preview) over a parallel full ladder.
- Put tags, sets, trails, bookmarks, annotations in an **overlay graph** that
  only references ids—Memex-style association without mutating records.
- Align with **dirtoo** checksum + tag + set + archive behaviour at the
  identity boundary.
- **Hypertia** is HTTP-shaped access to blobs, media, and overlays; the WWW is
  another locator space and link source, not a separate data model.
- **IPFS/IPLD** contribute vocabulary (CID, immutable blocks, mutable names),
  not a required runtime.

This document is the standing brainstorm; normative schema belongs in
DESIGN.md (or a successor) only after the open questions above are decided.
