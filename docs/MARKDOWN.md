<!--
SPDX-FileCopyrightText: 2026 Ingo Ruhnke <grumbel@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Markdown, plain text, and //text force (MuPDF)

Requires MuPDF ≥ 1.28 (`pinMupdf`).

## Auto extensions

- **Markdown:** `.md`, `.markdown`, `.mdown`, `.mkd`
- **Plain text:** `.txt`, `.text`, plus common sources/data (`.c`, `.h`, `.cpp`,
  `.py`, `.rs`, `.json`, `.yaml`, … — see `is_plain_text_extension`)

Non-native text extensions open via MuPDF filetype magic `"txt"`.

## Force pipe: `//text`

Force plain-text interpretation regardless of extension:

| URI | Meaning |
|-----|---------|
| `path/to/foo//text` | Expand whole file as text document |
| `path/to/foo//text//page:3` | Page 3 as text |
| `file:///abs/foo.xyz//text//page:1` | Same with file URI |

## Related

- biltoo `docs/TXT_MD_SUPPORT.md`
