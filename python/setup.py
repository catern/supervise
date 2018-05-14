from setuptools import setup

setup(name='supervise_api',
      version='0.6.0',
      description='An API for running processes safely and securely',
      long_description=("This package uses the supervise utility, a separately-available C binary,"
                        " to provide a better process API for Linux."),
      classifiers=[
          "License :: OSI Approved :: MIT License",
          "Operating System :: POSIX :: Linux",
      ],
      keywords='linux subprocess processes',
      url='https://github.com/catern/supervise',
      author='catern',
      author_email='sbaugh@catern.com',
      license='MIT',
      packages=['supervise_api', 'supervise_api.tests'])
