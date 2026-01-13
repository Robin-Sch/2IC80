#!/bin/bash

VERSION=1.21.1
TARGET_VERSION="go$VERSION"

# Improved version check
if command -v go &> /dev/null; then
    INSTALLED_VERSION=$(go version 2>/dev/null)
    if [ $? -eq 0 ]; then
        INSTALLED_VERSION=$(echo $INSTALLED_VERSION | awk '{print $3}')
        echo "Go is already installed: $INSTALLED_VERSION"
        
        if [ "$INSTALLED_VERSION" = "$TARGET_VERSION" ]; then
            echo "The installed version of Go matches the target version ($TARGET_VERSION). No action is needed."
            exit 0
        else
            echo "The installed version of Go ($INSTALLED_VERSION) does not match the target version ($TARGET_VERSION)."
            echo "Proceeding with installation of Go $VERSION..."
        fi
    else
        echo "Go command exists but version check failed. Proceeding with installation..."
    fi
else
    echo "Go is not installed. Proceeding with installation..."
fi

echo "Downloading Go $VERSION"
wget https://dl.google.com/go/go$VERSION.linux-armv6l.tar.gz
if [ $? -ne 0 ]; then
    echo "Failed to download Go. Please check your internet connection and try again."
    exit 1
fi
echo "Downloading Go $VERSION completed"

echo "Extracting..."
sudo tar -C /usr/local -xzf go$VERSION.linux-armv6l.tar.gz
if [ $? -ne 0 ]; then
    echo "Failed to extract Go. Please check if you have sufficient permissions."
    exit 1
fi
echo "Extraction complete"

# Check if PATH already contains Go binary path
if ! grep -q "/usr/local/go/bin" "$HOME/.bashrc"; then
    echo 'export PATH=$PATH:/usr/local/go/bin' >> "$HOME/.bashrc"
fi

# Check if GOPATH is already set
if ! grep -q "GOPATH=" "$HOME/.bashrc"; then
    echo 'export GOPATH=$HOME/go' >> "$HOME/.bashrc"
fi

# Source the updated environment
source "$HOME/.bashrc"

# Verify installation
if command -v go &> /dev/null; then
    NEW_VERSION=$(go version | awk '{print $3}')
    echo "Go $VERSION has been installed successfully. Version: $NEW_VERSION"
else
    echo "Installation completed but Go command is not available. Please restart your terminal."
fi

# Cleanup
rm -f go$VERSION.linux-armv6l.tar.gz