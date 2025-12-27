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

copyright = 'Asbjørn M. Bonvik 2025'

extensions = ['breathe', 'exhale']

html_theme = 'sphinx_rtd_theme'
html_static_path = ['/home/ambonvik/github/cimba/docs/static']
html_css_files = ['custom.css']

breathe_projects = {
    "cimba": "/home/ambonvik/github/cimba/build/docs/xml"
}

breathe_default_project = "cimba"

breathe_default_members = ('members', 'undoc-members')
breathe_domain_by_extension = {
    "h": "c",
}

breathe_implementation_filename_extensions = ['.c']

exhale_args = {
    "containmentFolder":    "/home/ambonvik/github/cimba/docs/api",
    "rootFileName":         "library_root.rst",
    "doxygenStripFromPath": "..",
    "rootFileTitle":        "Library API",
    "createTreeView":       True,
    "lexerMapping": {
        r".*\.c": "c",
        r".*\.h": "c"
    },
    "unabridgedOrphanKinds": [],
}

primary_domain = 'c'
highlight_language = 'c'

