import os
import re

project = 'cimba'

# Single source of truth: the VERSION file in the repository root
version_file = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'VERSION')
with open(version_file, 'r', encoding='utf-8') as f:
    release = f.read().strip()
if not re.fullmatch(r'\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?', release):
    raise ValueError(f"{version_file}: unexpected version string {release!r}")
version = release

copyright = 'Asbjørn M. Bonvik 2025-26'

extensions = ['breathe', 'exhale']

c_extra_keywords = []

html_theme = 'sphinx_rtd_theme'
html_logo = '../images/logo_small.png'
html_theme_options = {
    'logo_only': False,
    'display_version': False,
}
html_static_path = ['./static']
html_css_files = ['custom.css']

breathe_projects = {
    "cimba": "./xml"
}

breathe_default_project = "cimba"
breathe_projects_source = {
    "cimba": ("../include", ["cmb_process.h", "cmb_buffer.h"])
}

breathe_default_members = ('members', 'undoc-members')
breathe_domain_by_extension = {
    "h": "c",
}

breathe_implementation_filename_extensions = ['.c']

exhale_args = {
    "containmentFolder":    "./api",
    "rootFileName":         "library_root.rst",
    "doxygenStripFromPath": "..",
    "rootFileTitle":        "Cimba API Reference",
    "createTreeView":       True,
    "lexerMapping": {
        r".*\.c": "c",
        r".*\.h": "c"
    },
    "unabridgedOrphanKinds": [],
    "exhaleExecutesDoxygen": True,
    "exhaleDoxygenStdin": open("Doxyfile", "r").read(),
}

primary_domain = 'c'
highlight_language = 'c'

