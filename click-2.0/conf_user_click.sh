#!/bin/sh

IO_ENGINE=`dirname $0`/../io_engine
IO_ENGINE=`readlink -f $IO_ENGINE`

cd `dirname $0`/
#./configure --disable-linuxmodule --enable-warp9--enable-userlevel --enable-dmalloc  # for debugging 
CPPFLAGS="-I$IO_ENGINE/include -D_GNU_SOURCE" LDFLAGS="-L$IO_ENGINE/lib" ./configure --disable-linuxmodule --enable-warp9 --enable-user-multithread --enable-userlevel --enable-multithread=24  --enable-ip6 --enable-ipsec

