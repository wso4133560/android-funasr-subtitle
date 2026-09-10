# Models

Run `pwsh -File scripts/download-models.ps1` to place the verified model files here. The Android
application does not read this directory directly: install the APK, then use its two import buttons
to copy the files into the application's private storage.

The model binaries and temporary `.part` files are ignored by Git.
