FROM archlinux:latest

# update and install base-devel and common tools
RUN pacman -Syu --noconfirm && \
    pacman -S --noconfirm base-devel sudo git which less && \
    pacman -Scc --noconfirm

# create a non-root user similar to Codespaces' default
RUN useradd -m -G wheel -s /bin/bash codespace && \
    echo "codespace ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/codespace && \
    chmod 0440 /etc/sudoers.d/codespace

USER codespace
WORKDIR /workspaces/${GITHUB_REPOSITORY}
