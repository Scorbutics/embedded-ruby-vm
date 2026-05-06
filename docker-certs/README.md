# docker-certs/

Placeholder directory for custom CA certificates used during Docker builds.

## What goes here

When `INSTALL_CUSTOM_CERTS=true` (set via `docker-compose.yml` or an override),
the Docker build copies the contents of this directory into the image and runs
`install_certs.sh` to register them as trusted root CAs.

To install custom certs, populate this directory with:

- `install_certs.sh` — script invoked by the Dockerfile to process the certs
- Your certificate files (`.crt`, `.pem`, etc.)

See [../README.docker.md](../README.docker.md) for the full setup flow.

## Why this README exists

The Dockerfile unconditionally `COPY`s this directory into the build context,
so the directory must exist on disk even when no custom certs are configured.
The contents (other than this README) are gitignored — see the project
`.gitignore` for the exception rule that keeps this file tracked.

The cert-install step skips this README via `grep -v README`, so leaving the
directory otherwise empty results in a no-op cert step.
