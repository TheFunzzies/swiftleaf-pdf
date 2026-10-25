# Command-line automation

The batch and compare commands run before normal window creation and print one
JSON result to standard output. Errors use a nonzero exit code.

```powershell
Swiftleaf.exe -batch actions.json C:\PDF\one.pdf C:\PDF\incoming
Swiftleaf.exe -compare before.pdf after.pdf -report comparison.html
```

`-batch` takes a JSON definition followed by one or more PDF files or folders.
Folders are scanned recursively for PDFs. The definition schema and available
actions are in [batch-compare.md](batch-compare.md). Batch results include
`ok`, `total`, and a `results` array with each input and output or error. The
optimizer action also returns `sizeBefore` and `sizeAfter` byte counts.

`-compare` takes exactly two PDFs and a `-report` output path. Its JSON result
includes the number of pages compared, word-change count, average changed-pixel
percentage, and report path. The report is HTML.
