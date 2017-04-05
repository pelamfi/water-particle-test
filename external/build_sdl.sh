
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/usr/local/bin
echo Setting PATH to disable macports: $PATH
export PATH

sdlVersion=2.0.5
externalDir=`pwd`
logDir=$externalDir/logs
installDir=$externalDir/sdl
srcDir=$externalDir/SDL2-${sdlVersion}

echo logs $logDir

mkdir -p $logDir

echo Removing old source dir and install dir

rm -rf $srcDir || exit 1
rm -rf $installDir || exit 1

echo Extracting sources

time tar vxzf SDL2-${sdlVersion}.tar.gz > $logDir/sdl-tar-x.log 2>&1  || exit 1

cd $srcDir || exit 1

echo Running configure

time ./configure --prefix=$installDir > $logDir/sdl-configure.log 2>&1 || exit 1

echo Running make

time make -j6 install >> $logDir/sdl-make.log 2>&1 || exit 1

echo OK
