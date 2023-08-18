import setuptools
from distutils.core import setup

setuptools.setup(
    name='tau_profile_inspect',
    version='0.1',
    author='Jean-Baptiste BESNARD',
    description='This is a command line interface client for the tau_profile storage.',
    entry_points = {
        'console_scripts': ['tau_profile_inspect=tauprofile.cli:run'],
    },
    packages=["tauprofile"],
    install_requires=[
        'pyyaml',
        'rich'
    ],
    python_requires='>=3.5'
)
