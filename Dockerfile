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

# Apply current Debian security patches, then strip every OS package the app
# does not actually use at runtime (verified via ldd against the interpreter,
# lib-dynload extensions, and pydantic_core: only libc/libm/libssl/libcrypto/
# libsqlite3/libz/libffi/libbz2/liblzma/libgdbm/libdb/libuuid/libcrypt remain
# NEEDED). This removes perl, the NIS/Kerberos/RPC stack, mount/e2fsprogs
# tools, curses/readline, PAM, SELinux policy-management libs, systemd/udev
# client libs, and the package manager itself — eliminating the OS CVEs that
# have no upstream fix (e.g. in bsdutils/util-linux, libncursesw6, perl-base)
# by removing the vulnerable package rather than suppressing the finding.
# useradd must run before adduser/login/passwd are removed below.
RUN apt-get update \
    && apt-get upgrade -y \
    && useradd --no-create-home --shell /usr/sbin/nologin appuser \
    && apt-get remove -y --allow-remove-essential \
    util-linux util-linux-extra mount bsdutils e2fsprogs libext2fs2 libss2 logsave libcom-err2 \
    libblkid1 libmount1 libsmartcols1 \
    libkrb5-3 libgssapi-krb5-2 libk5crypto3 libkrb5support0 libkeyutils1 libnsl2 libtirpc3 libtirpc-common \
    libncursesw6 ncurses-base ncurses-bin libreadline8 readline-common \
    libsemanage2 libsemanage-common libsepol2 libcap-ng0 libcap2 \
    libudev1 libsystemd0 perl-base adduser login passwd \
    libpam-modules libpam-modules-bin libpam-runtime libpam0g libaudit1 libaudit-common \
    libgcrypt20 libgnutls30 libgpg-error0 libhogweed6 liblz4-1 libnettle8 \
    libp11-kit0 libseccomp2 libstdc++6 libtasn1-6 libxxhash0 \
    apt libapt-pkg6.0 gpgv debian-archive-keyring \
    && rm -rf /var/lib/apt/lists/* /var/cache/apt/* \
    && chown -R appuser:appuser /app

USER appuser

EXPOSE 8000

CMD ["uvicorn", "main:app", "--host", "0.0.0.0", "--port", "8000"]