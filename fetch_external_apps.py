import os
import subprocess
import configparser
import shutil
import sys
import tuiro
import argparse

def parse_args():
    parser = argparse.ArgumentParser(description="Fetch external apps and optionally enable CI mode for TUI output.")
    parser.add_argument('--ci', action='store_true', help='Enable CI mode for TUI')
    return parser.parse_args()

args = parse_args()
tui = tuiro.TUI(ci_mode=args.ci)

APPS_DIR = os.path.join(os.path.dirname(__file__), 'Apps')

# Helper to parse fetch.properties
def parse_properties(path):
    config = configparser.ConfigParser()
    try:
        with open(path, 'r') as f:
            # ConfigParser needs a section header, so fake one
            file_content = '[DEFAULT]\n' + f.read()
        config.read_string(file_content)
        return config['DEFAULT']
    except Exception as e:
        tui.error(f"Failed to parse {path}: {e}")
        return None

def validate_properties(props, app_name):
    if not props:
        return False
    repo_url = props.get('repo_url')
    if not repo_url:
        tui.error(f"No repo_url specified for {app_name}")
        return False
    # Basic URL validation
    if not repo_url.startswith(('http://', 'https://', 'git@')):
        tui.warning(f"repo_url for {app_name} may not be valid: {repo_url}")
    return True

def fetch_and_build_app(app_dir):
    prop_path = os.path.join(app_dir, 'fetch.properties')
    if not os.path.exists(prop_path):
        return
    app_name = os.path.basename(app_dir)
    tui.step(f"Processing {app_name}")
    props = parse_properties(prop_path)
    if not validate_properties(props, app_name):
        return
    repo_url = props.get('repo_url')
    app_name = props.get('app_name', app_name)
    subdir = props.get('subdir', None)
    build_script = props.get('build_script', None)
    output_dir = props.get('output_dir', 'build')  # Default to 'build' if not specified
    branch = props.get('branch', 'main')  # Default branch
    commit = props.get('commit', None)  # Optional commit hash
    skip_build = props.get('skip_build', 'false').lower() == 'true'
    dest_dir = os.path.join(app_dir, 'fetched')
    try:
        if os.path.exists(dest_dir):
            shutil.rmtree(dest_dir)
        clone_args = ['git', 'clone']
        if not commit:
            clone_args.extend(['--depth', '1'])
        clone_args.extend(['--branch', branch, repo_url, dest_dir])
        with tui.spinner(f"Cloning {repo_url} (branch: {branch})"):
            subprocess.run(clone_args, check=True, capture_output=True)
        if commit:
            with tui.spinner(f"Checking out commit {commit}"):
                subprocess.run(['git', 'checkout', commit], cwd=dest_dir, check=True, capture_output=True)
    except subprocess.CalledProcessError as e:
        tui.error(f"Failed to clone/checkout {repo_url} for {app_name}: {e.stderr.decode()}")
        return
    except Exception as e:
        tui.error(f"Error during cloning for {app_name}: {e}")
        return
    work_dir = dest_dir
    if subdir:
        work_dir = os.path.join(dest_dir, subdir)
        if not os.path.exists(work_dir):
            tui.error(f"Subdir {subdir} not found in {dest_dir} for {app_name}")
            return
    if not skip_build:
        try:
            if build_script:
                with tui.spinner(f"Running build script for {app_name}"):
                    subprocess.run(build_script, shell=True, cwd=work_dir, check=True, capture_output=True)
            else:
                tactility_py = os.path.join(work_dir, 'tactility.py')
                if os.path.isfile(tactility_py):
                    with tui.spinner(f"Building {app_name} with tactility.py"):
                        subprocess.run([sys.executable, 'tactility.py', 'build'], cwd=work_dir, check=True, capture_output=True)
                else:
                    tui.warning(f"No build script or tactility.py found for {app_name}, skipping build step.")
        except subprocess.CalledProcessError as e:
            stderr_msg = e.stderr.decode().strip() if e.stderr else ""
            stdout_msg = e.stdout.decode().strip() if e.stdout else ""
            error_msg = f"Build failed for {app_name}"
            if stderr_msg:
                error_msg += f" (stderr): {stderr_msg}"
            if stdout_msg:
                error_msg += f" (stdout): {stdout_msg}"
            tui.error(error_msg)
            return
        except Exception as e:
            tui.error(f"Error during build for {app_name}: {e}")
            return
    # Copy .app to build/ so that everything else picks it up as if it was built in place
    fetched_build_dir = os.path.join(work_dir, output_dir)
    if os.path.exists(fetched_build_dir):
        app_build_dir = os.path.join(app_dir, 'build')
        os.makedirs(app_build_dir, exist_ok=True)
        copied = False
        for file in os.listdir(fetched_build_dir):
            if file.endswith('.app'):
                shutil.copy2(os.path.join(fetched_build_dir, file), os.path.join(app_build_dir, file))
                copied = True
        if copied:
            tui.info(f"Copied .app files from {fetched_build_dir} to {app_build_dir} for {app_name}.")
        else:
            tui.warning(f"No .app files found in {fetched_build_dir} for {app_name}.")
    else:
        tui.warning(f"Build directory {fetched_build_dir} not found for {app_name}.")
    tui.success(f"Successfully processed {app_name}.")

def main():
    tui.section("Fetching External Apps")
    apps = [app for app in os.listdir(APPS_DIR) if os.path.isdir(os.path.join(APPS_DIR, app))]
    if not apps:
        tui.info("No apps found to process.")
        return
    for app in apps:
        app_dir = os.path.join(APPS_DIR, app)
        fetch_and_build_app(app_dir)

if __name__ == '__main__':
    main()
