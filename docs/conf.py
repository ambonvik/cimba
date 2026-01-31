import os
import re

project = 'cimba'
with open(os.path.join(os.path.dirname(__file__), '../meson.build'), 'r') as f:
    meson_build = f.read()
version_match = re.search(r"version\s*:\s*'([^']+)'", meson_build)
if version_match:
    release = version_match.group(1)
    version = release
else:
    release = '3.0.0'
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

