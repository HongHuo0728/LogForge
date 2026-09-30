# LogForge 1.3.0 English Academic Paper

The parent directory contains `LogForge Source Code Analysis and Mathematical Proof - Academic Paper_enus.pdf`. This edition replaces the earlier 1.2.1 paper and is revised alongside the Chinese edition for 1.3.0 (26929A), source commit `913e4b9417fa0f32b88629c39062b54589d9dd21`, on 2026-09-30.

The editable manuscript retains 24 chapters, 66 equations, 18 figures, 5 tables and two appendices. Updated topics include explicit input interpretation, full-sequence rational cadence, audio payload SHA-256, CUDA ranking/cache, queue planning, publication states and identity-bound crash recovery. `translation_checks.json` checks formula equality, matching figures and subsection identifiers, and numerical literals across both languages; this supplements editorial review rather than proving semantic equivalence.

`source_inventory_en.json` and `.tsv` cover 110 immutable source/project files, excluding AcademicPapers generated artifacts. `LogForge_913e4b9_source_snapshot.zip` preserves these project bytes and licenses, excluding old papers. The editable paper sources are provided separately.

`evidence/verification_1.3.0.json` is the existing 2026-09-29 release record: 29 passes, no failures or skips. `evidence/ctest_current.xml` is a reading-table export of that JSON, not a newly executed test log. Historical 1.2.1 sustained data and CTest log, and 1.2.0 benchmarks, retain their original provenance. The memory trace is explicitly historical; new sustained metrics are cited separately. No new editor-import, native-camera or physical multi-GPU experiment is claimed.

Run `python check_translation.py`, then `python build_english_paper.py` after rebuilding the Chinese counterpart. Both PDFs use ReportLab and embedded fonts with vector equations and figures. The `.tex` file is a multi-file editable project requiring its adjacent figures, not a standalone document; LaTeX compilation is not asserted. `verify_english_pdf.py` renders every current page and checks structure, counterpart hashes and copied assets. Production metrics and `qa` contain current hashes and page checks.
