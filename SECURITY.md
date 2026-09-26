# Security

Only the current release line receives fixes. Media decoding and model loading
use native third-party libraries. Do not process untrusted files as root or
mount unrelated personal directories into a container.

For confidential recordings, pre-download the model, use `--local-files-only`
and disable container networking with `--network none`. Mount sources and models
read-only and use a separate writable output directory. JSON records source
paths: review it before sharing. Media protocols are limited to local files;
model downloads use HTTPS with SHA-256 verification. Locally supplied GGML files
are trusted inputs and do not have catalog integrity guarantees.

Report vulnerabilities with GitHub private vulnerability reporting when enabled
by the repository owner. Until a public repository/contact is configured, do not
post exploit details or secrets in a public issue; contact the owner privately.

Pull requests must run on GitHub-hosted runners without publication credentials.
Do not attach a personal GPU runner to workflows triggered by untrusted PRs.
Never use `pull_request_target` to execute contributor code. Release publication
is intentionally not automated in this repository yet.
