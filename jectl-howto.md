# Introduction

This will allow to generate jail with ZFS to permit fast updating.

## What you need 

You need :
- A freebsd server with poudriere-devel and a zpool NOT named `zroot`
- A poudriere setup
- A jail poudiere created.
- Jail server that will run jails. It needs a `zroot` pool TODO: avoid hardcoded things

## Poudriere environement 

Notice : *DON'T* have zroot pool otherwize it will not be possible to generate 
zfs streams.

### Create poudriere jail environement

We will create 2 jail env to permit upgrade from 12.4 to 13.2

```
# poudriere jail -c -j 120x64 -v 12.4-RELEASE
# poudriere jail -c -j 130x64 -v 13.2-RELEASE
```

### Create images.

There is 2 kind of images :
- the INITIAL image, which name is "full"
- the UPDATE image, which name is "je"

#### Initial image :

You will need the 'generate-je.sh' from jectl github.

For example to generate a initial image of 12.4-RELEASE :
```
# poudriere image -t zfs+send -B /home/generate-je.sh -j 120x64 -n 120stream -s 1G
[00:00:00] Preparing the image '120stream'
[00:00:00] Calculated image size 976m
[00:00:00] [jail environment] generate stream to create new jail
[00:00:01] Installing world with tar
>>> Removing old files (only deletes safe to delete libs)
>>> Old files removed
>>> Removing old directories
>>> Old directories removed
To remove old libraries run 'make delete-old-libs'.
>>> Removing old libraries
Please be sure no application still uses those libraries, else you
can not start such an application. Consult UPDATING for more
information regarding how to cope with the removal/revision bump
of a specific library.
>>> Old libraries removed
[00:02:06] Installing world done
[00:02:06] Installing packages
[00:02:07] [jail environment] setting zfs user properties
[00:02:07] [jail environment] creating snapshot for replication stream
[00:02:07] Creating replication stream
[00:02:13] Image available at: /usr/local/poudriere/data/images/120stream.full.zfs
dd
```

#### Update Image

As initial image you will need the generate-je + poudriere. Notice the `+be` 
used to generate update image :

```
poudriere image -t zfs+send+be -B /home/generate-je.sh -j 130x64 -n 130stream -s 1G
[00:00:00] Preparing the image '130stream'
[00:00:00] Calculated image size 976m
[00:00:01] [jail environment] generate stream to update existing jail
[00:00:01] Installing world with tar
>>> Removing old files (only deletes safe to delete libs)
>>> Old files removed
>>> Removing old directories
>>> Old directories removed
To remove old libraries run 'make delete-old-libs'.
>>> Removing old libraries
Please be sure no application still uses those libraries, else you
can not start such an application. Consult UPDATING for more
information regarding how to cope with the removal/revision bump
of a specific library.
>>> Old libraries removed
[00:01:55] Installing world done
[00:01:55] Installing packages
[00:01:55] [jail environment] setting zfs user properties
[00:01:56] [jail environment] creating snapshot for replication stream
[00:01:56] Creating replication stream
[00:01:24] Image available at: /usr/local/poudriere/data/images/130stream.je.zfs
```

Then we have 2 stream that can be used for jails.

## Usage in production

### Create jail.conf

```
path = /$name;
exec.prepare = "/sbin/jectl mount $name $path";
exec.start = "/bin/sh /etc/rc";
exec.stop = "/bin/sh /etc/rc.shutdown";

klara {

}
```

The `klara` is the name of the jail.
Notice you need to have a `zroot` zpool to use `jectl`.

### Create the initial jail

Simply :

```
# cat 120stream.full.zfs | jectl import klara
create zroot/JAIL
create zroot/JE
```

The following dataset will be created :

```
zroot/JAIL                                     711M  7.87G      192K  none
zroot/JAIL/klara                               711M  7.87G      192K  none
zroot/JAIL/klara/default                       711M  7.87G      710M  none
zroot/JAIL/klara/default/config                192K  7.87G      192K  none
zroot/JE                                       192K  7.87G      192K  none
```

Starting the jail :
```
# service jail onestart
Starting jails: klara.
# jls
   JID  IP Address      Hostname                      Path
     1                                                /klara
```

Connecting into jail :

```
# jexec 1 /bin/sh
sh: can't access tty; job control turned off
# uname -a
FreeBSD  13.2-RELEASE-p4 FreeBSD 13.2-RELEASE-p4 GENERIC  amd64
# freebsd-version -ru
13.2-RELEASE-p4
12.4-RELEASE-p9
```


