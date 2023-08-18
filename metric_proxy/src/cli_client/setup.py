import setuptools
from distutils.core import setup

setuptools.setup(
    name='tau_metric_client',
    version='0.1',
    author='Jean-Baptiste BESNARD',
    description='This is a command line interface client for the tau_metric_proxy.',
    entry_points = {
        'console_scripts': ['tau_metric_client=tauproxyclient.cli:run'],
    },
    packages=["tauproxyclient"],
    install_requires=[
        'pyyaml',
        'rich'
    ],
    python_requires='>=3.5'
)
