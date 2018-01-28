from setuptools import setup

setup(name='supervise_api',
      version='0.3.0',
      description='An API for running processes safely and securely',
      long_description=("This package uses the supervise utility, a separately-available C binary,"
                        " to provide a better process API for Linux."),
      classifiers=[
          "License :: OSI Approved :: GNU Lesser General Public License v3 or later (LGPLv3+)",
          "Operating System :: POSIX :: Linux",
      ],
      keywords='linux subprocess processes',
      url='https://github.com/catern/supervise',
      author='catern',
      author_email='sbaugh@catern.com',
      license='LGPLv3+',
      packages=['supervise_api'])
