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
file:///abs/path/book.zip//archive:chapter/page001.jpg
file:///outer.zip//archive:inner.rar//archive:dir/img.png

# Prefer //archive: pipes (DESIGN.md); do not use JAR-style path!/member.

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
file:///home/user/Pictures/vacation.zip//archive:DSC_0001.jpg
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

## 14. Internet Archive (archive.org)

Biltoo as a **browser for open Internet Archive holdings** (especially texts)
is a natural consumer of the locator/blob/region model. IA exposes public HTTP
APIs; no special partnership is required for read/search of openly available
items. Developer portal: <https://archive.org/developers/>.

### APIs that matter

| Capability | Pattern | Role |
|------------|---------|------|
| **Item metadata + file list** | `GET https://archive.org/metadata/{identifier}` | Title, creator, mediatype, `files[]` (name, format, size, md5, sha1, …) |
| **Search** | `advancedsearch.php?q=…&output=json` | Lucene-like queries (`mediatype:texts`, `creator:…`, `collection:…`) |
| **Deep search** | `https://archive.org/services/search/v1/scrape` | Cursor pagination beyond the ~10k sorted-page limit |
| **File download** | `https://archive.org/download/{id}/{filename}` | Prefer this over hand-built data-node URLs (items move) |
| **IIIF** | `https://iiif.archive.org/iiif/{id}/manifest.json` | Page/image manifests for books and images; stream pages without a full PDF |
| **Full-text search** | FTS API (also via official `ia` Python tooling) | Search inside OCR’d texts; map hits to pages when possible |
| **Details / stream UI** | `https://archive.org/details/{id}` (+ page fragments) | Human deep links; same identifier space |

Upload/S3-like (IAS3) and metadata write are out of scope for a reader.

### Locator mapping

```text
ia:{identifier}                 -- item (bag of files; TOC-like)
ia:{identifier}/{filename}      -- one file → locator → blob when fetched
ia:{identifier}#page=12         -- region (or IIIF canvas)
https://archive.org/details/…   -- HTTP form of the item
https://archive.org/download/…/… -- direct file locator
```

- Item ≈ **container** (many files, analogous to archive TOC).
- Each file ≈ **member**; metadata digests seed **`blob_hash`** after cache.
- Book pages ≈ **regions** (or IIIF canvases); OCR/text can become
  `doc_structure`.
- Search results ≈ transient listing until the user opens a file.

Location-bar examples:

```text
ia:aliceinwonderlan00carriala
ia:aliceinwonderlan00carriala#page=23
https://archive.org/details/aliceinwonderlan00carriala
https://archive.org/download/aliceinwonderlan00carriala/aliceinwonderlan00carriala.pdf
```

### Product phases (biltoo)

1. **HTTPS open** — Paste `details/` or `download/` URLs; thumtoo http(s) fetch
   + body cache; existing PDF/DjVu/EPUB/image paths.
2. **Search chrome** — Advanced Search / scrape → virtual folder of items;
   thumb from `__ia_thumb.jpg` or IIIF; open item → file list from metadata.
3. **Book-native** — Prefer **IIIF** (or JP2 sequences) for large scans: page
   regions and tiles without downloading multi‑GB PDFs; optional FTS → page jump.
4. **Identity + overlays** — After fetch, promote to blob + digests; tags,
   bookmarks, trails on the blob with `ia:{id}` kept as a locator alias.

### Limits

- **Controlled Digital Lending** — Many modern books are not free full-file
  downloads; access is logged-in, time-limited, often image-only through IA’s
  viewer. Start with **public / openly downloadable** items
  (`mediatype:texts`, images, etc.). Lending is a later, auth-heavy experiment
  if at all.
- **Rate limits / bot policy** — Interactive use with caching is appropriate;
  bulk scrape is not. Respect IA guidance for automated clients.
- **Size** — Page streaming (IIIF) is the difference between a viewer and a
  download manager.
- **Auth** — Open metadata, search, and most downloads: anonymous. Lending and
  writes: cookies or S3 keys.

### Fit to the rest of this doc

IA is another **locator space** (like `file:` and `https:`), not a second data
model. Items and files become containers/members; pages become regions; user
structure stays in the **overlay** graph. Same spine as local archives and the
WWW.

