# syntax=docker/dockerfile:1

############################################################
# Builder: resolve and install Python dependencies in isolation
# from the final runtime image.
############################################################
FROM python:3.12-slim-bookworm AS builder

WORKDIR /app

COPY app/requirements.txt .

# Patch the packaging toolchain before it is used to build/install anything
# (setuptools <78.1.1 / wheel <0.46.2 carry known HIGH CVEs).
RUN pip install --no-cache-dir --upgrade pip "setuptools>=78.1.1" "wheel>=0.46.2" \
    && pip install --no-cache-dir --prefix=/install -r requirements.txt

############################################################
# Runtime: minimal image containing only what's needed to run the API.
############################################################
FROM python:3.12-slim-bookworm

WORKDIR /app

# Apply current Debian security patches (e.g. libpcre2-8-0, openssl/libssl)
RUN apt-get update \
    && apt-get upgrade -y \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /install /usr/local

# Only the application source the service needs at runtime — never .venv,
# logs, the SQLite db, or local scanner tooling (see .dockerignore).
COPY app/main.py ./

# uvicorn/fastapi don't need pip/setuptools/wheel at runtime. Removing them
# drops their bundled vendored packages (e.g. pip's vendored setuptools,
# urllib3, msgpack) from the shipped image entirely, instead of just
# re-pinning versions that would still be flagged as present.
RUN find /usr/local/lib/python3.12/site-packages -maxdepth 1 \
    \( -iname 'pip' -o -iname 'pip-*' -o -iname 'setuptools' -o -iname 'setuptools-*' \
    -o -iname 'wheel' -o -iname 'wheel-*' -o -iname 'pkg_resources' \
    -o -iname '_distutils_hack' -o -iname 'distutils-precedence.pth' \) \
    -exec rm -rf {} + \
    && rm -f /usr/local/bin/pip /usr/local/bin/pip3 /usr/local/bin/pip3.12 /usr/local/bin/wheel

RUN useradd --no-create-home --shell /usr/sbin/nologin appuser \
    && chown -R appuser:appuser /app
USER appuser

EXPOSE 8000

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]