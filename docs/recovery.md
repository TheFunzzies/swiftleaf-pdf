# Saving and crash recovery

PDF saves write a temporary sibling of the destination. The complete file is
flushed before Windows replaces the destination with `MoveFileEx` (replace
existing, write through). A failed serialization, flush or replacement leaves
the destination intact. A process killed before replacement leaves the previous
version; after replacement it leaves the complete new version. This includes
annotation/form Save, Save As, PDF creation/conversion, merge, page extraction,
compression, encryption/decryption and page-organizer replacement/undo.

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

Snapshots include committed annotations, text edits and form edits, including
field authoring. A document unchanged since its last snapshot is not rewritten.
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

Serialization runs on the UI thread under the engine's page/render/document
locks: form calculations and appearance updates use its UI-owned base context.
Moving these operations to a worker would risk concurrent form JavaScript and
annotation lifetime bugs. A large dirty PDF can therefore pause the UI during
its snapshot. Idle/clean documents incur no PDF serialization.

## Undo coverage

- Annotation and form edits use the existing MuPDF undo/redo journal.
- Rotate, delete, reorder (including thumbnail drag), insert blank page, and
  insert pages from another file already use **Undo Page Change**, backed by
  separate snapshots. These operations save immediately to disk, so no unsaved
  page-operation state needs periodic recovery.
- The two histories are not unified. Page operations reload the engine and reset
  annotation/form undo. Undo Page Change restores the whole earlier PDF and can
  replace annotations made after that page operation. Page redo is unavailable.
- Export, split, bake, compress, encryption and other tools producing another PDF
  do not form undo steps in the source document. Undo history does not survive a
  restart or recovery restore. Ordinary view rotation does not edit the PDF.

Unifying these histories requires changing page operations to edit the live
engine and rebuilding page caches, annotation identities and controller layout
across journal traversal. This change deliberately keeps that larger refactor
out of the save/recovery path.

## Verification

`FileUtilTest` covers abandoned staging, locked-destination failure/retry and new
file publication. `FormAuthoringTest` covers field recovery, unchanged-snapshot
skipping, undo/redo after snapshots, failed saves preserving recovery, and repeated
saves of one live engine after a copy. `tests/edit-reliability.ts` covers actual
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
