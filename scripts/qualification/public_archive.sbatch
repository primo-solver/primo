#!/usr/bin/env bash
#SBATCH --job-name=primo-public-archive
#SBATCH --cpus-per-task=4
#SBATCH --mem=8G
#SBATCH --time=00:30:00
#SBATCH --output=results/public-archive-%j.out
#SBATCH --error=results/public-archive-%j.err
set -euo pipefail
cd "${PRIMO_WORKDIR:?}"
printf '%s  %s\n' "${PRIMO_ARCHIVE_SHA256:?}" "${PRIMO_SOURCE_ARCHIVE:?}" | sha256sum -c -
temp=$(mktemp -d "$PWD/results/archive-${SLURM_JOB_ID}.XXXXXX")
tar -xzf "$PRIMO_SOURCE_ARCHIVE" -C "$temp"
cd "$temp/$(basename "$PRIMO_SOURCE_ARCHIVE" .tar.gz)"
test ! -e .git
grep -Fx "${PRIMO_ARCHIVE_REVISION:?}" SOURCE-REVISION
export PATH="${PRIMO_CMAKE_BIN:?}:$PATH" CMAKE_BUILD_PARALLEL_LEVEL=4
if [[ -n ${PRIMO_CXX_HEADER_MANIFEST:-} ]]; then
  while IFS= read -r header; do
    [[ $header =~ ^[a-zA-Z0-9_./-]+$ ]] && test -d "$header"
    export CXXFLAGS="${CXXFLAGS:-} -isystem $header"
  done < "$PRIMO_CXX_HEADER_MANIFEST"
fi
# This checks archive independence from Git. Empty-cache bootstrap is a
# separate qualification job; provisioned dependencies are used explicitly here.
ln -s "${PRIMO_DEPENDENCY_PREFIX:?}" contrib/local
make -o contrib-required build
build/primo --version | grep -Fx "git: $PRIMO_ARCHIVE_REVISION"
python3 scripts/check_doc_examples.py --solver build/primo
sha256sum build/primo
