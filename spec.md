# xbps

## Package names and versions

```
pkgname = 1*(ALPHA / DIGIT / '_' / '-')
pkgver  = pkgname '-' version
pkgdep  = pkgver / pattern
```

```
version   = 1*component revision
revision  = '_' 1*DIGIT
component = '.' / 1*DIGIT / keyword
keyword   = "alpha" / "beta" / "pre" / "rc" / "pl"
```

```
pattern    = pkgname (patternmin [patternmax] / patternmax)
patternver = 1*component [revision]
patternmin = '>' ['='] patternver
patternmax = '<' ['='] patternver
```

## dependency resolution

### virtual packages

#### "real" virtual packages

If a virtual package is installed as a dependency or
manually with xbps-install, then there are two ways
how the package is chosen:

1. Through virtualpkg= in configuration files.
2. If a provider for the given virtual package is already in the transaction
   this is the chosen one, like it is currently.
3. The alphabetically last package in the repository index.

When a virtual package is being removed,
then it breaks

```
$ xbps-query -p provides -s 'awk'
gawk-5.1.0_1: awk-0_1
mawk-1.3.4.20200120_1: awk-0_1
nawk-20180827_1: awk-0_1
```

Installing a already installed virtual package manually
is even more broken than previous. It will "install" the
already installed package again based on the underspecified
method of choosing the provider.

```
# xbps-install -n awk
nawk-20180827_1 install x86_64 https://alpha.de.repo.voidlinux.org/current 144260 52152
```

Removing them is also pretty bad, it won't allow you to remove the "chosen" package:

```
# xbps-remove nawk
nawk-20180827_1 in transaction breaks installed pkg `autoconf-2.69_8'
nawk-20180827_1 in transaction breaks installed pkg `runit-void-20200720_1'
Transaction aborted due to unresolved dependencies.

# xbps-remove -n mawk
mawk-1.3.4.20200120_1 remove x86_64 https://alpha.de.repo.voidlinux.org/current 210416 80180
```

#### A "real" package and one or more "virtual packages".

This seems fine, you can install the real package and/or one or more packages
providing the same package.


## replaces

This works ok, there seems to be some issues with the inheritance of
the automatic/manual mode, but I haven't looked into it and I think this could
also benefit from a better specification.

# What should be changed?

First we need to better define how virtual packages ("provides") and replaces
work.

Then we can add a new feature, 

## virtual packages

1. One or more virtual packages:
  I think it would be good to define how virtual packages are chosen.

  If a virtual package is being installed as dependency or manually
  with xbps-install choose the real package by the following criteria:
  1. virtualpkg= in configuration files.
  2. If a provider for the given virtual package is already in the transaction
     this is the chosen one, like it is currently.
  3. By version number. i.e. for the awk virtual package
     gawk provides awk-1, nawk and mawk provides awk-0.
     Then gawk is the default.
  4. If there is no single greatest version,
     then use some undefined behaviour like
     the reverse alphabetically order like its done currently.

2. A real package and one or more "virtual packages":
  This should be unchanged, xbps will prefer the real package,
  users can manually install other providers or providers can be
  installed through dependencies.


## replaces

A package in the transaction has one or more replaces= that match
currently installed packages uninstall those packages and choose one
and mark the package as automatically/manually installed based on:
- If the package is updated keep the manually/automatically installed mode.
- If the package is installed through xbps-install, mark it manually installed.
- If the package is installed through a dependency then mark it automatically
  installed if all of the packages it replaces are marked automatically installed.


## updating packages not found 
