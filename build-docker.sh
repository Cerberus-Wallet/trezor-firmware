#!/usr/bin/env bash
set -e -o pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

############## Select the right Alpine architecture ##############

if [ -z "$ALPINE_ARCH" ]; then
  arch="$(uname -m)"
  case "$arch" in
    aarch64|arm64)
      ALPINE_ARCH="aarch64"
      ;;
    x86_64)
      ALPINE_ARCH="x86_64"
      ;;
    *)
      echo "Unsupported arch"
      exit
  esac
fi

if [ -z "$ALPINE_CHECKSUM" ]; then
  case "$ALPINE_ARCH" in
    aarch64)
      ALPINE_CHECKSUM="1be50ae27c8463d005c4de16558d239e11a88ac6b2f8721c47e660fbeead69bf"
      ;;
    x86_64)
      ALPINE_CHECKSUM="ec7ec80a96500f13c189a6125f2dbe8600ef593b87fc4670fe959dc02db727a2"
      ;;
    *)
      exit
  esac
 fi


DOCKER=${DOCKER:-docker}
CONTAINER_NAME=${CONTAINER_NAME:-cerberus-firmware-env.nix}
ALPINE_CDN=${ALPINE_CDN:-https://dl-cdn.alpinelinux.org/alpine}
ALPINE_RELEASE=${ALPINE_RELEASE:-3.15}
ALPINE_VERSION=${ALPINE_VERSION:-3.15.0}
ALPINE_TARBALL=${ALPINE_FILE:-alpine-minirootfs-$ALPINE_VERSION-$ALPINE_ARCH.tar.gz}
NIX_VERSION=${NIX_VERSION:-2.4}
CONTAINER_FS_URL=${CONTAINER_FS_URL:-"$ALPINE_CDN/v$ALPINE_RELEASE/releases/$ALPINE_ARCH/$ALPINE_TARBALL"}

############## Options parsing ##############

function help_and_die() {
  echo "Usage: $0 [options] tag"
  echo "Options:"
  echo "  --skip-bitcoinonly - do not build bitcoin-only firmwares"
  echo "  --skip-normal - do not build regular firmwares"
  echo "  --skip-core - do not build core"
  echo "  --skip-legacy - do not build legacy"
  echo "  --prodtest - build core prodtest"
  echo "  --repository path/to/repo - checkout the repository from the given path/url"
  echo "  --no-init - do not recreate docker environments"
  echo "  --models - comma-separated list of models. default: --models R,T"
  echo "  --help"
  echo
  echo "Set PRODUCTION=0 to run non-production builds."
  exit 0
}

OPT_BUILD_CORE=1
OPT_BUILD_LEGACY=1
OPT_BUILD_NORMAL=1
OPT_BUILD_BITCOINONLY=1
OPT_BUILD_PRODTEST=0
INIT=1
MODELS=(R T)

REPOSITORY="/local"

while true; do
  case "$1" in
    -h|--help)
      help_and_die
      ;;
    --skip-bitcoinonly)
      OPT_BUILD_BITCOINONLY=0
      shift
      ;;
    --skip-normal)
      OPT_BUILD_NORMAL=0
      shift
      ;;
    --skip-core)
      OPT_BUILD_CORE=0
      shift
      ;;
    --skip-legacy)
      OPT_BUILD_LEGACY=0
      shift
      ;;
    --prodtest)
      OPT_BUILD_PRODTEST=1
      shift
      ;;
    --repository)
      REPOSITORY="$2"
      shift 2
      ;;
    --no-init)
      INIT=0
      shift
      ;;
    --models)
      # take comma-separated next argument and turn it into an array
      IFS=',' read -r -a MODELS <<< "$2"
      shift 2
      ;;
    *)
      break
      ;;
  esac
done

if [ -z "$1" ]; then
  help_and_die
fi

################## Variant selection ##################

variants=()
if [ "$OPT_BUILD_NORMAL" -eq 1 ]; then
  variants+=(0)
fi
if [ "$OPT_BUILD_BITCOINONLY" -eq 1 ]; then
  variants+=(1)
fi

VARIANTS_core=()
VARIANTS_legacy=()

if [ "$OPT_BUILD_CORE" -eq 1 ]; then
  VARIANTS_core=("${variants[@]}")
fi
if [ "$OPT_BUILD_LEGACY" -eq 1 ]; then
  VARIANTS_legacy=("${variants[@]}")
fi

TAG="$1"
COMMIT_HASH="$(git rev-parse "$TAG")"
PRODUCTION=${PRODUCTION:-1}

if which wget > /dev/null ; then
  wget --no-config -nc -P ci/ "$CONTAINER_FS_URL"
else
  if ! [ -f "ci/$ALPINE_TARBALL" ]; then
    curl -L -o "ci/$ALPINE_TARBALL" "$CONTAINER_FS_URL"
  fi
fi

# check alpine checksum
if command -v sha256sum &> /dev/null ; then
    echo "${ALPINE_CHECKSUM}  ci/${ALPINE_TARBALL}" | sha256sum -c
