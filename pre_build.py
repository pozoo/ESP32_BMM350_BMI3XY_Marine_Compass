Import("env")
import subprocess
import sys
import os

def generate_html_header_callback(*args, **kwargs):
    """Generate PROGMEM header from HTML files before compilation"""
    print("=" * 60)
    print("Pre-build: Generating HTML header from html/ directory...")
    print("=" * 60)
    
    # Get the project directory
    project_dir = env.get("PROJECT_DIR")
    script_path = os.path.join(project_dir, "generate_html_header.py")
    
    # Run the generator script
    try:
        result = subprocess.run(
            [sys.executable, script_path],
            cwd=project_dir,
            capture_output=True,
            text=True,
            check=True
        )
        print(result.stdout)
        if result.stderr:
            print(result.stderr)
        print("=" * 60)
        print("HTML header generation complete!")
        print("=" * 60)
    except subprocess.CalledProcessError as e:
        print("ERROR: Failed to generate HTML header!")
        print(e.stdout)
        print(e.stderr)
        sys.exit(1)

# Execute immediately when script is loaded (before any compilation)
generate_html_header_callback()

