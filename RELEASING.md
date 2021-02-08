# Releasing Ogre from Open Robotics fork

The ogre-2.1-release repository follows the guidelines of
[Packaging with Git](https://wiki.debian.org/PackagingWithGit). There are
different permanent branches useful for packaging:

 * `upstream`: the branch host the code coming from the Ogre project. It also
    has the modifications made by Open Robotics on top of the Ogre code.
 * `pristine-tar`: compressed version of upstream branch.
 * `master`: upstream branch with `debian/` metadata directory.

Note that the fork is not configured to be able to host changes to source code
directly in repository files. Any modification should happen through patches in
`debian/patches`.

## Launch a new release

### Prerequisites

To manage Debian release repositories using git the `git-buildpackage` tool is
required in the local system together with a checkout of this repository:

```bash
sudo apt-get install git-buildpackage
git clone https://github.com/ignition-forks/ogre-2.1-release
```

### Update Changelog

The Debian version of the package uses the form: `2.0.9999~20180616~06a386f`:
 * First `2.0.9999` means that a snapshot just before `2.1` was used from ogre
   repository.
 * `20180616` date when the source snapshot was taken
 * `06a386f` commit hash from the source snapshot from Ogre repository

The repository uses the same changelog for all distributions (a non canonical
way of using this kind of gbp repositories). Since the upstream source code it
is not designed to be modified, new versions usually implies: a new supported
platform or new patches in Debian building system in the `master` branch.

To bump revision:
```bash
# distro can be any release name of Ubuntu or Debian
gbp dch --force-distribution <distro> --auto
```

From there are two options: if a changes in Debian packaging need to be released
or a new platform needs to be released:

#### Option 1) modify revision with changes in packaging

The command should open an editor with the reversion set to something like
`-Xubuntu1` while keeping the version still the same. Edit `Xubuntu1` to just
use `X` +1.

```
# example of debian/changelog modification
ogre-2.1 (2.0.9999~20180616~06a386f-3ubuntu1) bionic; urgency=medium
<manual edition>
ogre-2.1 (2.0.9999~20180616~06a386f-4) bionic; urgency=medium
```
#### Option 2) modify revision with changes in distribution

The command should open an editor with the reversion set to something like
`-Xubuntu1` while keeping the version still the same. Edit `Xubuntu1` to leave
the version as it was before:

```
# example of debian/changelog modification
ogre-2.1 (2.0.9999~20180616~06a386f-3ubuntu1) buster; urgency=medium
<manual edition>
ogre-2.1 (2.0.9999~20180616~06a386f-3) focal; urgency=medium
```

### Triggering the release in Jenkins

After preparing the repository in a local system clone all is ready to launch
a new release directly in the Jenkins server job:

 * https://build.osrfoundation.org/job/ogre-2.1-debbuilder

Login in the server and press `Build with Parameters`. The following parameters
needs to be set correctly:
 * `LINUX_DISTRO` (Ubuntu or Debian) and `DISTRO` (distribution name) needs to
   match what is currently in the `Changelog`.
 * `ARCH` can be set to any particular architecture supported by Ubuntu/Debian
   being `amd64`, `arm64` or `armfh` the main ones.

The build should generate packages and upload the to the stable repository.
