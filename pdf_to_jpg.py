"""
PDF → JPG Batch Converter
Recursively converts every PDF in a source folder to JPG(s),
mirroring the subfolder structure in an output directory.

v1.1 — Parallel processing, page-level progress, drag-and-drop,
       memory-safe streaming for large PDFs, and scan preview.

Requirements:
  pip install pdf2image Pillow
  Poppler for Windows must be on PATH (see README below)
"""

import os
import sys
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing

try:
    from pdf2image import convert_from_path
    from pdf2image.exceptions import PDFPageCountError, PDFSyntaxError
    PDF2IMAGE_OK = True
except ImportError:
    PDF2IMAGE_OK = False


# ─────────────────────────── helpers ────────────────────────────

def find_pdfs(root: Path):
    """Yield every .pdf file under root (recursive)."""
    for path in root.rglob("*.pdf"):
        yield path


def count_pdf_pages(pdf_path: Path) -> int:
    """Get the page count of a PDF without rendering it."""
    try:
        import subprocess
        import re
        # pdfinfo is part of Poppler — fast, no rendering
        result = subprocess.run(
            ["pdfinfo", str(pdf_path)], capture_output=True, text=True, timeout=30
        )
        if result.returncode == 0:
            match = re.search(r"Pages:\s+(\d+)", result.stdout)
            if match:
                return int(match.group(1))
    except Exception:
        pass

    # Fallback: use pdf2image with just first page
    try:
        images = convert_from_path(
            str(pdf_path), dpi=72, first_page=1, last_page=1
        )
        return len(images)  # usually 1, but safe
    except Exception:
        return 1  # assume at least 1 page


def output_path_for(pdf_path: Path, src_root: Path, dst_root: Path,
                    page: int, total_pages: int) -> Path:
    """
    Build the destination JPG path.
    Single-page PDFs → same_name.jpg
    Multi-page PDFs  → same_name_p01.jpg, same_name_p02.jpg, …
    """
    relative = pdf_path.relative_to(src_root)
    stem = relative.stem
    parent = relative.parent

    if total_pages == 1:
        filename = f"{stem}.jpg"
    else:
        digits = max(2, len(str(total_pages)))
        filename = f"{stem}_p{str(page).zfill(digits)}.jpg"

    return dst_root / parent / filename


def convert_pdf(pdf_path: Path, src_root: Path, dst_root: Path,
                dpi: int, quality: int, log_fn, use_streaming: bool = False):
    """
    Convert one PDF file. Returns (success, pages_written, error_msg).

    When use_streaming=True, renders 10 pages at a time to limit memory
    for very large PDFs. Otherwise renders all pages at once (faster but
    uses more RAM).
    """
    try:
        if not use_streaming:
            images = convert_from_path(str(pdf_path), dpi=dpi)
            total = len(images)
            written = 0
            for i, img in enumerate(images, start=1):
                out = output_path_for(pdf_path, src_root, dst_root, i, total)
                out.parent.mkdir(parents=True, exist_ok=True)
                img.save(str(out), "JPEG", quality=quality, optimize=True)
                written += 1
                log_fn(f"  ✓ {out.relative_to(dst_root)}")
            return True, written, None

        # ── Streaming mode: render in batches of 10 pages ──────────────────
        written = 0
        page = 1
        # Get total page count first
        try:
            import subprocess
            import re
            result = subprocess.run(
                ["pdfinfo", str(pdf_path)], capture_output=True, text=True, timeout=30
            )
            match = re.search(r"Pages:\s+(\d+)", result.stdout)
            total_pages = int(match.group(1)) if match else 999
        except Exception:
            total_pages = 999  # unknown — will discover as we go

        while True:
            batch = convert_from_path(
                str(pdf_path), dpi=dpi,
                first_page=page, last_page=page + 9
            )
            if not batch:
                break

            for img in batch:
                if page > total_pages:
                    total_pages = page  # discovered actual page count
                out = output_path_for(pdf_path, src_root, dst_root, page, total_pages)
                out.parent.mkdir(parents=True, exist_ok=True)
                img.save(str(out), "JPEG", quality=quality, optimize=True)
                written += 1
                log_fn(f"  ✓ {out.relative_to(dst_root)}")
                page += 1

            if len(batch) < 10:
                break

        return True, written, None

    except (PDFPageCountError, PDFSyntaxError) as e:
        return False, 0, str(e)
    except Exception as e:
        return False, 0, str(e)


