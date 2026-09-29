# LogForge Source Code Analysis and Mathematical Proof - Academic Paper

This directory contains the complete English edition and its editable sources, verification records, and reproduction evidence. The final PDF is in the parent Videos directory, using the exact title above as its filename.

## Preservation of the Chinese edition

The original Chinese PDF and its materials are preserved without modification. `original_chinese_files_sha256.json` records the companion files before translation. `qa/verification.json` checks these hashes and the original Chinese PDF after English production. The English figure assets are byte-identical copies of the Chinese edition's vector assets; captions, table labels, prose, references, and repository role descriptions have been translated separately.

## Content and correspondence

The English manuscript retains all 24 chapters, 134 numbered subsections, 66 equations, 18 figures, 5 tables, two appendices, and source references. It contains approximately 20,900 English words before automatically expanded source inventory and references. `translation_checks.json` reports a one-to-one correspondence of 556 nonempty source blocks, exact formula equality, matching figure IDs and order, matching subsection IDs, and checks for preservation of numerical literals. These mechanical checks supplement the translation; they are not an automated proof of semantic equivalence.

The full translated abstract and the original shorter English abstract are both retained, with separate labels, to preserve the original bilingual paper's structure. The English edition has its own pagination and English typography.

## Files

- `manuscript_en.md`: complete editable English manuscript. Figure, table, results, inventory, and reference markers are expanded by the builder.
- `LogForge_Academic_Paper_English.tex`: editable LaTeX project source, used together with the adjacent `figures` directory.
- `figures`: unchanged vector equations and illustrations, plus original chart PDF/PNG exports.
- `figure_index_en.json`: figure numbers, stable IDs, and translated captions.
- `source_inventory_en.json` and `.tsv`: all 102 version-controlled files, their English role descriptions, line counts, sizes, and source-byte SHA-256 hashes.
- `LogForge_6b43a93_source_snapshot.zip`: the immutable Git source snapshot, including original licenses and third-party notices.
- `evidence`: the original study's CTest XML/log and sustained and historical performance records.
- `production_metrics.json`: edition metadata, page and object counts, and final PDF hash.
- `qa`: Poppler page renders, contact sheets, extracted text, and structural/preservation checks.

## Evidence remains the original study's evidence

The research object remains commit `6b43a93fbd4d1457ff20b4c99a030c0e398f520a`, release 1.2.1, build 26927B. The Release build and 23 of 24 CTest cases passed in the original September 29, 2026 study. `media_pipeline` failed at the old input-rejection assertion in `tests/integration.py`, line 154. The complete analysis and limitations are retained in Chapter 21. Preparing this translation did not perform or claim a fresh software test run, camera experiment, or editor certification.

## PDF and source production

The PDF uses ReportLab with embedded Times New Roman and Arial fonts. Equations and scientific figures remain vector graphics. All final pages are rendered through Poppler for layout inspection; PyMuPDF checks page boundaries, unresolved markers, untranslated Chinese text, and source preservation.

The built-in LaTeX compiler was attempted on the English source and returned `Unable to find standard directories for platform`. The delivered PDF was therefore generated with the local vector-typesetting workflow. The LaTeX project is retained for editing and is not claimed to have passed compilation. No local TeX installation was added. External compilation requires the figure directory and the packages declared in the preamble.

For reproduction, install Python packages `reportlab`, `svglib`, `pymupdf`, and `Pillow`, and retain the adjacent original Chinese materials from which the unchanged assets and evidence are copied. Windows fonts are referenced under `C:/Windows/Fonts`. Run `python check_translation.py`, then `python build_english_paper.py`. Render the PDF with `pdftoppm -r 65 -png INPUT.pdf qa/page`, then run `python verify_english_pdf.py`. The scripts write only this English materials directory and the separately named English PDF.
