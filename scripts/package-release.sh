#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

run_build=1
version="${ESP32_KVM_RELEASE_VERSION:-}"
build_dir="${ESP32_KVM_BUILD_DIR:-build}"
dist_dir="${ESP32_KVM_DIST_DIR:-dist}"
raw_base_url="${ESP32_KVM_RAW_BASE_URL:-https://raw.githubusercontent.com/YOUR_ORG/esp32-kvm}"

usage() {
    cat <<'EOF'
Usage:
  scripts/package-release.sh [--no-build] [--version vX.Y.Z]

Builds or reuses local ESP-IDF output and creates a release bundle under dist/.
The bundle is intended for GitHub/GitLab release assets, not for committing.
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --no-build)
            run_build=0
            shift
            ;;
        --version)
            if [ "$#" -lt 2 ]; then
                echo "missing value for --version" >&2
                exit 2
            fi
            version="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "$version" ]; then
    version="$(git describe --tags --always --dirty)"
fi
if ! printf '%s\n' "$version" | grep -Eq '^[A-Za-z0-9._-]+$'; then
    echo "invalid release version: $version" >&2
    echo "Use only letters, numbers, dot, underscore, and dash." >&2
    exit 2
fi

if [ "$run_build" = "1" ]; then
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "idf.py not found. Source ESP-IDF export.sh first." >&2
        exit 127
    fi
    idf.py build
fi

required_files=(
    "${build_dir}/bootloader/bootloader.bin"
    "${build_dir}/partition_table/partition-table.bin"
    "${build_dir}/ota_data_initial.bin"
    "${build_dir}/esp32_kvm.bin"
    "tools/host/setup-linux-serial-console.sh"
    "README.md"
)

for file in "${required_files[@]}"; do
    if [ ! -s "$file" ]; then
        echo "missing release artifact: $file" >&2
        exit 1
    fi
done

bundle_dir="${dist_dir}/esp32-kvm-${version}"
rm -rf "$bundle_dir"
mkdir -p "$bundle_dir"

cp "${build_dir}/bootloader/bootloader.bin" "$bundle_dir/"
cp "${build_dir}/partition_table/partition-table.bin" "$bundle_dir/"
cp "${build_dir}/ota_data_initial.bin" "$bundle_dir/"
cp "${build_dir}/esp32_kvm.bin" "$bundle_dir/"
cp tools/host/setup-linux-serial-console.sh "$bundle_dir/"
cp README.md "$bundle_dir/"

setup_url="${raw_base_url%/}/${version}/tools/host/setup-linux-serial-console.sh"
cat >"$bundle_dir/INSTALL.md" <<EOF
# ESP32-KVM ${version}

## Host Setup

Run this once on the Linux server connected to ESP32-KVM:

\`\`\`bash
curl -fsSL ${setup_url} | sudo sh
\`\`\`

If auto-detection fails:

\`\`\`bash
curl -fsSL ${setup_url} | sudo sh -s -- --device /dev/ttyACM<N>
\`\`\`

To remove host setup:

\`\`\`bash
curl -fsSL ${setup_url} | sudo sh -s -- --uninstall
\`\`\`
EOF

cat >"$bundle_dir/manifest.txt" <<EOF
ESP32-KVM release ${version}

Flash offsets:
  bootloader.bin          0x0
  partition-table.bin     0x8000
  ota_data_initial.bin    0xd000
  esp32_kvm.bin           0x20000

Host setup:
  setup-linux-serial-console.sh
EOF

(
    cd "$bundle_dir"
    sha256sum bootloader.bin partition-table.bin ota_data_initial.bin esp32_kvm.bin setup-linux-serial-console.sh README.md INSTALL.md manifest.txt >SHA256SUMS
)

tarball="${bundle_dir}.tar.gz"
tar -C "$dist_dir" -czf "$tarball" "esp32-kvm-${version}"

echo "Release bundle: $bundle_dir"
echo "Release archive: $tarball"