---

## 15. Compatibility and migration

- **No** attempt to migrate old thumtoo ladder rows automatically.
- On schema epoch bump: detect old DB → warn → delete/replace cache root (or
  open a new epoch directory).
- dirtoo tag DBs remain valid as long as **SHA-256 identity** is preserved at
  the boundary; a future bridge maps hex ↔ `blob_id`.

---

## 16. Decisions (2026-09-16)

Settled for the redesign spine. Earlier “open questions” that match these are
closed.

### Pixels: tiles only

- **No durable multi-edge levels** in the new schema.
- Tile grid is the pixel source of truth (256² and below already match a “level”;
  larger soft/full views are **reconstructed from tiles**).
- biltoo may keep a compatibility path that assembles ladder-like edges from
  tiles until **0.1.0**; after that, prefer full tile rendering in the host.

### When to hash archive members

Hash member bytes when:

1. the member is **read in full** for another purpose (decode, extract, copy), or  
2. the user **explicitly** requests a checksum, or  
3. hashing is **required** for tagging, bookmarks, trails, or other overlay ops
   keyed by blob identity.

Not on mere TOC list. Nested archives follow the same rule per member.

### EPUB region keys

- Prefer keys derivable from the **text / structure overlay** when available
  (stable anchors from the extract).
- Allow **multiple addressing means** for the same bookmark target (e.g. spine
  id, CFI-like path, ordinal) — store as alternate keys or multi-key region
  rows rather than a single fragile scheme.
- Exact EPUB key grammar still needs a short design pass; principle is fixed.

### Database layout (three roles)

| Store | Contents | Notes |
|-------|----------|--------|
| **Index / small meta** | blob, blob_hash, locator, container_member, media, region, directory snapshots, link_edge text refs, schema_meta | SQLite, hot, small rows |
| **Bulk cache** | tile payloads, HTTP body cache, other large BLOBs | Separate SQLite (or equivalent); disposable |
| **User data** | tags, tag defs, collections/sets, bookmarks, user annotations, user link trails | Separate DB under XDG data (not pure cache); survives cache wipe |

Exact filenames under `$XDG_CACHE_HOME` / `$XDG_DATA_HOME` TBD; the split is
normative. Cache wipe must **not** destroy user data.

### Library ownership

- **thumtoo** is the shared **library** for the lower layers (blob, locator,
  archive members, media probe, tiles, directory cache, optional HTTP fetch).
- dirtoo / biltoo link the library; avoid long-term dual checksum/tag
  implementations. Bridge/migration from existing dirtoo DBs is allowed; the
  target API is thumtoo (or a thin façade over the same stores).

### Deferred to 0.2.0 (keep on TODO, do not block spine)

- Internet Archive **search** UI and **IIIF-first** book reading  
- **`ia:`** URI scheme vs only `https://archive.org/…`  
- WWW **HTML as container of links** (extract graph from pages)  
- Full **Hypertia** multi-machine product (protocol sketch below is enough for now)

### Link edges

- **Text canonical refs** for `from_ref` / `to_ref` (no `ref_node` / typed FK
  edges for now). Requires strict `canonicalize(ref)`.

### Provisional blobs

- **Inside the DB:** always link by integer **PK** (`blob_id`, etc.).
- **Exported / wire URLs:** content-addressed form (`blob:sha256:…`). If a hash
  is not yet available, **compute it on demand** when exporting or when an
  overlay needs a stable public ref—do not ship provisional path-only identities
  as durable external links.

### Directory listings

- **First open** of a directory should read a **cached snapshot from the DB**
  (no blocking full filesystem walk on cold USB/NFS).
- `inotify` (and similar) **update the cache** and notify listeners later; they
  are not required for the initial paint.
- Live FS remains authoritative for mutations; cache is the fast path for list
  UI (dirtoo + thumtoo alignment).

### `media` table

- Single **`media`** row with **`kind`** (`image` | `document` | `video` |
  `audio` | …), not separate top-level image/video/audio tables (see discussion
  notes above).

---

## 17. Hypertia protocol sketch (DB-agnostic)

