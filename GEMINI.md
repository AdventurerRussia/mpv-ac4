# Rules and Guidelines

## Release Notes & Changelogs
- All release notes, changelogs, GitHub release descriptions, and lists of changes MUST ALWAYS be written in English.
- While direct conversation with the user may be in Russian (or the user's language), all release documentation, `release_notes_*.md` files, and GitHub releases must strictly use English.

## Release Archive Packaging Rules
- The release zip archive attached to GitHub releases MUST follow the exact naming convention:
  `MPV-AC4-nvJPEG2000-Windows-<version>-x64.zip` (e.g., `MPV-AC4-nvJPEG2000-Windows-1.4.0-x64.zip`).
- Inside the ZIP archive, all files must be enclosed inside a single top-level directory named identically to the archive base name (e.g., `MPV-AC4-nvJPEG2000-Windows-1.4.0-x64/`).
- The player distribution must be clean: strictly no user scripts (`.lua`), temporary files, or configuration files (`.conf`).
- Placeholders for user storage MUST be included as empty directories: `portable_config/`, `scripts/`, `script-opts/`, `shaders/`.
- Temporary build or decoder directories like `DECODIR` must never be included.
