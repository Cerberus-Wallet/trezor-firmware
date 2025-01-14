#!/usr/bin/env bash

: "${FORCE_DOCKER_USE:=0}"

CORE_DIR="$(SHELL_SESSION_FILE='' && cd "$( dirname "${BASH_SOURCE[0]}" )/.." >/dev/null 2>&1 && pwd )"

DOCKER_ID=""

# Test termination trap
terminate_test() {
  if [ -n "$DOCKER_ID" ]; then docker kill $DOCKER_ID 2>/dev/null >/dev/null; fi
}

set -e
trap terminate_test EXIT

export TEST_MIN_HF=15  # No need to test hard fork 12 or lower

if [[ "$OSTYPE" != "linux-gnu" && "$OSTYPE" != "darwin"* ]]; then
  echo "Tests with native Monero app is supported only on Linux and OSX at the moment. Your OS: $OSTYPE"
  exit 0
fi

# When updating URL and sha256sum also update the URL in ci/shell.nix.
error=1
: "${CERBERUS_MONERO_TESTS_URL:=https://github.com/ph4r05/monero/releases/download/v0.18.1.1-dev-tests-u18.04-02/cerberus_tests}"
: "${CERBERUS_MONERO_TESTS_SHA256SUM:=81424cfc3965abdc24de573274bf631337b52fd25cefc895513214c613fe05c9}"
: "${CERBERUS_MONERO_TESTS_PATH:=$CORE_DIR/tests/cerberus_monero_tests}"
: "${CERBERUS_MONERO_TESTS_LOG:=$CORE_DIR/tests/cerberus_monero_tests.log}"
: "${CERBERUS_MONERO_TESTS_CHAIN:=$CORE_DIR/tests/cerberus_monero_tests.chain}"

if [[ ! -f "$CERBERUS_MONERO_TESTS_PATH" ]]; then
  echo "Downloading Cerberus monero tests binary ($CERBERUS_MONERO_TESTS_SHA256SUM) to ${CERBERUS_MONERO_TESTS_PATH}"
  wget -O "$CERBERUS_MONERO_TESTS_PATH" "$CERBERUS_MONERO_TESTS_URL" \
    && chmod +x "$CERBERUS_MONERO_TESTS_PATH" \
    && echo "${CERBERUS_MONERO_TESTS_SHA256SUM}  ${CERBERUS_MONERO_TESTS_PATH}" | shasum -a 256 -c
else
  echo "Cerberus monero binary already present at $CERBERUS_MONERO_TESTS_PATH - not downloading again."
fi

echo "Running tests"
TIME_TESTS_START=$SECONDS
if [[ "$OSTYPE" == "linux-gnu" && "$FORCE_DOCKER_USE" != 1 ]]; then
  echo "Note: use --heavy-tests with real device (and CERBERUS_PATH) env var"
  TEST_MAX_HF=15 TEST_MIN_HF=15 "$CERBERUS_MONERO_TESTS_PATH" --fix-chain --chain-path=$CERBERUS_MONERO_TESTS_CHAIN $@ 2>&1 > "$CERBERUS_MONERO_TESTS_LOG"
  error=$?

elif [[ "$OSTYPE" == "darwin"* || "$FORCE_DOCKER_USE" == 1 ]]; then
  DOCKER_ID=$(docker run -idt --mount type=bind,src="$CORE_DIR",dst="$CORE_DIR" -w "$CORE_DIR" --network=host ubuntu:18.04)
  docker exec $DOCKER_ID apt-get update -qq 2>/dev/null >/dev/null
  docker exec $DOCKER_ID apt-get install --no-install-recommends --no-upgrade -qq net-tools socat 2>/dev/null >/dev/null
  docker exec -d $DOCKER_ID socat UDP-LISTEN:21424,reuseaddr,reuseport,fork UDP4-SENDTO:host.docker.internal:21424
  docker exec -d $DOCKER_ID socat UDP-LISTEN:21425,reuseaddr,reuseport,fork UDP4-SENDTO:host.docker.internal:21425
  docker exec -e TEST_MAX_HF=15 -e TEST_MIN_HF=15 $DOCKER_ID "$CERBERUS_MONERO_TESTS_PATH" 2>&1 > "$CERBERUS_MONERO_TESTS_LOG"
  error=$?

else
  echo "Unsupported OS: $OSTYPE"
  exit 1
fi


TIME_TESTS_ELAPSED=$((SECONDS-TIME_TESTS_START))

if ((error != 0)); then
  RESULT=FAIL
else
  RESULT=PASS
fi

echo "[$RESULT] Monero test in $TIME_TESTS_ELAPSED sec. "
cat "$CERBERUS_MONERO_TESTS_LOG" | grep -v DEBUG | egrep '#TEST#|tests.core\b' | tail -n 50
exit $error