else
    echo "${ALPINE_CHECKSUM}  ci/${ALPINE_TARBALL}" | shasum -a 256 -c
fi

tag_clean="${TAG//[^a-zA-Z0-9]/_}"
SNAPSHOT_NAME="${CONTAINER_NAME}__${tag_clean}"

mkdir -p build/core build/legacy
mkdir -p build/core-bitcoinonly build/legacy-bitcoinonly

# if not initializing, does the image exist?
if [ $INIT -eq 0 ] && ! $DOCKER image inspect $SNAPSHOT_NAME > /dev/null; then
  echo "Image $SNAPSHOT_NAME does not exist."
  exit 1
fi

GIT_CLEAN_REPO="git clean -dfx -e .venv"
SCRIPT_NAME="._setup_script"

if [ $INIT -eq 1 ]; then

  SELECTED_CONTAINER="$CONTAINER_NAME"

  echo
  echo ">>> DOCKER BUILD ALPINE_VERSION=$ALPINE_VERSION ALPINE_ARCH=$ALPINE_ARCH NIX_VERSION=$NIX_VERSION -t $CONTAINER_NAME"
  echo

  $DOCKER build \
    --network=host \
    --build-arg ALPINE_VERSION="$ALPINE_VERSION" \
    --build-arg ALPINE_ARCH="$ALPINE_ARCH" \
    --build-arg NIX_VERSION="$NIX_VERSION" \
    -t "$CONTAINER_NAME" \
    ci/

  cat <<EOF > "$SCRIPT_NAME"
    #!/bin/bash
    set -e -o pipefail

    mkdir -p /reproducible-build
    cd /reproducible-build
    git clone "$REPOSITORY" cerberus-firmware
    cd cerberus-firmware
EOF

else  # init == 0

  SELECTED_CONTAINER="$SNAPSHOT_NAME"

  cat <<EOF > "$SCRIPT_NAME"
    #!/bin/bash
    set -e -o pipefail

    cd /reproducible-build/cerberus-firmware
EOF

fi  # init

# append common part to script
cat <<EOF >> "$SCRIPT_NAME"
  $GIT_CLEAN_REPO
  git fetch origin "$COMMIT_HASH"
  git checkout "$COMMIT_HASH"
  git submodule update --init --recursive
  poetry install
  cd core/embed/rust
  cargo fetch

  echo
  echo ">>> AT COMMIT \$(git rev-parse HEAD)"
  echo
EOF

echo
echo ">>> DOCKER REFRESH $SNAPSHOT_NAME"
echo

$DOCKER run \
  --network=host \
  -t \
  -v "$PWD:/local" \
  -v "$PWD/build:/build" \
  --name "$SNAPSHOT_NAME" \
  "$SELECTED_CONTAINER" \
  /nix/var/nix/profiles/default/bin/nix-shell --run "bash /local/$SCRIPT_NAME" \
  || ($DOCKER rm "$SNAPSHOT_NAME"; exit 1)

rm $SCRIPT_NAME

echo
echo ">>> DOCKER COMMIT $SNAPSHOT_NAME"
echo

$DOCKER commit "$SNAPSHOT_NAME" "$SNAPSHOT_NAME"
$DOCKER rm "$SNAPSHOT_NAME"

# stat under macOS has slightly different cli interface
USER=$(stat -c "%u" . 2>/dev/null || stat -f "%u" .)
GROUP=$(stat -c "%g" . 2>/dev/null || stat -f "%g" .)

DIR=$(pwd)

# build core

