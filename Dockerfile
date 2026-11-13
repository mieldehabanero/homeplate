FROM python:3.13-slim

ARG GIT_VERSION=2.56.1

RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential ca-certificates curl \
        libcurl4-openssl-dev libexpat1-dev libssl-dev unzip xz-utils zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Relative worktrees require Git >= 2.48; Debian's packaged 2.47 cannot read
# this repository layout, which would leave VERSION empty in firmware builds.
RUN curl -fsSL "https://www.kernel.org/pub/software/scm/git/git-${GIT_VERSION}.tar.xz" -o /tmp/git.tar.xz \
    && tar -xJf /tmp/git.tar.xz -C /tmp \
    && make -C "/tmp/git-${GIT_VERSION}" -j8 prefix=/usr/local \
        NO_GETTEXT=YesPlease NO_TCLTK=YesPlease NO_PERL=YesPlease NO_RUST=YesPlease install \
    && rm -rf /tmp/git.tar.xz "/tmp/git-${GIT_VERSION}"

RUN pip install --no-cache-dir platformio==6.1.19

CMD ["pio", "test", "-e", "native"]
