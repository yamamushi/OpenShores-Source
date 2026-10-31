#!/usr/bin/env bash
# Prepare a self-contained Apache image context from already-built web tools.
set -euo pipefail

if (($# != 1)); then
    echo "usage: $0 CONFIG" >&2
    exit 2
fi

config=$1
case "$config" in
    debug|release|relwithdebinfo) ;;
    *) echo "error: unsupported build configuration: $config" >&2; exit 2;;
esac

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
web_dir="$repo_root/deploy/server/web"
artifacts="$repo_root/build/linux-$config/bin"
libraries="$repo_root/build/linux-$config/lib"
context="$repo_root/build/server/web-context-$config"

web_files=(
    Dockerfile openshores.conf
    index.php server-status.php server-status.js playerson.php galactic.php styles.css register.html reset.html emailchange.html passchange.html
    health.cgi entrypoint.sh generate-standings.sh generate-avatars.sh report-scheduler.sh
)
for image in "$web_dir"/images/*; do
    [[ -f "$image" ]] || continue
    web_files+=("images/${image##*/}")
done
programs=(AuServerStatus AuAcctNew AuAcctEmail AuAcctPassword AuEmpireReport)

# Check every input before updating an existing context.
for filename in "${web_files[@]}"; do
    if [[ ! -f "$web_dir/$filename" || ! -r "$web_dir/$filename" ]]; then
        echo "error: missing or unreadable web deployment file: $web_dir/$filename" >&2
        exit 1
    fi
done
for program in "${programs[@]}"; do
    if [[ ! -f "$artifacts/$program" || ! -r "$artifacts/$program" ]]; then
        echo "error: missing or unreadable web executable: $artifacts/$program" >&2
        echo "Compile the web-tools target for $config before staging." >&2
        exit 1
    fi
done

for library in "$libraries"/*.so*; do
    if [[ ! -f "$library" || ! -r "$library" ]]; then
        echo "error: missing or unreadable reporting library: $library" >&2
        exit 1
    fi
done

install -d "$context/bin" "$context/lib" "$context/images"
# Older staged contexts may still contain the retired build-context allowlist.
rm -f -- "$context/Dockerfile.dockerignore"
for filename in "${web_files[@]}"; do
    # macOS install has no GNU -D option; create parents explicitly.
    install -d "$(dirname "$context/$filename")"
    install -m 0644 "$web_dir/$filename" "$context/$filename"
done
install -m 0755 "$web_dir/health.cgi" "$web_dir/entrypoint.sh" \
    "$web_dir/generate-standings.sh" "$web_dir/generate-avatars.sh" \
    "$web_dir/report-scheduler.sh" "$context/"
for program in "${programs[@]}"; do
    install -m 0755 "$artifacts/$program" "$context/bin/$program"
done

cp -a "$libraries"/*.so* "$context/lib/"

printf 'Web image context: %s\n' "$context"
