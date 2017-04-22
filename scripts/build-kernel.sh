#!/bin/bash
#set -x
###################################################################################
# Default values
###################################################################################
export ARCH=arm64
platform=${platform:-d05}
CROSS_COMPILE=${CROSS_COMPILE}
OUTPUT_DIR=${OUTPUT_DIR:-build}
STARTUP_DISK=${STARTUP_DISK:-sda1}
CORE_NUM=`cat /proc/cpuinfo | grep "processor" | wc -l`
###################################################################################
# build_kernel_usage
###################################################################################
build_kernel_usage()
{
cat << EOF
Usage: ./sailling/build-kernel.sh [clean] --cross=xxx --output=xxx

    clean: clean the kernel binary files (include dtb)
    --cross: cross compile prefix (if the host is not arm architecture, it must be specified.)
    --output: target binary output directory, default ./workspace
    --module: select compile kernel module & firmware

    caution:
	the following options only for the target board ( D03 D05 .etc )

    --allflush: just used to build kernel in target board, update startup disk and module(default sda1)
    --startup; automate set starting items in grub.cfg and copy Image to special disk(default sda1)
    --startup_disk: Generally can be set sda1 sdb1 or nvme0n1 (ES3000), default sda1

Example:
    ./scripts/build-kernel.sh
    ./scripts/build-kernel.sh --module
    ./scripts/build-kernel.sh --output=output_dir
    ./scripts/build-kernel.sh --cross=aarch64-linux-gnu-
    ./scripts/build-kernel.sh --module --cross=aarch64-linux-gnu-

    ./scripts/build-kernel.sh --allflush
    ./scripts/build-kernel.sh --startup --startup_disk=sdb1
    ./scripts/build-kernel.sh --startup --startup_disk=nvme0n1
    ./scripts/build-kernel.sh clean
EOF
}

