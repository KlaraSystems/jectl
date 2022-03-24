#!/bin/sh

# poudriere chokes on a non-empty value if etc/rc.conf
# doesn't exist - jail.conf will set hostname anyway.
HOSTNAME=""

# temporary variable to delay copying the overlay directory
_OVERLAYDIR=

# type of stream to generate
# 0 = create new jail
# 1 = update existing jail
je_update=0

# set defaults
ZFS_JEROOT=
ZFS_JAIL_NAME="main"
ZFS_BOOTFS_NAME="default"
ZFS_BEROOT_NAME="JAIL"

# create temporary pool
_zfs_create_zpool() {
	truncate -s ${IMAGESIZE} ${WRKDIR}/raw.img;
	md=$(/sbin/mdconfig ${WRKDIR}/raw.img);

	zpool create \
	    -O mountpoint=/${ZFS_POOL_NAME} \
	    -O canmount=noauto \
	    -O checksum=sha512 \
	    -O compression=on \
	    -O atime=off \
	    -R ${WRKDIR}/world ${ZFS_POOL_NAME} /dev/${md} || exit;
}

zfs_prepare() {
	_zfs_create_zpool

	# create new jail
	case "${MEDIAREMAINDER}" in
	*full*|send|zfs)
		je_update=0
		ZFS_JEROOT="${ZFS_POOL_NAME}/${ZFS_JAIL_NAME}"

		msg "[jail environment] generate stream to create new jail"

		zfs create -o canmount=off -o mountpoint=none ${ZFS_JEROOT}
		zfs create -o mountpoint=/ ${ZFS_JEROOT}/${ZFS_BOOTFS_NAME}
		zfs create ${ZFS_JEROOT}/${ZFS_BOOTFS_NAME}/config;
		;;
	esac

	# update existing jail
	case "${MEDIAREMAINDER}" in
	    *be*)
		je_update=1
		ZFS_JEROOT="${ZFS_POOL_NAME}"

		msg "[jail environment] generate stream to update existing jail"

		zfs create -o mountpoint=/ ${ZFS_JEROOT}/${ZFS_BOOTFS_NAME}
		;;
	esac

	# delay copying the overlay directory until files are installed
	# to ${ZFS_POOL_NAME}/${ZFS_BOOTFS_NAME}.
	#
	# depending on the contents of the overlay directory, the config
	# dataset will be bootstrapped with files of interest.
	if [ -d "${EXTRADIR}" ]; then
		_OVERLAYDIR=${EXTRADIR};
		EXTRADIR=;
	fi

}

bootstrap_links()
{
	cd ${_OVERLAYDIR}
	find * -type l | \
	while read link; do
		src=$(readlink $link);
		
		# nothing to shuffle around, check next link
		if [ ! -e "${WRKDIR}/world/$link" ]; then
			continue;
		fi

		# does this link link into the config dataset?
		# if not, skip it
		if ! dirname $src | grep -Eq "^/config$|^/config/"; then     
			continue;
		fi

		# remove ${WRKDIR}/world/$link to avoid clashing when the
		# overlay directory is copied in - for example, cp will
		# complain when copying a symbolic link over an existing
		# directory.
		if [ ${je_update} -eq 0 ]; then
		        msg "[jail environment] moving $link to $src"
			# bootstrap config dataset with defaults
			mkdir -p $(dirname ${WRKDIR}/world/$src)
			cp -fRPp ${WRKDIR}/world/$link ${WRKDIR}/world/$src
		        rm -rf ${WRKDIR}/world/$link
		else
		        msg "[jail environment] removing $link from jail environment"
		        rm -rf ${WRKDIR}/world/$link
		fi
	done
}

zfs_build() {
	zroot=${ZFS_JEROOT}
	jail_version=$(poudriere jail -i -j $JAILNAME | awk '$2 == "version:" { print $3 }')
	freebsd_version=""

	if [ $je_update -eq 0 ]; then
		zroot=${ZFS_POOL_NAME}/${ZFS_JAIL_NAME}
	fi

	if [ -f "${WRKDIR}/world/usr/include/sys/param.h" ]; then
		freebsd_version=$(awk '/^\#define[[:space:]]*__FreeBSD_version/ {print $3}' \
		    ${WRKDIR}/world/usr/include/sys/param.h)
	fi

	if [ -d "${_OVERLAYDIR}" ]; then
		# not sure if bootstrap is necessary
		(bootstrap_links)

		msg "[jail environment] copying in overlay directory from ${_OVERLAYDIR}"
		cp -fRPp "${_OVERLAYDIR}/" ${WRKDIR}/world/

		EXTRADIR=${_OVERLAYDIR}
		_OVERLAYDIR=
	fi

	msg "[jail environment] setting zfs user properties" 

	# not quite a fingerprint
	zfs set je:version="${jail_version}" ${zroot}/${ZFS_BOOTFS_NAME}
	zfs set je:poudriere:jailname="${JAILNAME}" ${zroot}/${ZFS_BOOTFS_NAME}
	zfs set je:poudriere:overlaydir="${EXTRADIR}" ${zroot}/${ZFS_BOOTFS_NAME}
	zfs set je:poudriere:packagelist="${PACKAGELIST}" ${zroot}/${ZFS_BOOTFS_NAME}
	zfs set je:poudriere:freebsd_version="${freebsd_version}" ${zroot}/${ZFS_BOOTFS_NAME}

	# dont know the final mountpoint, so none.
	zfs set mountpoint=none canmount=off ${zroot}
	zfs set mountpoint=none canmount=noauto ${zroot}/${ZFS_BOOTFS_NAME}

	if [ $je_update -eq 0 ]; then
		zfs set canmount=noauto ${zroot}/${ZFS_BOOTFS_NAME}/config
	fi
}

zfs_generate()
{
	: ${SNAPSHOT_NAME:=$IMAGENAME}
	zroot=${ZFS_JEROOT}

	msg "[jail environment] creating snapshot for replication stream"

	if [ ${je_update} -eq 0 ]; then
		# jectl checks this property to determine if the generated
		# stream will be used to create a new jail
		zfs set je:poudriere:create="${ZFS_BOOTFS_NAME}" ${ZFS_JEROOT}

		SNAPSPEC="${ZFS_JEROOT}@${SNAPSHOT_NAME}"
		zfs snapshot -r "$SNAPSPEC"

		FINALIMAGE=${IMAGENAME}.full.zfs
		_zfs_writereplicationstream "${SNAPSPEC}" "${FINALIMAGE}"

	else
		BESNAPSPEC="${ZFS_JEROOT}/${ZFS_BOOTFS_NAME}@${SNAPSHOT_NAME}"
		zfs snapshot "$BESNAPSPEC"

		FINALIMAGE=${IMAGENAME}.je.zfs
		_zfs_writereplicationstream "${BESNAPSPEC}" "${FINALIMAGE}"
	fi

	zpool export ${ZFS_POOL_NAME}
	zroot=
	/sbin/mdconfig -d -u ${md#md}
	md=
}
