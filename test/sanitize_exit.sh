#!/bin/bash
# A failing executable must survive the sanitizer runner's output pipeline.
set -euo pipefail
runner=$(cd "$(dirname "$0")" && pwd)/sanitize.sh
scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
mkdir -p "$scratch"/{src,test,third_party/sqlite,tools}
touch "$scratch/src/dummy.c" "$scratch/test/dummy.c" "$scratch/third_party/sqlite/sqlite3.c"
cat > "$scratch/tools/cc" <<'CC'
#!/bin/bash
while [[ $# -gt 0 ]]; do
  if [[ $1 == -o ]]; then
    shift
    printf '#!/bin/sh\nexit 7\n' > "$1"
    chmod +x "$1"
    exit 0
  fi
  shift
done
exit 2
CC
chmod +x "$scratch/tools/cc"
set +e
(cd "$scratch" && PATH="$scratch/tools:$PATH" bash "$runner" ubsan dummy)
rc=$?
set -e
[[ $rc == 7 ]] || { echo "sanitizer runner masked exit 7: got $rc"; exit 1; }
echo 'sanitizer exit propagation: passed'