###################################################################################
# build_kernel $kernel_dir $kernel_bin $modules_dir
###################################################################################
build_kernel()
{
    kernel_dir=$1
    kernel_bin=$2
    modules_dir=$3
    sudo cp -f ./arch/arm64/configs/estuary_te_defconfig  $kernel_dir/.sailing.config
    make O=$kernel_dir CROSS_COMPILE=${CROSS_COMPILE} KCONFIG_ALLCONFIG=$kernel_dir/.sailing.config alldefconfig
    #make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE O=$kernel_dir menuconfig
    make O=$kernel_dir CROSS_COMPILE=${CROSS_COMPILE} -j${CORE_NUM} ${kernel_bin##*/}

    if [ x"$MODULE" = x"yes" ]; then
        if [ x"$ALLFLUSH" = x"yes" ]; then
		modules_dir=/
	fi
	echo "Compile module ......"
	#Compile kernel module
	make O=$kernel_dir CROSS_COMPILE=${CROSS_COMPILE} modules -j${CORE_NUM}
	make O=$kernel_dir CROSS_COMPILE=${CROSS_COMPILE} modules_install INSTALL_MOD_PATH=$modules_dir
    fi
    if [ x"$FIRMWARE" = x"yes" ]; then
	echo "Compile firmware ......"
	#Compile firmware
	mkdir -p $modules_fir/lib/firmware
	make O=$kernel_dir CROSS_COMPILE=${CROSS_COMPILE} firmware_install INSTALL_FW_PATH=$modules_dir/lib/firmware
    fi
}

###################################################################################
# Automatic install & debug
###################################################################################
auto_install()
{
    install_path=/root/$STARTUP_DISK
    distro_name=`uname -a | awk '{print $2}'`
    disk_info=`blkid | grep LABEL`
    install_disk=`expr "${disk_info}" : '\([^:]*\):[^:]*'`
    root_dev=`mount | grep  "on / type" | awk '{print $1}'`
    root_dev_info=`blkid -s PARTUUID $root_dev 2>/dev/null | grep -o "PARTUUID=.*" | sed 's/\"//g'`
    boot_dev_info=`blkid -s UUID $install_disk 2>/dev/null | grep -o "UUID=.*" | sed 's/\"//g'`
    boot_dev_uuid=`expr "${boot_dev_info}" : '[^=]*=\(.*\)'`

    #Install kernel Image
    if [ ! -d "$install_path" ]; then
        mkdir -p $install_path
    fi

    mount /dev/$STARTUP_DISK $install_path

    cp $kernel_bin $install_path/Image-debug
    linux_arg="/Image-debug root=$root_dev_info rootwait rw pcie_aspm=off pci=pcie_bus_perf"
    pushd $install_path >/dev/null
cat >> grub.cfg << EOF

# Debug Booting from SATA/SAS with $distro_name rootfs (Console)
menuentry "${platform} $distro_name (Console) Debug" --id ${platform}_${distro_name}_console_debug {
    set root=(hd0,gpt1)
    search --no-floppy --fs-uuid --set=root $boot_dev_uuid
    linux $linux_arg
}

EOF
    #get the startup linenum
    startup_line=`sed -n '/^set default=/='  $install_path/grub.cfg`
    default_menuentry="${platform}"_"${distro_name}"_console_debug
    sed -i ''$startup_line's/.*/set default='$default_menuentry'/' $install_path/grub.cfg
    sync
    sleep 1
    popd
    #umount need add conditional judgment
    umount /dev/$STARTUP_DISK $install_path
    echo "Update grub.cfg done!"

}

###################################################################################
# build_kernel <output_dir>
###################################################################################
build_project()
{
	mkdir -p ${OUTPUT_DIR}/kernel
	mkdir -p ${OUTPUT_DIR}/modules
	kernel_dir=$(cd ${OUTPUT_DIR}/kernel; pwd)
	kernel_bin=$kernel_dir/arch/arm64/boot/Image
	modules_dir=$(cd ${OUTPUT_DIR}/modules; pwd)

	if ! build_kernel $kernel_dir $kernel_bin $modules_dir; then
		echo -e "\033[31mError! Build kernel distro failed!\033[0m" ; exit 1
	fi

	mkdir -p $OUTPUT_DIR/binary  2>/dev/null
	cp $kernel_bin $OUTPUT_DIR/binary
	cp $kernel_dir/vmlinux $OUTPUT_DIR/binary
	cp $kernel_dir/System.map $OUTPUT_DIR/binary

}

###################################################################################
# get args
###################################################################################
while test $# != 0
do
    case $1 in
        --*=*) ac_option=`expr "X$1" : 'X\([^=]*\)='` ; ac_optarg=`expr "X$1" : 'X[^=]*=\(.*\)'` ;;
	-*) ac_option=$1 ; ac_optarg=$2; ac_shift=shift ;;
        *) ac_option=$1 ac_optarg=$2;;
    esac

    case $ac_option in
	    clean) CLEAN=yes ;;
	    --module) MODULE=yes;;
	    --cross) CROSS_COMPILE=$ac_optarg ;;
            --output) OUTPUT_DIR=$ac_optarg ;;
	    --startup) STARTUP=yes;;
	    --allflush) MODULE=yes; STARTUP=yes; ALLFLUSH=yes;;
	    --startup_disk) STARTUP_DISK=$ac_optarg ;;
        *) build_kernel_usage ; exit 1 ;;
    esac

    shift
done

###################################################################################
# clean_kernel <output_dir>
###################################################################################
clean_kernel()
{
	if [ x"$CLEAN" = x"yes" ]; then
		echo "Clean kernel ......"
		sudo rm -rf $OUTPUT_DIR
		echo "Clean binary files done!"
		exit 0
	fi
}


###################################################################################
# Const Variables, PATH
###################################################################################
LOCALARCH=`uname -m`
CURDIR=`pwd`
TOPDIR=$(cd `dirname $0` ; pwd)
if [ x"$CURDIR" = x"$TOPDIR" ]; then
	echo "---------------------------------------------------------------"
	echo "- Please execute build-kernel.sh in kernel dir!"
	echo "- Example:"
	echo "-     ./scripts/build-kernel.sh --output=workspace"
	echo "---------------------------------------------------------------"
	exit 1
fi

# build kernel or clean_kernel
#if ! clean_kernel; then
#        echo -e "\033[31mError! Clean kernel failed!\033[0m" ; exit 1
#fi
#
if ! build_project; then
        echo -e "\033[31mError! Build kernel failed!\033[0m" ; exit 1
fi

if [ x"$STARTUP" = x"yes" ]; then
    echo "Startup deploy ...... \n"
    if ! auto_install; then
        echo -e "\033[31mError! Auto install startup disk failed!\033[0m" ; exit 1
    fi
fi



