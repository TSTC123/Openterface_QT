#!/usr/bin/env python3
"""
auto_translate.py - Automated Qt .ts file translation using local Ollama

Subcommands:
  fill   Fill unfinished translations in existing .ts files
  add    Add a brand-new language: update cmake/.pro, run lupdate, then translate

Requirements:
  pip install requests
  # Ollama must be running locally:  https://ollama.com
  # Pull a model first, e.g.:  ollama pull qwen2.5:7b

Examples:
  # Fill all languages (default model: qwen2.5:7b)
  python scripts/auto_translate.py fill

  # Fill only Chinese and German
  python scripts/auto_translate.py fill --lang zh de

  # Use a specific model
  python scripts/auto_translate.py fill --model llama3.1:8b

  # Preview without modifying files
  python scripts/auto_translate.py fill --dry-run

  # Add Korean as a new language
  python scripts/auto_translate.py add --lang-code ko

  # Add Brazilian Portuguese
  python scripts/auto_translate.py add --lang-code pt --target-lang "Brazilian Portuguese"

  # Use a remote Ollama instance
  python scripts/auto_translate.py fill --base-url http://192.168.1.100:11434
"""

import argparse
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    import requests
except ImportError:
    print("Error: 'requests' package is not installed.")
    print("  Run: pip install requests")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Paths (relative to the project root, computed from this script's location)
# ---------------------------------------------------------------------------
SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
TS_DIR = PROJECT_ROOT / "config" / "languages"
CMAKE_FILE = PROJECT_ROOT / "cmake" / "Internationalization.cmake"
RESOURCES_CMAKE = PROJECT_ROOT / "cmake" / "Resources.cmake"
PRO_FILE = PROJECT_ROOT / "openterfaceQT.pro"

DEFAULT_MODEL = "qwen2.5:7b"
DEFAULT_BASE_URL = "http://localhost:11434"

# Qt language code → full language name used in the translation prompt
LANG_NAME_MAP: dict[str, str] = {
    "da": "Danish",
    "de": "German",
    "es": "Spanish",
    "fr": "French",
    "ja": "Japanese",
    "se": "Swedish",
    "zh": "Simplified Chinese",
    "ko": "Korean",
    "it": "Italian",
    "nl": "Dutch",
    "pl": "Polish",
    "pt": "Portuguese",
    "ru": "Russian",
    "ro": "Romanian",
    "tr": "Turkish",
    "ar": "Arabic",
    "vi": "Vietnamese",
    "th": "Thai",
}

# Source language – skip translating these
SKIP_LANGS = {"en"}


# ---------------------------------------------------------------------------
# Ollama client
# ---------------------------------------------------------------------------

class OllamaClient:
    def __init__(self, base_url: str, model: str):
        self.base_url = base_url.rstrip("/")
        self.model = model
        self._check_connection()

    def _check_connection(self) -> None:
        try:
            resp = requests.get(f"{self.base_url}/api/tags", timeout=5)
            resp.raise_for_status()
            models = [m["name"] for m in resp.json().get("models", [])]
            model_base = self.model.split(":")[0]
            found = any(model_base in m for m in models)
            if not found:
                print(f"  Warning: model '{self.model}' not found in Ollama.")
                print(f"  Available: {', '.join(models) or '(none)'}")
                print(f"  Pull it with:  ollama pull {self.model}")
        except requests.exceptions.ConnectionError:
            print(f"Error: Cannot connect to Ollama at {self.base_url}")
            print("  Make sure Ollama is running:  ollama serve")
            sys.exit(1)

    def translate(self, text: str, target_lang: str) -> str:
        """Translate a single string via Ollama /api/chat."""
        system_prompt = (
            f"You are a professional software UI translator. "
            f"Translate the following English UI text to {target_lang}.\n"
            f"Rules:\n"
            f"- Return ONLY the translated text, nothing else\n"
            f"- Keep placeholders like %1, %2, {{0}}, \\n unchanged\n"
            f"- Keep the same capitalization style\n"
            f"- Do not add quotes or explanations"
        )
        payload = {
            "model": self.model,
            "messages": [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": text},
            ],
            "stream": False,
            "options": {"temperature": 0.1, "num_predict": 256},
        }
        for attempt in range(3):
            try:
                resp = requests.post(
                    f"{self.base_url}/api/chat",
                    json=payload,
                    timeout=60,
                )
                resp.raise_for_status()
                result = resp.json()["message"]["content"].strip()
                # Strip surrounding quotes the model may accidentally add
                if len(result) >= 2 and result[0] in ('"', "'", "\u201c") and result[-1] in ('"', "'", "\u201d"):
                    result = result[1:-1].strip()
                return result
            except requests.exceptions.Timeout:
                if attempt < 2:
                    print(f"    Timeout, retrying ({attempt + 1}/3)...")
                    time.sleep(2)
                else:
                    print(f"    Failed after 3 attempts, keeping original.")
                    return text
            except Exception as e:
                print(f"    Error: {e}")
                return text
        return text


def get_lang_name(lang_code: str, override: str | None = None) -> str:
    """Return full language name used in the translation prompt."""
    if override:
        return override
    return LANG_NAME_MAP.get(lang_code.lower(), lang_code)


def _ensure_et_indent():
    """ET.indent was added in Python 3.9; provide a fallback for older versions."""
    if not hasattr(ET, "indent"):
        def _indent(elem, space="  ", level=0):
            i = "\n" + level * space
            if len(elem):
                if not elem.text or not elem.text.strip():
                    elem.text = i + space
                if not elem.tail or not elem.tail.strip():
                    elem.tail = i
                for child in elem:
                    _indent(child, space, level + 1)
                if not child.tail or not child.tail.strip():
                    child.tail = i
            else:
                if level and (not elem.tail or not elem.tail.strip()):
                    elem.tail = i
        ET.indent = _indent  # type: ignore[attr-defined]


def parse_ts_file(ts_path: Path):
    """
    Parse a Qt .ts file.

    Returns (root, header_text) where header_text is everything
    before the opening <TS tag (XML declaration + DOCTYPE).
    """
    raw = ts_path.read_text(encoding="utf-8")

    # Capture header lines (<?xml ...?> and <!DOCTYPE TS>)
    header_lines = []
    for line in raw.splitlines():
        stripped = line.strip()
        if stripped.startswith("<TS"):
            break
        if stripped:  # skip blank lines between decl and <TS
            header_lines.append(line)

    root = ET.fromstring(raw)
    return root, "\n".join(header_lines)


def write_ts_file(ts_path: Path, root: ET.Element, header: str) -> None:
    """Write a Qt .ts file preserving the original XML declaration and DOCTYPE."""
    _ensure_et_indent()
    ET.indent(root, space="    ")
    body = ET.tostring(root, encoding="unicode", xml_declaration=False)
    output = header + "\n" + body + "\n"
    ts_path.write_text(output, encoding="utf-8")


def collect_unfinished(root: ET.Element) -> list[tuple[ET.Element, str]]:
    """Return list of (translation_element, source_text) for every unfinished entry."""
    items: list[tuple[ET.Element, str]] = []
    for context in root.findall("context"):
        for message in context.findall("message"):
            source = message.find("source")
            translation = message.find("translation")
            if source is None or translation is None:
                continue
            if translation.get("type") == "unfinished":
                src_text = source.text or ""
                if src_text.strip():
                    items.append((translation, src_text))
    return items


def fill_ts_file(
    ts_path: Path,
    lang_name: str,
    client: OllamaClient,
    *,
    dry_run: bool = False,
) -> int:
    """Fill unfinished entries in a single .ts file. Returns the count translated."""
    root, header = parse_ts_file(ts_path)
    items = collect_unfinished(root)

    if not items:
        print(f"  [{ts_path.name}] All entries already translated, skipping.")
        return 0

    print(f"  [{ts_path.name}] {len(items)} unfinished entries -> {lang_name}  (model: {client.model})")

    if dry_run:
        preview = items[:5]
        for _, src in preview:
            print(f"    » \"{src[:70]}\"")
        if len(items) > 5:
            print(f"    ... and {len(items) - 5} more")
        return len(items)

    for idx, (elem, src) in enumerate(items, 1):
        print(f"    [{idx}/{len(items)}] {src[:55]:<55}", end="\r", flush=True)
        result = client.translate(src, lang_name)
        elem.text = result
        elem.attrib.pop("type", None)  # remove type="unfinished"

    print(f"    {'':60}", end="\r")  # clear progress line
    write_ts_file(ts_path, root, header)
    print(f"  [{ts_path.name}] Done. {len(items)} entries translated.")
    return len(items)


# ---------------------------------------------------------------------------
# Subcommand: fill
# ---------------------------------------------------------------------------

def cmd_fill(args) -> None:
    ts_dir = Path(args.ts_dir)
    if not ts_dir.is_dir():
        print(f"Error: TS directory not found: {ts_dir}")
        sys.exit(1)

    if args.lang:
        lang_codes = [lc.lower() for lc in args.lang]
    else:
        lang_codes = sorted(
            p.stem.split("_", 1)[-1]
            for p in ts_dir.glob("openterface_*.ts")
        )
    lang_codes = [lc for lc in lang_codes if lc not in SKIP_LANGS]

    print("=" * 60)
    print("Ollama Auto-translate  |  mode: fill")
    print("=" * 60)
    print(f"  Ollama URL   : {args.base_url}")
    print(f"  Model        : {args.model}")
    print(f"  TS directory : {ts_dir}")
    print(f"  Languages    : {', '.join(lang_codes)}")
    if args.dry_run:
        print("  DRY RUN      : files will NOT be modified")
    print()

    client = None if args.dry_run else OllamaClient(args.base_url, args.model)

    total = 0
    start = time.time()
    for lc in lang_codes:
        ts_path = ts_dir / f"openterface_{lc}.ts"
        if not ts_path.exists():
            print(f"  [{lc}] File not found: {ts_path}, skipping.")
            continue
        lang_name = get_lang_name(lc)
        if args.dry_run:
            root, _ = parse_ts_file(ts_path)
            items = collect_unfinished(root)
            print(f"  [{ts_path.name}] {len(items)} unfinished entries -> {lang_name}")
            for _, src in items[:3]:
                print(f"    \u00bb \"{src[:70]}\"")
            if len(items) > 3:
                print(f"    ... and {len(items) - 3} more")
            total += len(items)
        else:
            total += fill_ts_file(ts_path, lang_name, client, dry_run=False)

    elapsed = time.time() - start
    print()
    print(f"Total entries translated: {total}  ({elapsed:.1f}s)")


# ---------------------------------------------------------------------------
# Subcommand: add
# ---------------------------------------------------------------------------

def find_lupdate() -> str | None:
    """Auto-detect the lupdate executable."""
    candidates = [
        "lupdate",
        "lupdate-qt6",
        "/opt/Qt6/bin/lupdate",
        "/usr/lib/qt6/bin/lupdate",
        "/usr/local/lib/qt6/bin/lupdate",
        "/usr/local/bin/lupdate",
    ]

    if sys.platform == "win32":
        import glob as _glob

        for pattern in [
            r"C:\Qt\*\*\bin\lupdate.exe",
            r"C:\Qt\*\*\*\bin\lupdate.exe",
        ]:
            candidates.extend(_glob.glob(pattern))

    for candidate in candidates:
        try:
            result = subprocess.run(
                [candidate, "-version"],
                capture_output=True,
                timeout=5,
            )
            if result.returncode == 0:
                return candidate
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
    return None


def add_lang_to_cmake(lang_code: str) -> None:
    """Append a new .ts entry to TS_FILES in cmake/Internationalization.cmake."""
    content = CMAKE_FILE.read_text(encoding="utf-8")

    if f"openterface_{lang_code}.ts" in content:
        print(f"  cmake : {lang_code} already present in TS_FILES.")
        return

    new_entry = (
        f"    ${{CMAKE_CURRENT_SOURCE_DIR}}/config/languages/openterface_{lang_code}.ts"
    )

    # Insert new entry before the closing ) of set(TS_FILES ...)
    # Matches the last .ts line immediately followed by a line with just ")"
    new_content = re.sub(
        r"(    \$\{CMAKE_CURRENT_SOURCE_DIR\}/config/languages/openterface_\w+\.ts\n)(\))",
        rf"\1{new_entry}\n\2",
        content,
    )

    if new_content == content:
        print(f"  cmake : Could not auto-update TS_FILES. Add manually:")
        print(f"    {new_entry}")
        return

    CMAKE_FILE.write_text(new_content, encoding="utf-8")
    print(f"  cmake : Added {lang_code} to TS_FILES.")


def add_lang_to_resources(lang_code: str) -> None:
    """Append a new .qm entry to languages_resources_files in cmake/Resources.cmake."""
    content = RESOURCES_CMAKE.read_text(encoding="utf-8")

    if f"openterface_{lang_code}.qm" in content:
        print(f"  resources: {lang_code} already present in languages_resources_files.")
        return

    new_entry = f'    "config/languages/openterface_{lang_code}.qm"'

    new_content = re.sub(
        r'(    "config/languages/openterface_\w+\.qm"\n)(\))',
        rf'\1{new_entry}\n\2',
        content,
    )

    if new_content == content:
        print(f"  resources: Could not auto-update languages_resources_files. Add manually:")
        print(f'    {new_entry}')
        return

    RESOURCES_CMAKE.write_text(new_content, encoding="utf-8")
    print(f"  resources: Added {lang_code} to languages_resources_files.")


def add_lang_to_pro(lang_code: str) -> None:
    """Append a new .ts entry to TRANSLATIONS in openterfaceQT.pro."""
    content = PRO_FILE.read_text(encoding="utf-8")

    if f"openterface_{lang_code}.ts" in content:
        print(f"  .pro  : {lang_code} already present in TRANSLATIONS.")
        return

    new_entry = f"                config/languages/openterface_{lang_code}.ts"

    # The last .ts line has no trailing backslash; add one and insert new line.
    # Match the last .ts entry (not followed by another .ts line).
    new_content = re.sub(
        r"(                config/languages/openterface_\w+\.ts)([ \t]*\n)(?![ \t]*config/languages/)",
        rf"\1 \\\n{new_entry}\2",
        content,
    )

    if new_content == content:
        print(f"  .pro  : Could not auto-update TRANSLATIONS. Add manually:")
        print(f"    {new_entry} \\")
        return

    PRO_FILE.write_text(new_content, encoding="utf-8")
    print(f"  .pro  : Added {lang_code} to TRANSLATIONS.")


def cmd_add(args) -> None:
    lang_code = args.lang_code.lower()
    lang_name = get_lang_name(lang_code, args.target_lang)
    ts_path = TS_DIR / f"openterface_{lang_code}.ts"

    print("=" * 60)
    print("Ollama Auto-translate  |  mode: add")
    print("=" * 60)
    print(f"  Language code : {lang_code}")
    print(f"  Language name : {lang_name}")
    print(f"  Model         : {args.model}")
    print(f"  TS file       : {ts_path}")
    print()

    if ts_path.exists():
        print(f"  '{ts_path.name}' already exists.")
        print("  Use the 'fill' subcommand to fill missing entries instead.")
        sys.exit(1)

    # 1. Update project configuration files
    print("--- Updating project configuration ---")
    add_lang_to_cmake(lang_code)
    add_lang_to_pro(lang_code)
    add_lang_to_resources(lang_code)
    print()

    # 2. Run lupdate to generate empty .ts skeleton
    print("--- Running lupdate ---")
    lupdate = args.lupdate or find_lupdate()
    if not lupdate:
        print(
            "Error: lupdate not found. Install Qt6 development tools or pass --lupdate /path/to/lupdate"
        )
        sys.exit(1)

    print(f"  Executable : {lupdate}")
    result = subprocess.run(
        [lupdate, str(PRO_FILE), "-no-obsolete"],
        cwd=str(PROJECT_ROOT),
    )
    if result.returncode != 0:
        print("  Warning: lupdate returned a non-zero exit code, continuing...")

    if not ts_path.exists():
        print(
            f"  Error: '{ts_path.name}' was not created by lupdate.\n"
            "  Verify that TRANSLATIONS in openterfaceQT.pro was updated correctly."
        )
        sys.exit(1)
    print(f"  Created: {ts_path.name}")
    print()

    # 3. Translate all entries in the new file
    print(f"--- Translating {ts_path.name} ---")
    client = OllamaClient(args.base_url, args.model)
    count = fill_ts_file(ts_path, lang_name, client)

    print()
    print("=" * 60)
    print(f"Done: language '{lang_code}' added \u2014 {count} entries translated.")
    print("=" * 60)
    print("Next steps:")
    print(f"  1. Review {ts_path} for translation quality")
    print(f"  2. Compile: lrelease {PRO_FILE.name}")
    print(f"  3. Register the language in the UI language selector")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def _add_ollama_args(p: argparse.ArgumentParser) -> None:
    """Add --model and --base-url to a subcommand parser."""
    p.add_argument(
        "--model",
        default=os.environ.get("OLLAMA_MODEL", DEFAULT_MODEL),
        metavar="NAME",
        help=f"Ollama model name (default: {DEFAULT_MODEL}, or OLLAMA_MODEL env var)",
    )
    p.add_argument(
        "--base-url",
        default=os.environ.get("OLLAMA_BASE_URL", DEFAULT_BASE_URL),
        metavar="URL",
        help=f"Ollama server URL (default: {DEFAULT_BASE_URL}, or OLLAMA_BASE_URL env var)",
    )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Automated Qt .ts translation via local Ollama LLM",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )

    sub = parser.add_subparsers(dest="command", required=True)

    # ── fill ──────────────────────────────────────────────────────────────
    p_fill = sub.add_parser("fill", help="Fill unfinished translations in existing .ts files")
    _add_ollama_args(p_fill)
    p_fill.add_argument(
        "--ts-dir",
        default=str(TS_DIR),
        metavar="DIR",
        help=f"Directory containing .ts files (default: {TS_DIR})",
    )
    p_fill.add_argument(
        "--lang",
        nargs="+",
        metavar="CODE",
        help="Language code(s) to process, e.g. zh de ja. Default: all except en",
    )
    p_fill.add_argument(
        "--dry-run",
        action="store_true",
        help="Preview unfinished entries without calling Ollama or writing files",
    )
    p_fill.set_defaults(func=cmd_fill)

    # ── add ───────────────────────────────────────────────────────────────
    p_add = sub.add_parser(
        "add", help="Add a new language: update cmake/.pro, run lupdate, then translate"
    )
    _add_ollama_args(p_add)
    p_add.add_argument(
        "--lang-code",
        required=True,
        metavar="CODE",
        help="Qt language suffix, e.g. ko  ->  openterface_ko.ts",
    )
    p_add.add_argument(
        "--target-lang",
        metavar="NAME",
        help='Full language name for the prompt, e.g. "Brazilian Portuguese". '
             "Defaults to built-in mapping.",
    )
    p_add.add_argument(
        "--lupdate",
        metavar="PATH",
        help="Path to lupdate executable. Auto-detected if not specified",
    )
    p_add.set_defaults(func=cmd_add)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()