#!/bin/bash

KERNEL_DIR=$PWD
INITRAMFS_SRC_DIR=../sc04d_initramfs
INITRAMFS_TMP_DIR=/tmp/sc04d_initramfs

cpoy_initramfs()
{
  if [ -d $INITRAMFS_TMP_DIR ]; then
    rm -rf $INITRAMFS_TMP_DIR  
  fi
  cp -a $INITRAMFS_SRC_DIR $(dirname $INITRAMFS_TMP_DIR)
  rm -rf $INITRAMFS_TMP_DIR/.git
  find $INITRAMFS_TMP_DIR -name .gitignore | xargs rm
}

BUILD_DEFCONFIG=sc04d_defconfig
OUTPUT_DIR=out

# generate LOCALVERSION
. mod_version

# check and get compiler
. cross_compile

# set build env
export ARCH=arm
export CROSS_COMPILE=$BUILD_CROSS_COMPILE
export LOCALVERSION="-$BUILD_LOCALVERSION"

echo "=====> BUILD START $BUILD_KERNELVERSION-$BUILD_LOCALVERSION"

if [ ! -n "$1" ]; then
  echo ""
  read -p "select build? [(a)ll/(u)pdate/(z)Image default:update] " BUILD_SELECT
else
  BUILD_SELECT=$1
fi

# copy initramfs
echo ""
echo "=====> copy initramfs"
cpoy_initramfs


# make start
if [ "$BUILD_SELECT" = 'all' -o "$BUILD_SELECT" = 'a' ]; then
  echo ""
  echo "=====> cleaning"
  make clean
  cp -f ./arch/arm/configs/$BUILD_DEFCONFIG ./.config
  make -C $PWD oldconfig || exit -1
fi

if [ "$BUILD_SELECT" != 'zImage' -a "$BUILD_SELECT" != 'z' ]; then
  echo ""
  echo "=====> build start"
  if [ -e make.log ]; then
    mv make.log make_old.log
  fi
  nice -n 10 make -j12 2>&1 | tee make.log
fi

# check compile error
COMPILE_ERROR=`grep 'error:' ./make.log`
if [ "$COMPILE_ERROR" ]; then
  echo ""
  echo "=====> ERROR"
  grep 'error:' ./make.log
  exit -1
fi

# *.ko replace
find -name '*.ko' -exec cp -av {} $INITRAMFS_TMP_DIR/lib/modules/ \;

# build zImage
echo ""
echo "=====> make zImage"
nice -n 10 make -j2 zImage CONFIG_INITRAMFS_SOURCE="$INITRAMFS_TMP_DIR" CONFIG_INITRAMFS_ROOT_UID=`id -u` CONFIG_INITRAMFS_ROOT_GID=`id -g` || exit 1

if [ ! -e $OUTPUT_DIR ]; then
  mkdir -p $OUTPUT_DIR
fi

echo ""
echo "=====> CREATE RELEASE IMAGE"
# clean release dir
if [ `find ./$OUTPUT_DIR -type f | wc -l` -gt 0 ]; then
  rm $KERNEL_DIR/$OUTPUT_DIR/*
fi

# copy zImage
cp arch/arm/boot/zImage ./$OUTPUT_DIR/boot.img
echo "  $OUTPUT_DIR/boot.img"

# create cwm image
cd $KERNEL_DIR/$OUTPUT_DIR
if [ -d tmp ]; then
  rm -rf tmp
fi
mkdir -p tmp/META-INF/com/google/android
cp boot.img ./tmp/
cp $KERNEL_DIR/release-tools/update-binary $KERNEL_DIR/$OUTPUT_DIR/tmp/META-INF/com/google/android/
sed -e "s/@VERSION/$BUILD_LOCALVERSION/g" $KERNEL_DIR/release-tools/updater-script.sed > $KERNEL_DIR/$OUTPUT_DIR/tmp/META-INF/com/google/android/updater-script
cd tmp && zip -rq ../cwm.zip ./* && cd ../
SIGNAPK_DIR=$KERNEL_DIR/release-tools/signapk
java -jar $SIGNAPK_DIR/signapk.jar $SIGNAPK_DIR/testkey.x509.pem $SIGNAPK_DIR/testkey.pk8 cwm.zip $BUILD_LOCALVERSION-signed.zip
rm cwm.zip
rm -rf tmp
echo "  $OUTPUT_DIR/$BUILD_LOCALVERSION-signed.zip"

cd $KERNEL_DIR
echo ""
echo "=====> BUILD COMPLETE $BUILD_KERNELVERSION-$BUILD_LOCALVERSION"
exit 0
