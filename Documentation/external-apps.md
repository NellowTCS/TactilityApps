# Fetching External Apps

How to integrate external apps into the TactilityApps repository when contributors prefer to maintain their own repositories.

## Overview

The external app fetching system allows community contributors to add their apps to the official app hub by providing a simple configuration file. The system automatically fetches the app's source code from a Git repository, builds it, and integrates it into the standard build and release process.

## Licensing

External apps are licensed under their respective licenses as specified in their source repositories. They are not automatically covered by the GPL v3 license that applies to the internal apps in this repository. Contributors are responsible for ensuring their app's license is compatible with distribution through the Tactility app hub.

When adding an external app, verify the license in the source repository and ensure it allows redistribution. The app's license information will be included in its manifest and displayed to users.

## Configuration with `fetch.properties`

The `fetch.properties` file contains key-value pairs that define how to fetch and build the external app. Place this file in your app's directory (e.g., `Apps/MyApp/fetch.properties`).

### Required Properties

- `app_name`: The display name of the app.
- `author`: The name of the app's author or maintainer.
- `repo_url`: The URL of the Git repository containing the app's source code. Supports HTTPS and SSH URLs.

### Optional Properties

- `branch`: The Git branch to clone from. Defaults to `main`.
- `commit`: A specific Git commit hash to check out to. If specified, overrides the branch and performs a full clone (not shallow).
- `subdir`: A subdirectory within the repository to use as the working directory for building.
- `build_script`: A custom build command to run instead of the default `python tactility.py build`. This is executed as a shell command.
- `output_dir`: The directory within the cloned repository where built `.app` files are located. Defaults to `build`.
- `skip_build`: Set to `true` to skip the build step entirely. Defaults to `false`.

## Example fetch.properties

```properties
app_name = App Name
author = Author
repo_url = https://github.com/author/app-name.git
branch = develop
commit = abc123def456
subdir = tactility-src
build_script = cmake .. && make
output_dir = dist
```

## Error Handling

- If fetching or building fails, the script logs an error and skips that app.
- Build failures prevent `.app` files from being copied, ensuring only successfully built apps are included.
- The system is designed to be resilient, failures in one external app do not affect others.
