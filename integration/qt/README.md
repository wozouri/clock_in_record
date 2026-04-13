# Qt License Integration

This folder contains a minimal reusable Qt checker for `License_Public`.

Files:

- `LicenseCheck.h/.cpp`: reads the `license_public.ini` next to the executable and calls `/api/client/activate`
- `license_public.ini.example`: sample config file

Current model:

- store `server_base_url` and `license_key` in `license_public.ini`
- send an activation request during app startup
- allow the app to continue only after an accepted response

Default ini format:

```ini
[license_public]
server_base_url=https://localhost:7443
license_key=YOUR_LICENSE_KEY
```

By default, `LicenseCheck` resolves this file from `QCoreApplication::applicationDirPath()`, so it follows the executable location instead of the current working directory.