Goal: one thumtoo instance can offer blobs, media views, and overlays to another
machine **without exposing SQLite internals**. Refs on the wire are the same
canonical text forms as local (`blob:sha256:…`, pipes, `https:…`).

### Roles

- **Server:** local library + optional user-data store; speaks Hypertia over HTTP
  (or HTTP/2).  
- **Client:** another thumtoo / biltoo / dirtoo; resolves refs, may cache tiles
  locally as its own bulk DB.

### URL shape (illustrative)

```text
GET /v1/blob/{algo}/{hex}           → raw bytes (if permitted)
HEAD /v1/blob/{algo}/{hex}          → size, digest headers only
GET /v1/blob/{algo}/{hex}/meta      → JSON: size, kind hints, media probe
GET /v1/blob/{algo}/{hex}/tiles?scale=&x=&y=  → tile bytes + codec
GET /v1/blob/{algo}/{hex}/page/{n}  → page meta / raster policy
GET /v1/blob/{algo}/{hex}/links     → outgoing link_edge as JSON
GET /v1/blob/{algo}/{hex}/backlinks
GET /v1/tags?blob=sha256:…          → tag list (if user-data shared)
GET /v1/resolve?uri=file:///…       → { blob, media } if known (local server)
```

- Path and query use **public refs**, never internal integer ids.  
- Integer PKs stay server-local.  
- Authz (read-only share vs user-data write) is orthogonal; default sketch is
  read of cacheable blob/media, optional read of overlays.

### Client behaviour

1. Receive or paste `blob:sha256:…` or `https://host/v1/blob/sha256/…`.  
2. Fetch meta → optional tiles/pages.  
3. Store in **local** bulk/index under the same digest (merge by hash).  
4. Overlays either stay on the server or are copied as link_edge text rows.

This is enough to implement a prototype later; not part of 0.1.0 spine.

---

## 18. Implementation priority (post-decision)

**0.1.0 spine (thumtoo library)**

1. blob + blob_hash + locator (PK-internal; wire `blob:sha256:`)  
2. container_member + //archive: + hash-on-read/tag policy  
3. media + kind + //page: regions  
4. tiles + codec (+ reconstruct soft views); no new levels table  
5. directory **snapshots in DB**; first list from cache  
6. split DBs: index / bulk / user-data  
7. tag_def + blob_tag; collections/sets  
8. link_edge (canonical text refs)  
9. extracted text layer / links cache on page region  
10. minimal HTTPS fetch → body in bulk store → blob  

**After 0.1.0**

- biltoo full tile rendering (drop ladder compatibility path)  
- EPUB multi-key regions detail  
- Hypertia prototype  

**0.2.0+ TODO**

- IA search + IIIF-first books; `ia:` scheme  
- WWW HTML link extraction  
- Richer hypertia auth / multi-user  

---

## 19. Still open (narrow)

1. Canonical **merge** when several algos present (SHA-256 wins for identity?).  
2. EPUB **concrete** key fields (from text overlay vs spine list).  
3. Collection membership **exclusive** (dirtoo FileSet) vs multi-set.  
4. Exact XDG paths and filenames for the three DB roles.  
5. Directory snapshot **invalidation** policy when inotify is missing.  
6. Whether user-data DB is process-shared read/write from dirtoo and biltoo
   simultaneously (locking).

---

## 20. Summary

- Integer **blob** PKs; **blob_hash** side table; **media** + **kind**; regions
  via **`//page:`** / **`//archive:`** pipes.  
- **Tiles only**; reconstruct larger views; biltoo tile-native after 0.1.0.  
- Hash archive members on full read, explicit request, or overlay need.  
- **Three stores:** small index, bulk cache, user-editable data.  
- **thumtoo library** owns the lower layer for dirtoo and biltoo.  
- **link_edge** = canonical text refs; provisional blobs use PK inside, hash on
  export.  
- **Directory first paint from DB cache**; watchers refresh later.  
- IA / HTML graph / full Hypertia product deferred; protocol sketch is
  ref-based HTTP, not SQL over the wire.

Normative schema should land in DESIGN.md (or a successor) from §16–18 once
the narrow open list (§19) is closed enough to code.