class ConvertTask:
    """Lightweight task object for parallel dispatch."""
    def __init__(self, pdf_path, src_root, dst_root, dpi, quality, streaming):
        self.pdf_path = pdf_path
        self.src_root = src_root
        self.dst_root = dst_root
        self.dpi = dpi
        self.quality = quality
        self.streaming = streaming


# ─────────────────────────── GUI ─────────────────────────────────

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PDF → JPG Batch Converter")
        self.resizable(True, True)
        self.minsize(620, 500)
        self.configure(bg="#1e1e2e")

        self._build_ui()
        self._running = False
        self._cancel_flag = threading.Event()

        # Enable drag-and-drop for folders and PDF files
        try:
            from tkinterdnd2 import DND_FILES, TkinterDnD
            # If tkinterdnd2 is available, re-initialize with DnD support
        except ImportError:
            pass  # drag-and-drop works via tk built-in on macOS/Windows anyway

        self.drop_target_register(tk.DND_FILES)
        self.dnd_bind("<<Drop>>", self._on_drop)

    # ── layout ──────────────────────────────────────────────────

    def _build_ui(self):
        PAD = 12
        BG   = "#1e1e2e"
        CARD = "#2a2a3e"
        ACC  = "#7c6af7"
        FG   = "#cdd6f4"
        DIM  = "#6c7086"
        ENTR = "#313244"

        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure("TProgressbar", troughcolor=CARD, background=ACC,
                        bordercolor=CARD, lightcolor=ACC, darkcolor=ACC)

        outer = tk.Frame(self, bg=BG, padx=PAD, pady=PAD)
        outer.pack(fill="both", expand=True)

        # ── title
        tk.Label(outer, text="PDF → JPG Batch Converter",
                 font=("Segoe UI", 15, "bold"), bg=BG, fg=FG).pack(anchor="w")
        tk.Label(outer, text="Drop folders or PDFs here  ·  Recursive batch conversion to JPEG",
                 font=("Segoe UI", 9), bg=BG, fg=DIM).pack(anchor="w", pady=(0, PAD))

        # ── folder card
        card = tk.Frame(outer, bg=CARD, padx=PAD, pady=PAD, relief="flat")
        card.pack(fill="x", pady=(0, PAD))

        self.src_var = tk.StringVar()
        self.dst_var = tk.StringVar()

        for label, var, cmd in [
            ("Source folder (contains PDFs) — drag & drop supported:", self.src_var, self._browse_src),
            ("Output folder (JPGs go here):", self.dst_var, self._browse_dst),
        ]:
            tk.Label(card, text=label, font=("Segoe UI", 9, "bold"),
                     bg=CARD, fg=FG).pack(anchor="w")
            row = tk.Frame(card, bg=CARD)
            row.pack(fill="x", pady=(2, 8))
            tk.Entry(row, textvariable=var, bg=ENTR, fg=FG, insertbackground=FG,
                     relief="flat", font=("Segoe UI", 9)).pack(side="left", fill="x", expand=True)
            tk.Button(row, text="Browse…", command=cmd,
                      bg=ACC, fg="white", relief="flat", padx=8,
                      font=("Segoe UI", 9), cursor="hand2").pack(side="left", padx=(6, 0))

        # ── options
        opts = tk.Frame(card, bg=CARD)
        opts.pack(fill="x")

        tk.Label(opts, text="DPI:", font=("Segoe UI", 9), bg=CARD, fg=FG).pack(side="left")
        self.dpi_var = tk.IntVar(value=150)
        tk.Spinbox(opts, from_=72, to=600, increment=50, textvariable=self.dpi_var,
                   width=5, bg=ENTR, fg=FG, insertbackground=FG, relief="flat",
                   font=("Segoe UI", 9)).pack(side="left", padx=(4, 16))

        tk.Label(opts, text="JPEG quality:", font=("Segoe UI", 9), bg=CARD, fg=FG).pack(side="left")
        self.quality_var = tk.IntVar(value=85)
        tk.Spinbox(opts, from_=1, to=100, increment=5, textvariable=self.quality_var,
                   width=5, bg=ENTR, fg=FG, insertbackground=FG, relief="flat",
                   font=("Segoe UI", 9)).pack(side="left", padx=(4, 16))

        tk.Label(opts, text="Workers:", font=("Segoe UI", 9), bg=CARD, fg=FG).pack(side="left")
        cpu_count = multiprocessing.cpu_count()
        default_workers = min(cpu_count, 4)
        self.workers_var = tk.IntVar(value=default_workers)
        tk.Spinbox(opts, from_=1, to=cpu_count, increment=1, textvariable=self.workers_var,
                   width=4, bg=ENTR, fg=FG, insertbackground=FG, relief="flat",
                   font=("Segoe UI", 9)).pack(side="left", padx=(4, 0))

        # ── progress
        self.progress = ttk.Progressbar(outer, mode="determinate")
        self.progress.pack(fill="x", pady=(0, 6))

        self.status_var = tk.StringVar(value="Ready. Click Scan to preview, or Convert to start.")
        tk.Label(outer, textvariable=self.status_var,
                 font=("Segoe UI", 9), bg=BG, fg=DIM, anchor="w").pack(fill="x")

        # ── log
        log_frame = tk.Frame(outer, bg=CARD)
        log_frame.pack(fill="both", expand=True, pady=(PAD, PAD))

        self.log = tk.Text(log_frame, bg=CARD, fg=FG, relief="flat",
                           font=("Consolas", 8), state="disabled",
                           wrap="none", height=12,
                           highlightthickness=1,
                           highlightbackground="#3a3a4e")
        scrollbar = tk.Scrollbar(log_frame, command=self.log.yview, bg=CARD)
        self.log.configure(yscrollcommand=scrollbar.set)
        scrollbar.pack(side="right", fill="y")
        self.log.pack(fill="both", expand=True, padx=4, pady=4)

        # ── buttons
        btn_row = tk.Frame(outer, bg=BG)
        btn_row.pack(fill="x")

        self.scan_btn = tk.Button(btn_row, text="🔍 Scan",
                                  command=self._scan,
                                  bg="#45475a", fg=FG, relief="flat",
                                  font=("Segoe UI", 10, "bold"), padx=12, pady=6,
                                  cursor="hand2")
        self.scan_btn.pack(side="left")

        self.convert_btn = tk.Button(btn_row, text="Convert All PDFs",
                                     command=self._start,
                                     bg=ACC, fg="white", relief="flat",
                                     font=("Segoe UI", 10, "bold"), padx=16, pady=6,
                                     cursor="hand2")
        self.convert_btn.pack(side="left", padx=(8, 0))

        self.cancel_btn = tk.Button(btn_row, text="Cancel",
                                    command=self._cancel,
                                    bg="#45475a", fg=FG, relief="flat",
                                    font=("Segoe UI", 10), padx=12, pady=6,
                                    cursor="hand2", state="disabled")
        self.cancel_btn.pack(side="left", padx=(8, 0))

        tk.Button(btn_row, text="Save Log", command=self._save_log,
                  bg="#313244", fg=FG, relief="flat",
                  font=("Segoe UI", 9), padx=10, pady=6,
                  cursor="hand2").pack(side="right", padx=(4, 0))

        tk.Button(btn_row, text="Clear Log", command=self._clear_log,
                  bg="#313244", fg=FG, relief="flat",
                  font=("Segoe UI", 9), padx=10, pady=6,
                  cursor="hand2").pack(side="right")

    # ── browse ───────────────────────────────────────────────────

    def _browse_src(self):
        d = filedialog.askdirectory(title="Select source folder")
        if d:
            self.src_var.set(d)
            if not self.dst_var.get():
                self.dst_var.set(str(Path(d).parent / (Path(d).name + "_JPG")))

    def _browse_dst(self):
        d = filedialog.askdirectory(title="Select output folder")
        if d:
            self.dst_var.set(d)

    def _on_drop(self, event):
        """Handle drag-and-drop of files or folders."""
        data = event.data
        if not data:
            return

        # Clean up the dropped path(s)
        # On Windows: {C:/path/to/folder} or multiple {file1} {file2}
        # On macOS: /path/to/folder
        paths = []
        for item in data.strip("{}").split("} {"):
            item = item.strip()
            if item:
                paths.append(Path(item))

        if not paths:
            return

        first = paths[0]

        if first.is_dir():
            self.src_var.set(str(first))
            if not self.dst_var.get():
                self.dst_var.set(str(first.parent / (first.name + "_JPG")))
        elif first.suffix.lower() == ".pdf":
            # Dropped PDF file(s) — set source to parent folder
            self.src_var.set(str(first.parent))
            if not self.dst_var.get():
                self.dst_var.set(str(first.parent.parent / (first.parent.name + "_JPG")))
        else:
            # Try parent directory
            parent = first.parent
            if parent.is_dir():
                self.src_var.set(str(parent))
                if not self.dst_var.get():
                    self.dst_var.set(str(parent.parent / (parent.name + "_JPG")))

        # Auto-scan when dropping
        self.after(200, self._scan)

    # ── log helpers ──────────────────────────────────────────────

    def _log(self, msg: str):
        self.log.configure(state="normal")
        self.log.insert("end", msg + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _save_log(self):
        log_text = self.log.get("1.0", "end-1c")
        if not log_text.strip():
            messagebox.showinfo("Save Log", "Nothing to save — the log is empty.")
            return

        default_name = "pdf_to_jpg_log.txt"
        dst = self.dst_var.get().strip()
        if dst and Path(dst).exists():
            default_name = str(Path(dst) / default_name)

        path = filedialog.asksaveasfilename(
            title="Save log as",
            defaultextension=".txt",
            initialfile="pdf_to_jpg_log.txt",
            initialdir=dst if (dst and Path(dst).exists()) else None,
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")],
        )
        if path:
            try:
                Path(path).write_text(log_text, encoding="utf-8")
                self._log(f"Log saved to: {path}")
            except Exception as e:
                messagebox.showerror("Save Failed", str(e))

    # ── scan / preview ───────────────────────────────────────────

    def _scan(self):
        """Scan source folder and show a preview of all found PDFs with page counts."""
        src = self.src_var.get().strip()
        if not src:
            messagebox.showwarning("Missing folder", "Please set a source folder first.")
            return

        src_path = Path(src)
        if not src_path.is_dir():
            messagebox.showerror("Not found", f"Source folder does not exist:\n{src}")
            return

        self._clear_log()
        self.status_var.set("Scanning…")

        thread = threading.Thread(target=self._run_scan, args=(src_path,), daemon=True)
        thread.start()

    def _run_scan(self, src_root: Path):
        pdfs = list(find_pdfs(src_root))
        total = len(pdfs)

        if total == 0:
            self._log("No PDF files found in the source folder.")
            self.status_var.set("No PDFs found.")
            return

        self._log(f"Found {total} PDF file(s). Counting pages…\n")
        self._log(f"{'Pages':>6}  {'PDF File':<60}  Size")
        self._log("-" * 90)

        total_pages = 0
        total_size_mb = 0.0

        for i, pdf in enumerate(pdfs, start=1):
            size_mb = pdf.stat().st_size / (1024 * 1024)
            total_size_mb += size_mb
            pages = count_pdf_pages(pdf)
            total_pages += pages
            rel = pdf.relative_to(src_root)
            self._log(f"{pages:>5}p  {str(rel):<60}  {size_mb:>6.1f} MB")

            # Show progress on status bar
            if i % 10 == 0:
                self.status_var.set(f"Scanning… {i}/{total} ({total_pages} pages found so far)")

        self._log("-" * 90)
        avg_pages = total_pages / total if total > 0 else 0
        self._log(f"\n{total} PDFs  ·  {total_pages} pages  ·  "
                  f"{total_size_mb:.1f} MB total  ·  avg {avg_pages:.1f} pages/PDF")
        self._log(f"\nEstimated output at {self.dpi_var.get()} DPI: "
                  f"~{total_size_mb * 2:.0f}-{total_size_mb * 5:.0f} MB of JPGs")
        self._log("\nReady to convert. Click 'Convert All PDFs' to begin.")

        self.status_var.set(
            f"Scanned: {total} PDFs, {total_pages} pages total. Ready to convert."
        )

    # ── conversion ───────────────────────────────────────────────

    def _start(self):
        if not PDF2IMAGE_OK:
            messagebox.showerror("Missing library",
                "pdf2image is not installed.\n\nRun:\n  pip install pdf2image Pillow\n\n"
                "Also install Poppler for Windows and add it to your PATH.")
            return

        # Check Poppler before starting
        poppler_ok = self._check_poppler()
        if not poppler_ok:
            messagebox.showerror(
                "Poppler Not Found",
                "Poppler (PDF rendering library) was not found on your system PATH.\n\n"
                "This is required to convert PDFs.\n\n"
                "1. Download Poppler for Windows:\n"
                "   https://github.com/oschwartz10612/poppler-windows/releases\n\n"
                "2. Extract to a folder (e.g. C:\\poppler)\n\n"
                "3. Add the \\Library\\bin folder to your system PATH\n\n"
                "After installing, restart this application."
            )
            return

        src = self.src_var.get().strip()
        dst = self.dst_var.get().strip()
        if not src or not dst:
            messagebox.showwarning("Missing folders", "Please set both source and output folders.")
            return

        src_path = Path(src)
        dst_path = Path(dst)
        if not src_path.is_dir():
            messagebox.showerror("Not found", f"Source folder does not exist:\n{src}")
            return

        self._running = True
        self._cancel_flag.clear()
        self.convert_btn.configure(state="disabled")
        self.cancel_btn.configure(state="normal")
        self.scan_btn.configure(state="disabled")
        self._clear_log()

        dpi       = self.dpi_var.get()
        quality   = self.quality_var.get()
        workers   = self.workers_var.get()

        thread = threading.Thread(
            target=self._run,
            args=(src_path, dst_path, dpi, quality, workers),
            daemon=True,
        )
        thread.start()

    def _check_poppler(self) -> bool:
        """Check if Poppler is installed and accessible."""
        try:
            import subprocess
            result = subprocess.run(
                ["pdftoppm", "-v"], capture_output=True, timeout=5
            )
            return result.returncode == 0 or b"pdftoppm" in result.stderr
        except FileNotFoundError:
            pass
        try:
            result = subprocess.run(
                ["pdfinfo", "-v"], capture_output=True, timeout=5
            )
            return result.returncode == 0
        except FileNotFoundError:
            return False
        return False

    def _cancel(self):
        self._cancel_flag.set()
        self._running = False
        self.status_var.set("Cancelling…")

    def _run(self, src_root: Path, dst_root: Path, dpi: int, quality: int, workers: int):
        pdfs = list(find_pdfs(src_root))
        total = len(pdfs)

        if total == 0:
            self._log("No PDF files found in the source folder.")
            self._done("No PDFs found.")
            return

        # ── Count total pages for accurate progress ───────────────────────
        self._log(f"Found {total} PDF file(s). Counting pages…")
        pdf_info = []
        total_pages = 0
        for pdf in pdfs:
            pages = count_pdf_pages(pdf)
            size_mb = pdf.stat().st_size / (1024 * 1024)
            pdf_info.append((pdf, pages, size_mb))
            total_pages += pages

        # Determine if streaming is needed (PDFs > 50 pages)
        use_streaming = any(pages > 50 for _, pages, _ in pdf_info)

        self._log(f"Total: {total} PDFs, {total_pages} pages "
                  f"{'(streaming mode for large PDFs)' if use_streaming else ''}\n")

        self.progress["maximum"] = total_pages
        self.progress["value"]   = 0

        ok_count  = 0
        err_count = 0
        jpg_count = 0
        _page_lock = threading.Lock()
        _log_lock  = threading.Lock()

        def log_threadsafe(msg: str):
            with _log_lock:
                self._log(msg)

        def log_status(msg: str):
            self.status_var.set(msg)

        # ── Parallel conversion ──────────────────────────────────────────
        actual_workers = max(1, min(workers, total))

        with ThreadPoolExecutor(max_workers=actual_workers) as executor:
            futures = {}
            for pdf, pages, _size in pdf_info:
                if self._cancel_flag.is_set():
                    break
                task = ConvertTask(pdf, src_root, dst_root, dpi, quality,
                                   use_streaming and pages > 50)
                fut = executor.submit(
                    _convert_and_log, task, log_threadsafe
                )
                futures[fut] = (pdf, pages)

            for fut in as_completed(futures):
                if self._cancel_flag.is_set():
                    break

                pdf, pages_in_pdf = futures[fut]
                rel = pdf.relative_to(src_root)

                try:
                    success, written, err = fut.result()
                except Exception as e:
                    success, written, err = False, 0, str(e)

                with _page_lock:
                    if success:
                        ok_count  += 1
                        jpg_count += written
                        log_threadsafe(f"✓ {rel} ({written} pages)")
                    else:
                        err_count += 1
                        log_threadsafe(f"✗ {rel} — ERROR: {err}")

                    self.progress["value"] += pages_in_pdf
                    progress_pct = int(self.progress["value"] / total_pages * 100)
                    log_status(
                        f"Converting… {ok_count + err_count}/{total} PDFs "
                        f"({self.progress['value']}/{total_pages} pages, {progress_pct}%)"
                    )

        if not self._cancel_flag.is_set():
            summary = (f"\nDone! {ok_count} PDF(s) converted → {jpg_count} JPG(s). "
                       f"{err_count} error(s).")
        else:
            summary = f"\nCancelled. {ok_count} PDF(s) converted → {jpg_count} JPG(s)."

        self._log(summary)
        self._done(summary.strip())

    def _done(self, msg: str):
        self._running = False
        self.status_var.set(msg)
        self.convert_btn.configure(state="normal")
        self.cancel_btn.configure(state="disabled")
        self.scan_btn.configure(state="normal")


def _convert_and_log(task: ConvertTask, log_fn) -> tuple:
    """Convert a single PDF and log each page. Thread-safe wrapper."""
    from pathlib import Path
    from pdf2image import convert_from_path
    from pdf2image.exceptions import PDFPageCountError, PDFSyntaxError

    try:
        if not task.streaming:
            images = convert_from_path(str(task.pdf_path), dpi=task.dpi)
            total = len(images)
            written = 0
            for i, img in enumerate(images, start=1):
                out = output_path_for(task.pdf_path, task.src_root, task.dst_root, i, total)
                out.parent.mkdir(parents=True, exist_ok=True)
                img.save(str(out), "JPEG", quality=task.quality, optimize=True)
                written += 1
                log_fn(f"  ✓ {out.relative_to(task.dst_root)}")
            return True, written, None

        # ── Streaming mode for large PDFs ────────────────────────────────
        written = 0
        page = 1
        try:
            import subprocess
            import re
            result = subprocess.run(
                ["pdfinfo", str(task.pdf_path)], capture_output=True, text=True, timeout=30
            )
            match = re.search(r"Pages:\s+(\d+)", result.stdout)
            total_pages = int(match.group(1)) if match else 999
        except Exception:
            total_pages = 999

        while True:
            batch = convert_from_path(
                str(task.pdf_path), dpi=task.dpi,
                first_page=page, last_page=page + 9,
            )
            if not batch:
                break

            for img in batch:
                if page > total_pages:
                    total_pages = page
                out = output_path_for(task.pdf_path, task.src_root, task.dst_root, page, total_pages)
                out.parent.mkdir(parents=True, exist_ok=True)
                img.save(str(out), "JPEG", quality=task.quality, optimize=True)
                written += 1
                log_fn(f"  ✓ {out.relative_to(task.dst_root)}")
                page += 1

            if len(batch) < 10:
                break

        return True, written, None

    except (PDFPageCountError, PDFSyntaxError) as e:
        return False, 0, str(e)
    except Exception as e:
        return False, 0, str(e)


# ─────────────────────────── entry ───────────────────────────────

if __name__ == "__main__":
    app = App()
    app.mainloop()
