import setuptools
from distutils.core import setup

setuptools.setup(
    name='admire_deploy',
    version='0.1',
    author='Jean-Baptiste BESNARD',
    description='This is a wrapper script to start the admire monitoring infrastructure.',
    entry_points = {
        'console_scripts': ['admiremon_run=admiremon.cli:run'],
    },
    packages=["admiremon"],
    install_requires=[
        'pyyaml',
        'rich'
    ],
    python_requires='>=3.5'
)
