# Saving and crash recovery

PDF saves write a temporary sibling of the destination. The complete file is
flushed before Windows replaces the destination with `MoveFileEx` (replace
existing, write through). A failed serialization, flush or replacement leaves
the destination intact. A process killed before replacement leaves the previous
version; after replacement it leaves the complete new version. This includes
annotation/form Save, Save As, PDF creation/conversion, merge, page extraction,
compression, encryption/decryption and page-organizer replacement/undo. The
Windows MuPDF file saver also stages command-line PDF output; stdout stays a
stream. Its vendored change is recorded in `ext/patches/0052-atomic-pdf-save.patch`.

The destination directory must allow creating and renaming files. Staging needs
space for another complete PDF. Network/server filesystems must implement atomic
rename correctly; Windows cannot guarantee a remote server's power-loss behavior.
An interrupted operation can leave a temporary `slf*.tmp` sibling. It is never
treated as a successful save. Source documents are not deleted on failure.

## Recovery

About every 60 seconds, SwiftLeaf saves dirty PDFs to `recovery` under its app-data
directory (the directory containing its settings, including portable/custom
`-appdata` directories). Each running instance owns a locked session directory.
A second instance skips that directory while its owner is alive.

Snapshots include committed annotations, text/image edits, form edits, page
operations and document properties/bookmarks. A document unchanged since its
last snapshot is not rewritten.
Open annotation gestures are deferred until their operation ends. Uncommitted
in-place text and form input is not included. Pending digital signatures are
not completed by autosave. A crash before the first successful snapshot can
lose up to one interval of edits; disk errors can extend that interval. Failures
are recorded in the app log.

On next normal launch, leftover snapshots offer **Restore**, **Discard**, or
**Later** for each document. Restore asks for a different file name and opens
that copy; it never overwrites the original document. Cancelling or a failed
restore keeps the snapshot. Discard removes it. Later keeps it for a future
launch. Test, plugin, printing and Quick Look launches do not show recovery
prompts. `-for-testing` still supports snapshots through the debug control API.

Successful normal saves, explicit discard and normal document close remove that
document's snapshot. Recovery copies retain the original PDF's encryption;
passwords are not stored. The `.path` sidecar holds the original file name.
Recovery files are working copies, not sanitized exports or historical backups.

The first edit starts a background read of the original PDF. Every interval,
the UI thread tries the engine's page/render/document locks and freezes only
changed PDF objects and the trailer. Busy engines and open gestures wait for
the next interval. Immutable stream buffers remain retained until completion.
A worker with its own MuPDF context reconstructs and serializes that frozen
state, then atomically publishes it. Later edits cannot change the snapshot.
Closing or saving cancels publication; the job retains the engine until cleanup.
No full-document serialization or disk write runs in the timer callback.

## Undo coverage

- Each document has one MuPDF journal for annotations, forms, text/images,
  page changes and document properties/bookmarks. Ctrl+Z and Ctrl+Y traverse
  that history; Edit menu and ribbon show the next operation's label.
- Rotate, delete, reorder (including thumbnail drag), insert, replace, crop,
  N-up and other page transformations, watermark, header/footer and Bates
  tools import their complete result into the live document as one step.
  Unsaved edits are included as tool input. The original PDF changes only
  when saved. Page undo and redo rebuild layout, outlines and annotation caches.
- Nested operations coalesce a gesture into one entry. Each document has a
  32 MiB inactive-object budget; older journal entries spill into the locked
  recovery session as `history-*.undo`. Disk errors preserve in-memory history.
  Spill files are deleted on document disposal or stale-session cleanup.
- Export, split, bake, compress, encryption and other tools producing another PDF
  do not form undo steps in the source document. Undo history does not survive a
  restart or recovery restore. Ordinary view rotation does not edit the PDF.

## Verification

`FileUtilTest` covers abandoned staging, locked-destination failure/retry and new
file publication. `FormAuthoringTest` covers field recovery, unchanged-snapshot
skipping, undo/redo after snapshots, failed saves preserving recovery, and repeated
saves of one live engine after a copy. Its unified-history tests interleave
field creation/value edits, page insertion and properties, traverse undo/redo,
branch after undo and force disk spill. Recovery tests freeze before a later
edit, inject a mid-write fault, reopen the previous snapshot, retry and cancel.
`tests/edit-reliability.ts` covers actual
annotation edits, process termination, another active instance, restore into a
separate PDF, save cleanup and discard. It is registered in the regular and
SwiftLeaf CI smoke suites. Run:

```text
bun cmd/run-unit-tests.ts -dbg --no-build
bun tests/edit-reliability.ts --no-build
bun tools/swiftleaf/check-branding.ts
```

The debug control command `TestRecovery` uses action 0 for the production timer
callback, 1 to count unlocked leftovers, 2 to restore the first leftover to the
supplied destination, and 3 to discard unlocked leftovers. Normal launches use
the same scanner and restoration functions through the recovery dialog.
