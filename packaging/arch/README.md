# Arch Linux packaging (AUR)

This folder contains a PKGBUILD for the AUR VCS package `hyprlax-git`.

## Package variants
- hyprlax-git (AUR): tracks latest `git` HEAD and builds from source.
  - PKGBUILD: `packaging/arch/hyprlax-git/PKGBUILD`
- hyprlax (future): release tarball package when official releases are tagged.

## Build locally
```bash
cd packaging/arch/hyprlax-git
makepkg -si
```

## Generate .SRCINFO
```bash
cd packaging/arch/hyprlax-git
makepkg --printsrcinfo > .SRCINFO
```

## Publish to AUR
AUR packages live in separate repos. Create a new repo on `aur.archlinux.org`:

```bash
# Install dev tools
sudo pacman -S --needed base-devel git

# Clone AUR repo (requires AUR account + SSH key)
git clone ssh://aur@aur.archlinux.org/hyprlax-git.git
cd hyprlax-git

# Copy packaging files from this repo
cp -v ../path/to/your/checkout/packaging/arch/hyprlax-git/PKGBUILD .
makepkg --printsrcinfo > .SRCINFO

# Commit and push
git add PKGBUILD .SRCINFO
git commit -m "initial import: hyprlax-git"
git push
```

## Notes
- Runtime deps: `wayland`, `mesa`.
- Build deps: `wayland-protocols`, `git`.
- The Makefile supports `PREFIX` and `DESTDIR`; packaging installs to `/usr/bin`.
- Tests are optional and require extra deps (`check`).
- Consider adding a stable (non -git) PKGBUILD once tagged releases are established.

