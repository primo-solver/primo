#!/usr/bin/env bash
# Restore the official SMT-LIB 2025 non-incremental QF_LIA archive.
# Source: https://zenodo.org/records/16740866 (published file checksum below).
#SBATCH --job-name=primo-refresh-lia-data
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=2G
#SBATCH --time=00:15:00
#SBATCH --output=logs/%x-%j.out
#SBATCH --error=logs/%x-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
printf '%s\n' '6b78b5c578394cf7a72b22087279f34f  downloads/QF_LIA.tar.zst' | md5sum -c -
sha256sum downloads/QF_LIA.tar.zst > downloads/QF_LIA.sha256
mkdir downloads/qf-lia-extracted
tar --zstd --no-same-owner --no-same-permissions -xf downloads/QF_LIA.tar.zst \
  -C downloads/qf-lia-extracted
test -d downloads/qf-lia-extracted/non-incremental/QF_LIA
