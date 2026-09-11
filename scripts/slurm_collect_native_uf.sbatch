#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-artifacts
#SBATCH --array=0-9%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=01:15:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
revision=${PRIMO_EXPECTED_REVISION:?}
slot=${SLURM_ARRAY_TASK_ID:?}
# Independent parts bound the latency of traversing many small query files.
# Run after successful validation; never write into solver result directories.
case "$slot" in
  0) name=timing; paths=("results/uf-native-timing-$revision") ;;
  1) name=models-rebuild; paths=("results/uf-native-validation-$revision/rebuild") ;;
  2) name=models-assumptions; paths=("results/uf-native-validation-$revision/assumptions") ;;
  3) name=gate-auto; paths=("results/uf-auto-gate-$revision") ;;
  4) name=gate-matrix; paths=("results/uf-matrix-gate-$revision") ;;
  5) name=gate-extra; paths=("results/uf-extra-gate-$revision") ;;
  6) name=gate-mixed; paths=("results/uf-mixed-gate-$revision") ;;
  7) name=default; paths=("results/uf-default-$revision") ;;
  8) name=regressions; paths=("results/uf-regressions-$revision") ;;
  9) name=inputs-binary; paths=("results/uf-candidate-$revision"
      "${PRIMO_UF_MANIFEST:?}" "${PRIMO_UF_ARCHIVE:?}") ;;
  *) echo "invalid artifact slot: $slot" >&2; exit 2 ;;
esac
out="results/uf-$revision-artifacts"
mkdir -p "$out"
archive="$out/$name.tar.zst"
test ! -e "$archive"
test ! -e "$archive.partial"
# Follow the explicit reference-result links to preserve their unchanged records.
# Numeric ownership avoids environment-specific user/group names in headers.
tar --sort=name --numeric-owner -chf - "${paths[@]}" | zstd -T1 -3 -o "$archive.partial"
mv "$archive.partial" "$archive"
sha256sum "$archive" > "$archive.sha256"
wc -c "$archive"
cat "$archive.sha256"
