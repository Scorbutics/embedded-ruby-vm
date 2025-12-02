# Docker Setup - What to Commit to Git

This guide explains which Docker files should be committed to version control and which should remain local.

## ✅ Files to Commit

These files should be committed to git as they provide a generic, portable development environment:

- **`Dockerfile`** - Base development image (generic, works for everyone)
- **`Dockerfile.android`** - Android NDK image (generic)
- **`docker-compose.yml`** - Base Docker Compose configuration
- **`docker-compose.override.yml.example`** - Template for local overrides
- **`.dockerignore`** - Docker build optimization
- **`README.docker.md`** - Complete documentation
- **`docker-dev.sh`** - Helper script for Linux/Mac
- **`docker-dev.bat`** - Helper script for Windows
- **`DOCKER_SETUP.md`** - This file

## ❌ Files to NOT Commit (Gitignored)

These files contain local/corporate-specific configuration:

- **`docker-compose.override.yml`** - Local overrides (created from .example)
- **`docker-output/`** - Exported build artifacts
- **`docker-certs/`** - Corporate/custom certificates

## 📋 Git Commit Checklist

Before committing Docker configuration:

```bash
# 1. Verify .gitignore contains:
docker-compose.override.yml
/docker-output/
/docker-certs/

# 2. Check what you're about to commit:
git status

# 3. Ensure docker-compose.yml has no hardcoded paths:
grep -E "additional_contexts|certs:" docker-compose.yml

# 4. Verify INSTALL_CUSTOM_CERTS defaults to false:
grep "INSTALL_CUSTOM_CERTS" docker-compose.yml Dockerfile

# 5. Test that it works without overrides:
mv docker-compose.override.yml docker-compose.override.yml.backup  # if it exists
docker-compose build
# Should succeed without errors
mv docker-compose.override.yml.backup docker-compose.override.yml  # restore if needed
```

## 🎯 Corporate Environment Setup (Not Committed)

For users in corporate environments who need custom certificates:

### 1. Setup Certificates Directory

```bash
mkdir -p docker-certs
# Copy your certs and install_certs.sh to docker-certs/
```

### 2. Create Override File

```bash
cp docker-compose.override.yml.example docker-compose.override.yml
```

### 3. Edit Override File

Edit `docker-compose.override.yml` to enable certificates:

```yaml
version: '3.8'

services:
  dev:
    build:
      additional_contexts:
        certs: ./docker-certs
      args:
        - INSTALL_CUSTOM_CERTS=true
```

### 4. Build and Use

```bash
docker-compose build
./docker-dev.sh setup  # or docker-dev.bat setup on Windows
```

## 🔍 Why This Approach?

### Benefits

1. **Portable** - External contributors can use Docker setup without any modifications
2. **Flexible** - Corporate users can add certificates via local override
3. **Secure** - Company certificates never get committed to git
4. **Documented** - Clear separation between generic and environment-specific config
5. **Maintainable** - Updates to base config don't conflict with local overrides

### Docker Compose Override Mechanism

Docker Compose automatically merges configurations in this order:
1. `docker-compose.yml` (base configuration - committed)
2. `docker-compose.override.yml` (local overrides - gitignored)

This means:
- You commit the base configuration
- Each developer maintains their own `docker-compose.override.yml`
- No conflicts, no leaked secrets, no hardcoded paths

## 📖 For New Team Members

### Standard Setup (No Corporate Certs)

```bash
# Clone the repo
git clone <repo-url>
cd embedded-ruby-vm

# Just run the setup script
./docker-dev.sh setup     # Linux/Mac
# OR
docker-dev.bat setup      # Windows
```

### Corporate Setup (With Certs)

```bash
# Clone the repo
git clone <repo-url>
cd embedded-ruby-vm

# Setup certificates
mkdir -p docker-certs
# Copy certs from corporate share/wiki/etc.

# Create override config
cp docker-compose.override.yml.example docker-compose.override.yml
# Edit docker-compose.override.yml (follow instructions in file)

# Run setup
./docker-dev.sh setup     # Linux/Mac
# OR
docker-dev.bat setup      # Windows
```

## 🚀 Ready to Commit?

Run this final check:

```bash
# See what Docker files are staged
git diff --cached --name-only | grep -E "docker|Docker"

# Ensure these are NOT staged:
git status | grep -E "docker-compose.override.yml|docker-output|docker-certs"
# Should show "nothing to commit" or be listed under "Untracked files"

# Commit the Docker setup
git add Dockerfile* docker-compose.yml docker-compose.override.yml.example \
        .dockerignore README.docker.md docker-dev.sh docker-dev.bat DOCKER_SETUP.md

git commit -m "feat: add Docker development environment

- Add Dockerfile with optional corporate certificate support
- Add docker-compose.yml with named volumes (better Windows performance)
- Add helper scripts for common tasks (docker-dev.sh/bat)
- Add comprehensive documentation (README.docker.md)
- Support corporate environments via docker-compose.override.yml"
```

## 📚 Additional Resources

- **Full Documentation**: See `README.docker.md`
- **Helper Commands**: Run `./docker-dev.sh help` or `docker-dev.bat help`
- **Override Template**: See `docker-compose.override.yml.example`
