"""Check or format project C++ and Protobuf sources using clang-format 18."""

import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fix", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    names = subprocess.check_output(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root,
    ).decode().split("\0")
    sources = sorted({
        name for name in names
        if Path(name).suffix in {".cc", ".h", ".proto"}
        and not name.startswith("proto/grpc/")
    })
    flags = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    subprocess.run(["clang-format-18", *flags, *sources], cwd=root, check=True)
    print(f"Formatting {'applied to' if args.fix else 'passed for'} {len(sources)} project files")


if __name__ == "__main__":
    main()
