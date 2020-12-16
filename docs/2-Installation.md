# redpesk® cloud publication binding

**Important note**: the cloud publication binding actually comes in two parts: the
target/edge binding and the cloud part. Please see below how to install both.

The cloud part runs in a container an needs a machine supporting LXD.

## A - Redpesk targets

The cloud publication binding is available in the standard Redpesk repositories
included in your board configuration file.

```bash
dnf install cloud-publication-binding
```

## B - Native 

### Installing from package repositories

First, refer to the ["Verify Your Build Host"](../../developer-guides/host-configuration/docs/1-Setup-your-build-host.html)
section to check your host is supported and perform the associated necessary
configuration steps. Then, you can use the commandlines below to get the
`cloud-publication-binding` binding and all its dependencies. 

Please follow the instructions contained in the paragraph suitable for your
distribution. 

**Important**: the host configuration steps above will have made you define the
`DISTRO` variable used in the package repository URLs below.

#### Ubuntu 20.04 and 18.04

First, add the `redpesk-sdk` repository to the list of your packages repositories.

```bash
# Add the repository in your list
$ echo "deb https://download.redpesk.bzh/redpesk-devel/releases/33/sdk/$DISTRO/ ./" | sudo tee -a /etc/apt/sources.list
# Add the repository key
$ curl -L https://download.redpesk.bzh/redpesk-devel/releases/33/sdk/$DISTRO/Release.key | sudo apt-key add -
```

Then, update the list of packages and simply install the `cloud-publication-binding` package.

```bash
# Update the list of available packages
$ sudo apt update
# Installation of cloud-publication-binding
$ sudo apt-get install cloud-publication-binding
```

#### Fedora 32 and 33

First, add the `redpesk-sdk` repository to the list of your packages repositories.

```bash
$ cat << EOF > /etc/yum.repos.d/redpesk-sdk.repo
[redpesk-sdk]
name=redpesk-sdk
baseurl=https://download.redpesk.bzh/redpesk-devel/releases/33/sdk/$DISTRO
enabled=1
repo_gpgcheck=0
type=rpm
gpgcheck=0
skip_if_unavailable=True
EOF
```

Then, simply install the `cloud-publication-binding` package.

```bash
dnf install cloud-publication-binding
```

#### OpenSUSE Leap 15.1 and 15.2

First, add the Redpesk "sdk" repository in the list of your packages repositories.

```bash
$ OPENSUSE_VERSION=15.2 # Set the right OpenSUSE version
# Add the repository in your list
$ sudo zypper ar https://download.redpesk.bzh/redpesk-devel/releases/33/sdk/$DISTRO/ redpesk-sdk
# Refresh your repositories
$ sudo zypper ref
```

Then, simply install the `cloud-publication-binding` package.

```bash
sudo zypper in cloud-publication-binding
```

## C - From source

We advise you use the [local builder](../getting_started/local_builder/docs/1_installation.html) for building the binding sources. 
The local builder comes with everything setup to build Redpesk® projects.

### Dependencies

- gcc
- make
- cmake
- afb-cmake-modules
- json-c
- afb-binding
- libmicrohttpd
- afb-libhelpers
- afb-libcontroller

Fedora/OpenSUSE/Redpesk:
```bash
sudo dnf install gcc make cmake afb-cmake-modules json-c-devel afb-binding-devel libmicrohttpd afb-libhelpers-devel afb-libcontroller
```

Ubuntu:
```bash
sudo apt install gcc make cmake afb-cmake-modules-bin libsystemd-dev libjson-c-dev afb-binding-dev libmicrohttpd12 afb-libhelpers-dev afb-libcontroller
```

### Build & Install

```bash
git clone https://github.com/redpesk-common/cloud-publication-binding
cd cloud-publication-binding
mkdir build
cd build
cmake ..
make
make install
```

## D - Cloud side / container

The cloud publication binding purpose is to publish target data to the cloud.
The current implementation makes use of a Redis database driven by the
`redis-tsdb-binding` binding and application framework included in a container
for easy deployments.

### Install the container

IoT.bzh provides a setup script to easily install and configure LXD containers.
You can find it at in the [redpesk-localbuilder-installer repository](https://github.com/redpesk-devtools/redpesk-localbuilder-installer).

Follow these steps to setup LXD and configure the cloud binding container:

```bash
git clone https://github.com/redpesk-devtools/redpesk-localbuilder-installer
cd redpesk-localbuilder-installer
./install-redpesk-localbuilder.sh create -c redpesk-cloud-publication -t cloud-publication
```

This will download LXD for your OS, pull the cloud publication binding host 
side container and start it.

### Setup and check target/container connectivity

At this point, the container is running on your host machine, and the Redis
binding is listening on port 21212. The next step is to check you can reach it
from your target.

The specifics of target/host connectivity are left to the reader as they depend
on each user setup. The easiest is to have both target and host running on the
same subnet, connected to the same switch/access point.

The first criteria would be that you can ping the host machine running the
container from your target. 

You can then check you can also reach the container
itself using a command like `netcat` (here, the target is a Qemu virtual
machine):

```bash
# nc -vz 10.0.2.2 21212
Ncat: Version 7.80 ( https://nmap.org/ncat )
Ncat: Connected to 10.0.2.2:21212.
Ncat: 0 bytes sent, 0 bytes received in 0.02 seconds
```

The next step is to add an entry into `/etc/hosts` on the target. Following our
configuration above, this would give:

```bash
echo '10.0.2.2 cloud-publication-container' >> /etc/hosts
```

You are then ready to start the binding on the target following the [usage
instructions](4-Usage.html).
