# Tactility Apps

This project contains various official Tactility apps. Some are usable day-to-day while others are simply feature showcases.

## Adding Apps

### Internal Apps

To add a new app developed within this repository:

1. Create a new directory under `Apps/`.
2. Follow the standard Tactility app structure.
3. Add the app name to the CI build matrix in `.github/workflows/main.yml`.

### External Apps

To integrate an external app from a separate repository:

1. Create a new directory under `Apps/`.
2. Add a `fetch.properties` file with the app's configuration. It is *highly* recommended to lock the app to a specific commit for security reasons.
3. The CI system will automatically fetch, build, and include the app.

See [Documentation/external-apps.md](Documentation/external-apps.md) for detailed instructions.

## License

The internal [Apps](./Apps) are licensed as [GPL v3](Documentation/license-apps.md) while the [Libraries](./Libraries) have their own licenses. External apps integrated via `fetch.properties` are licensed under their respective source repository licenses.

Read more about licensing in [LICENSE.md](LICENSE.md).