### Upgrading a jail

In this case we will upgrade a jail from 12.4 to 13.2

### First import the ZFS stream

```
# cat 130stream.je.zfs | jectl import 13.2-RELEASE
```
*NOTICE*: this should be a "je" zfs stream made by poudriere `-t zfs+send+be`
otherwise it will *not* being imported (and fails without any warning).

A new JE is there:
```
zroot/JE                                       733M  7.15G      192K  none
zroot/JE/13.2-RELEASE                          733M  7.15G      733M  none
```

Then activate the JE to `klara` jail :

```
# jectl dump klara
Jail name: klara
Environments:
1.  Name:              default (ACTIVE)
    branch:            12.4-RELEASE-p9
    version:           1204000
    poudriere-jail:    120x64
# jectl activate klara 13.2-RELEASE
# jectl dump klara
Jail name: klara
Environments:
1.  Name:              default
    branch:            12.4-RELEASE-p9
    version:           1204000
    poudriere-jail:    120x64
2.  Name:              13.2-RELEASE (ACTIVE)
    branch:            13.2-RELEASE-p7
    version:           1302001
    poudriere-jail:    130x64
```

Next starting of jail will take 13.2 BE 

```
# service jail onestart
Starting jails: klara.
root@klara-jectl:/home/kiwi # jls
   JID  IP Address      Hostname                      Path
     2                                                /klara
# jexec 2 /bin/sh
sh: can't access tty; job control turned off
# freebsd-version -ru
13.2-RELEASE-p4
13.2-RELEASE-p7
```

## Saving configuration

The system will lost everything when upgrading jail environment. 
Package, configuration, users ...

To avoid that, like a mfsboot you will have to prepare the stream to 
look such data somewhere else that IS not destroy while changing root environment

For that you'll have to cread an overlay directory :

```
mkdir /home/ovr
cd /home/ovr
mkdir etc
cd etc
ln -s /config/rc.conf .
ln -s /config/master.passwd .
```

And run again :
```
# poudriere image -t zfs+send -B /home/generate-je.sh -j 120x64 -n 120stream -s 1G -c /home/ovr
[00:00:00] Preparing the image '120stream'
[00:00:00] Calculated image size 976m
[00:00:00] [jail environment] generate stream to create new jail
[00:00:01] Installing world with tar
>>> Removing old files (only deletes safe to delete libs)
>>> Old files removed
>>> Removing old directories
>>> Old directories removed
To remove old libraries run 'make delete-old-libs'.
>>> Removing old libraries
Please be sure no application still uses those libraries, else you
can not start such an application. Consult UPDATING for more
information regarding how to cope with the removal/revision bump
of a specific library.
>>> Old libraries removed
[00:01:13] Installing world done
[00:01:14] Installing packages
[00:01:14] [jail environment] moving etc/master.passwd to /config/master.passwd
[00:01:14] [jail environment] copying in overlay directory from /usr/home/ovr
[00:01:14] [jail environment] setting zfs user properties
[00:01:15] [jail environment] creating snapshot for replication stream
[00:01:15] Creating replication stream
[00:01:18] Image available at: /usr/local/poudriere/data/images/120stream.full.zfs
```

Same again for the JE :

```
# poudriere image -t zfs+send+be -B /home/generate-je.sh -j 130x64 -n 130stream -s 1G -c /home/ovr
[00:00:00] Preparing the image '130stream'
[00:00:00] Calculated image size 976m
[00:00:01] [jail environment] generate stream to update existing jail
[00:00:01] Installing world with tar
>>> Removing old files (only deletes safe to delete libs)
>>> Old files removed
>>> Removing old directories
>>> Old directories removed
To remove old libraries run 'make delete-old-libs'.
>>> Removing old libraries
Please be sure no application still uses those libraries, else you
can not start such an application. Consult UPDATING for more
information regarding how to cope with the removal/revision bump
of a specific library.
>>> Old libraries removed
[00:01:08] Installing world done
[00:01:08] Installing packages
[00:01:08] [jail environment] removing etc/master.passwd from jail environment
[00:01:08] [jail environment] copying in overlay directory from /usr/home/ovr
[00:01:08] [jail environment] setting zfs user properties
[00:01:08] [jail environment] creating snapshot for replication stream
[00:01:09] Creating replication stream
[00:01:11] Image available at: /usr/local/poudriere/data/images/130stream.je.zfs
```

Then you can use the previous way to handle initial jail setup and jail upgrade