for CERBERUS_MODEL in ${MODELS[@]}; do
  for BITCOIN_ONLY in ${VARIANTS_core[@]}; do

    DIRSUFFIX=${BITCOIN_ONLY/1/-bitcoinonly}
    DIRSUFFIX=${DIRSUFFIX/0/}
    DIRSUFFIX="-${CERBERUS_MODEL}${DIRSUFFIX}"

    SCRIPT_NAME=".build_core_${CERBERUS_MODEL}_${BITCOIN_ONLY}.sh"
    cat <<EOF > "build/$SCRIPT_NAME"
      # DO NOT MODIFY!
      # this file was generated by ${BASH_SOURCE[0]}
      # variant: core build BITCOIN_ONLY=$BITCOIN_ONLY CERBERUS_MODEL=$CERBERUS_MODEL
      set -e -o pipefail
      cd /reproducible-build/cerberus-firmware/core
      $GIT_CLEAN_REPO
      poetry run make clean vendor build_boardloader build_bootloader build_firmware
      for item in bootloader firmware; do
        poetry run ../python/tools/firmware-fingerprint.py \
                    -o build/\$item/\$item.bin.fingerprint \
                    build/\$item/\$item.bin
      done
      rm -rf /build/*
      cp -r build/* /build
      chown -R $USER:$GROUP /build
EOF

    echo
    echo ">>> DOCKER RUN core BITCOIN_ONLY=$BITCOIN_ONLY CERBERUS_MODEL=$CERBERUS_MODEL PRODUCTION=$PRODUCTION"
    echo

    $DOCKER run \
      --network=host \
      -it \
      --rm \
      -v "$DIR:/local" \
      -v "$DIR/build/core$DIRSUFFIX":/build:z \
      --env BITCOIN_ONLY="$BITCOIN_ONLY" \
      --env CERBERUS_MODEL="$CERBERUS_MODEL" \
      --env PRODUCTION="$PRODUCTION" \
      --init \
      "$SNAPSHOT_NAME" \
      /nix/var/nix/profiles/default/bin/nix-shell --run "bash /local/build/$SCRIPT_NAME"
  done
done

# build legacy

for BITCOIN_ONLY in ${VARIANTS_legacy[@]}; do

  DIRSUFFIX=${BITCOIN_ONLY/1/-bitcoinonly}
  DIRSUFFIX=${DIRSUFFIX/0/}

  SCRIPT_NAME=".build_legacy_$BITCOIN_ONLY.sh"
  cat <<EOF > "build/$SCRIPT_NAME"
    # DO NOT MODIFY!
    # this file was generated by ${BASH_SOURCE[0]}
    # variant: legacy build BITCOIN_ONLY=$BITCOIN_ONLY
    set -e -o pipefail
    cd /reproducible-build/cerberus-firmware/legacy
    $GIT_CLEAN_REPO
    ln -s /build build
    poetry run script/cibuild
    mkdir -p build/bootloader build/firmware build/intermediate_fw
    cp bootloader/bootloader.bin build/bootloader/bootloader.bin
    cp intermediate_fw/cerberus.bin build/intermediate_fw/inter.bin
    cp firmware/cerberus.bin build/firmware/firmware.bin
    cp firmware/firmware*.bin build/firmware/ || true  # ignore missing file as it will not be present in old tags
    cp firmware/cerberus.elf build/firmware/firmware.elf
    poetry run ../python/tools/firmware-fingerprint.py \
               -o build/firmware/firmware.bin.fingerprint \
               build/firmware/firmware.bin
    chown -R $USER:$GROUP /build
EOF

  echo
  echo ">>> DOCKER RUN legacy BITCOIN_ONLY=$BITCOIN_ONLY PRODUCTION=$PRODUCTION"
  echo

  $DOCKER run \
    --network=host \
    -it \
    --rm \
    -v "$DIR:/local" \
    -v "$DIR/build/legacy$DIRSUFFIX":/build:z \
    --env BITCOIN_ONLY="$BITCOIN_ONLY" \
    --env PRODUCTION="$PRODUCTION" \
    --init \
    "$SNAPSHOT_NAME" \
    /nix/var/nix/profiles/default/bin/nix-shell --run "bash /local/build/$SCRIPT_NAME"
done

if [ "$OPT_BUILD_PRODTEST" -eq "1" ]; then
  for CERBERUS_MODEL in ${MODELS[@]}; do
    DIRSUFFIX="-${CERBERUS_MODEL}-prodtest"
    SCRIPT_NAME=".build_${CERBERUS_MODEL}-prodtest.sh"
    cat <<EOF > "build/$SCRIPT_NAME"
      # DO NOT MODIFY!
      # this file was generated by ${BASH_SOURCE[0]}
      # variant: core build prodtest
      set -e -o pipefail
      cd /reproducible-build/cerberus-firmware/core
      $GIT_CLEAN_REPO
      poetry run make clean vendor build_prodtest
      poetry run ../python/tools/firmware-fingerprint.py \
                  -o build/prodtest/prodtest.bin.fingerprint \
                  build/prodtest/prodtest.bin
      rm -rf /build/*
      cp -r build/* /build
      chown -R $USER:$GROUP /build
EOF

    echo
    echo ">>> DOCKER RUN core prodtest CERBERUS_MODEL=$CERBERUS_MODEL PRODUCTION=$PRODUCTION"
    echo

    $DOCKER run \
      --network=host \
      -it \
      --rm \
      -v "$DIR:/local" \
      -v "$DIR/build/core$DIRSUFFIX":/build:z \
      --env CERBERUS_MODEL="$CERBERUS_MODEL" \
      --env PRODUCTION="$PRODUCTION" \
      --init \
      "$SNAPSHOT_NAME" \
      /nix/var/nix/profiles/default/bin/nix-shell --run "bash /local/build/$SCRIPT_NAME"
  done
fi


echo
echo "Docker image retained as $SNAPSHOT_NAME"
echo "To remove it, run:"
echo "  docker rmi $SNAPSHOT_NAME"

# all built, show fingerprints

echo
echo "Built from commit $COMMIT_HASH"
echo
echo "Fingerprints:"
for VARIANT in core legacy; do
  for MODEL in "R" "T"; do
    for DIRSUFFIX in "" "-bitcoinonly" "-prodtest"; do
      BUILD_DIR=build/${VARIANT}-${MODEL}${DIRSUFFIX}
      for file in $BUILD_DIR/*/*.fingerprint; do
        if [ -f "$file" ]; then
          origfile="${file%.fingerprint}"
          fingerprint=$(tr -d '\n' < $file)
          echo "$fingerprint $origfile"
        fi
      done
    done
  done
done
