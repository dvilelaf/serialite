#!/usr/bin/env bash
# Builds a release under dist/ ready to upload as GitHub release assets:
#   serialite-<version>.tar.gz          firmware bundle
#   serialite-<version>.tar.gz.sha256   checksum the flasher verifies before extracting
#   flash-latest-firmware.sh            end-user flasher
#   setup-linux-serial-console.sh       host setup
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

run_build=1
allow_dirty=0
profile="lab"
version="${SERIALITE_RELEASE_VERSION:-}"
build_dir="${SERIALITE_BUILD_DIR:-}"
dist_dir="${SERIALITE_DIST_DIR:-dist}"
release_base_url="${SERIALITE_RELEASE_BASE_URL:-https://github.com/dvilelaf/serialite/releases/download}"

usage() {
    cat <<'EOF'
Usage:
  scripts/package-release.sh [--version vX.Y.Z] [--profile lab|prod] [--no-build] [--allow-dirty]

--version defaults to the tag on HEAD. The lab profile is what end users flash;
prod needs the signing key referenced by sdkconfig.prod.defaults.
EOF
}

die() {
    echo "$*" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-build) run_build=0; shift ;;
        --allow-dirty) allow_dirty=1; shift ;;
        --version) [ "$#" -ge 2 ] || die "missing value for --version"; version="$2"; shift 2 ;;
        --profile) [ "$#" -ge 2 ] || die "missing value for --profile"; profile="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) usage >&2; die "unknown argument: $1" ;;
    esac
done

case "$profile" in
    lab) defaults="sdkconfig.defaults" ;;
    prod) defaults="sdkconfig.defaults;sdkconfig.prod.defaults" ;;
    *) die "profile must be lab or prod" ;;
esac
build_dir="${build_dir:-build-${profile}}"

if [ "$allow_dirty" != "1" ] && [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    die "working tree has uncommitted changes; commit them or pass --allow-dirty"
fi
if [ -z "$version" ]; then
    version="$(git describe --tags --exact-match 2>/dev/null)" || die "HEAD has no tag; tag it or pass --version"
fi
printf '%s\n' "$version" | grep -Eq '^[A-Za-z0-9._-]+$' || die "invalid release version: $version"

commit="$(git rev-parse HEAD)"
SOURCE_DATE_EPOCH="$(git log -1 --format=%ct)"
export SOURCE_DATE_EPOCH

if [ "$run_build" = "1" ]; then
    command -v idf.py >/dev/null 2>&1 || { echo "idf.py not found. Source ESP-IDF's export.sh first." >&2; exit 127; }
    if [ "$profile" = "prod" ]; then
        scripts/lint_production_profile.py
    fi
    idf.py -B "$build_dir" -D SDKCONFIG="$build_dir/sdkconfig" -D SDKCONFIG_DEFAULTS="$defaults" build
fi

for file in flash_args bootloader/bootloader.bin partition_table/partition-table.bin ota_data_initial.bin serialite.bin; do
    [ -s "$build_dir/$file" ] || { echo "missing build output: $build_dir/$file" >&2; exit 1; }
done
idf_version="unknown"
if [ -f "$build_dir/project_description.json" ]; then
    idf_version="$(sed -n 's/.*"git_revision": *"\([^"]*\)".*/\1/p' "$build_dir/project_description.json" | head -1)"
fi

name="serialite-${version}"
bundle="$dist_dir/$name"
rm -rf "$bundle" "$dist_dir/$name.tar.gz" "$dist_dir/$name.tar.gz.sha256"
mkdir -p "$bundle"

cp "$build_dir/bootloader/bootloader.bin" "$build_dir/partition_table/partition-table.bin" \
   "$build_dir/ota_data_initial.bin" "$build_dir/serialite.bin" "$bundle/"
# The build's own offsets, with paths flattened to the bundle layout.
sed -e 's#bootloader/##' -e 's#partition_table/##' "$build_dir/flash_args" >"$bundle/flash_args"
cp README.md LICENSE THIRD_PARTY_NOTICES.md "$bundle/"

cat >"$bundle/manifest.txt" <<EOF
Serialite ${version}
Profile: ${profile}
Commit: ${commit}
ESP-IDF: ${idf_version}

Flash with:
  python3 -m esptool --chip esp32s3 -p /dev/ttyACM<N> write_flash @flash_args

flash_args:
$(sed 's/^/  /' "$bundle/flash_args")
EOF

if [ "$profile" = "prod" ]; then
    cat >"$bundle/INSTALL.md" <<EOF
# Serialite ${version} (production)

This bundle is signed for Secure Boot. serialite.bin can be installed from the
web console (Firmware page) on boards provisioned with the same key.

Provisioning a new board burns eFuses and cannot be undone. Follow the ESP-IDF
Secure Boot v2 and flash encryption guides for first-time flashing; the
offsets are in flash_args.
EOF
else
    cat >"$bundle/INSTALL.md" <<EOF
# Serialite ${version}

Flash the board (it verifies the download and asks before writing):

\`\`\`bash
curl -fsSL ${release_base_url%/}/${version}/flash-latest-firmware.sh | bash -s -- --version ${version}
\`\`\`

Or by hand, from this directory:

\`\`\`bash
python3 -m esptool --chip esp32s3 -p /dev/ttyACM<N> write_flash @flash_args
\`\`\`

Then set up the Linux server Serialite plugs into; read the script first, it runs as root:

\`\`\`bash
curl -fsSLO ${release_base_url%/}/${version}/setup-linux-serial-console.sh
less setup-linux-serial-console.sh
sudo sh setup-linux-serial-console.sh
\`\`\`
EOF
fi

(
    cd "$bundle"
    sums="$(find . -type f -printf '%P\n' | LC_ALL=C sort | xargs sha256sum)"
    printf '%s\n' "$sums" >SHA256SUMS
)

# Same inputs give the same archive: fixed order, owners and timestamps.
tar -C "$dist_dir" --sort=name --mtime="@${SOURCE_DATE_EPOCH}" --owner=0 --group=0 --numeric-owner -cf - "$name" \
    | gzip -n >"$dist_dir/$name.tar.gz"
(cd "$dist_dir" && sha256sum "$name.tar.gz" >"$name.tar.gz.sha256")
cp tools/flash-latest-firmware.sh tools/host/setup-linux-serial-console.sh "$dist_dir/"

echo "Release assets in $dist_dir:"
(cd "$dist_dir" && ls -1 "$name.tar.gz" "$name.tar.gz.sha256" flash-latest-firmware.sh setup-linux-serial-console.sh)
