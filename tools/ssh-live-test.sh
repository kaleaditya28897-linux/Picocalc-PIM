#!/usr/bin/env bash
# Runs the SSH client against a throwaway OpenSSH server.
#   tools/ssh-live-test.sh [build-dir]
# Starts an sshd on a spare port with its own host key, its own authorized_keys
# and its own log, runs pim_ssh_live_tests against it, and takes it all down
# again. Nothing touches the real sshd, the real ~/.ssh, or the system log.
set -euo pipefail

build=${1:-build/host-test}
test_binary="${build}/pim_ssh_live_tests"
sshd=$(command -v sshd || echo /usr/sbin/sshd)

[[ -x "${test_binary}" ]] || {
    echo "build it first: cmake --preset host-debug && cmake --build --preset host-debug" >&2
    exit 2
}
[[ -x "${sshd}" ]] || { echo "no sshd to test against; skipping" >&2; exit 0; }

work=$(mktemp -d)
cleanup() {
    [[ -f "${work}/sshd.pid" ]] && kill "$(cat "${work}/sshd.pid")" 2>/dev/null || true
    rm -rf "${work}"
}
trap cleanup EXIT

# A free port, asked for rather than guessed.
port=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')

ssh-keygen -q -t ed25519 -f "${work}/hostkey" -N '' -C picodeck-test-host
ssh-keygen -q -t ed25519 -f "${work}/clientkey" -N '' -C picodeck-test-client
cp "${work}/clientkey.pub" "${work}/authorized_keys"
chmod 600 "${work}/authorized_keys" "${work}/hostkey" "${work}/clientkey"

cat > "${work}/sshd_config" <<EOF
Port ${port}
ListenAddress 127.0.0.1
HostKey ${work}/hostkey
AuthorizedKeysFile ${work}/authorized_keys
PidFile ${work}/sshd.pid
StrictModes no
UsePAM no
PasswordAuthentication no
KbdInteractiveAuthentication no
PubkeyAuthentication yes
PermitRootLogin no
EOF

"${sshd}" -f "${work}/sshd_config" -o LogLevel=INFO -E "${work}/sshd.log"
for _ in $(seq 1 30); do
    if python3 -c "
import socket,sys
s=socket.socket(); s.settimeout(0.3)
sys.exit(0 if s.connect_ex(('127.0.0.1',${port}))==0 else 1)
"; then break; fi
    sleep 0.2
done

fingerprint=$(ssh-keygen -lf "${work}/hostkey.pub" | awk '{print $2}')
echo "test sshd on 127.0.0.1:${port}, host key ${fingerprint}"

PIM_SSH_TEST_PORT="${port}" \
PIM_SSH_TEST_USER="${USER}" \
PIM_SSH_TEST_KEY="${work}/clientkey" \
PIM_SSH_TEST_FINGERPRINT="${fingerprint}" \
    "${test_binary}"
status=$?

if [[ ${status} -ne 0 ]]; then
    echo "--- sshd log ---" >&2
    tail -30 "${work}/sshd.log" >&2 || true
fi
exit ${status}
