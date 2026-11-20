# TweetNaCl, vendored

`tweetnacl.c` and `tweetnacl.h`, the single-file NaCl implementation by Daniel
J. Bernstein, Bernard van Gastel, Wesley Janssen, Tanja Lange, Peter Schwabe
and Sjaak Smetsers.

- Upstream: <https://tweetnacl.cr.yp.to/> (also `github.com/dominictarr/tweetnacl`)
- Version: 20140427, the release the authors published as final
- Licence: **public domain**, as the authors state on that page and in the
  accompanying paper ("TweetNaCl: a crypto library in 100 tweets"). There is
  no licence file to carry, which is why this note exists in place of one.

## What it is used for

The SSH client needs four primitives and nothing else:

- X25519 (`crypto_scalarmult`) for the key exchange
- Ed25519 (`crypto_sign_open`) to check a server's host key signature, and
  `crypto_sign` to prove ownership of ours
- SHA-512, which Ed25519 uses internally

ChaCha20, Poly1305 and SHA-256 are **not** taken from here: they are in
`src/core/sshcrypto.c`, written against RFC 8439 and RFC 6234 so that the
exact nonce and counter conventions OpenSSH uses could be matched and tested
against the RFC vectors.

## Modifications

None. The files are upstream's, byte for byte.

`randombytes()` is declared by TweetNaCl but not defined here. It is defined in
`src/core/sshcrypto.c`, where it forwards to `pim_ssh_random` - weak in the core
library, reading `/dev/urandom` for host builds, and overridden in
`src/firmware/wifi.c` by the RP2350's hardware random number generator. Nothing
in this project calls TweetNaCl's key generation, so that path exists to make
the link succeed and to be loud rather than silently weak if it is ever
reached.
