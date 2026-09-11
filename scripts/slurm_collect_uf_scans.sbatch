#!/usr/bin/env bash
#SBATCH --job-name=primo-uf-scan-artifacts
#SBATCH --array=0-5%4
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --time=00:45:00
#SBATCH --output=slurm_logs/%x-%A_%a.out
#SBATCH --error=slurm_logs/%x-%A_%a.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
revision=${PRIMO_EXPECTED_REVISION:?}
slot=${SLURM_ARRAY_TASK_ID:?}
case "$slot" in
  0) paths=("results/uf-scans-native-$revision" "results/uf-scans-paired-$revision"
      "results/uf-scans-default-$revision" "results/uf-scans-regressions-$revision"
      build-slurm-rel/primo) ;;
  1|2|3|4)
     paths=()
     for (( i=(slot-1)*4; i<slot*4; ++i )); do
       paths+=("results/uf-scans-validation-$revision/assumptions/shard-$i")
     done ;;
  5) paths=("results/uf-scans-gate-$revision") ;;
  *) exit 2 ;;
esac
out="results/uf-scans-$revision-artifacts"
mkdir -p "$out"
archive="$out/part-$slot.tar.zst"
test ! -e "$archive"
test ! -e "$archive.partial"
tar --sort=name --numeric-owner -cf - "${paths[@]}" | zstd -T1 -3 -o "$archive.partial"
mv "$archive.partial" "$archive"
sha256sum "$archive" > "$archive.sha256"
wc -c "$archive"
