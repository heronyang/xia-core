LOCAL_PATH := $(call my-dir)
  subdirs := $(addprefix $(LOCAL_PATH)/,$(addsuffix /Android.mk, \
        tools \
  ))
include $(subdirs)

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE := click

LOCAL_SRC_FILES := \
../../lib/string.cc \
../../lib/straccum.cc \
../../lib/nameinfo.cc \
../../lib/templatei.cc \
../../lib/bitvector.cc \
../../lib/bighashmap_arena.cc \
../../lib/vectorv.cc \
../../lib/hashallocator.cc \
../../lib/ipaddress.cc \
../../lib/ipflowid.cc \
../../lib/etheraddress.cc \
../../lib/packet.cc \
../../lib/error.cc \
../../lib/timestamp.cc \
../../lib/glue.cc \
../../lib/task.cc \
../../lib/timer.cc \
../../lib/atomic.cc \
../../lib/fromfile.cc \
../../lib/gaprate.cc \
../../lib/element.cc \
../../lib/confparse.cc \
../../lib/args.cc \
../../lib/variableenv.cc \
../../lib/lexer.cc \
../../lib/elemfilter.cc \
../../lib/routervisitor.cc \
../../lib/routerthread.cc \
../../lib/router.cc \
../../lib/master.cc \
../../lib/timerset.cc \
../../lib/selectset.cc \
../../lib/handlercall.cc \
../../lib/notifier.cc \
../../lib/integers.cc \
../../lib/md5.cc \
../../lib/crc32.c \
../../lib/in_cksum.c \
../../lib/iptable.cc \
../../lib/archive.cc \
../../lib/userutils.cc \
../../lib/driver.cc \
../../lib/xid.cc \
../../lib/xidpair.cc \
../../lib/xiapath.cc \
../../lib/xiaheader.cc \
../../lib/xiaextheader.cc \
../../lib/xiacontentheader.cc \
../../lib/xiatransportheader.cc \
../../lib/ip6address.cc \
../../lib/ip6flowid.cc \
../../lib/ip6table.cc \
../../elements/standard/addressinfo.cc \
../../elements/standard/alignmentinfo.cc \
../../elements/standard/errorelement.cc \
../../elements/standard/portinfo.cc \
../../elements/standard/scheduleinfo.cc \
../../elements/standard/xiaxidinfo.cc \
../../lib/clp.c \
../../elements/analysis/adjusttimestamp.cc \
../../elements/analysis/aggcounter.cc \
../../elements/analysis/aggpktcounter.cc \
../../elements/analysis/aggregatefilter.cc \
../../elements/analysis/aggregatefirst.cc \
../../elements/analysis/aggregateip.cc \
../../elements/analysis/aggregateipaddrpair.cc \
../../elements/analysis/aggregateipflows.cc \
../../elements/analysis/aggregatelast.cc \
../../elements/analysis/aggregatelen.cc \
../../elements/analysis/aggregatenotifier.cc \
../../elements/analysis/aggregatepaint.cc \
../../elements/analysis/anonipaddr.cc \
../../elements/analysis/eraseippayload.cc \
../../elements/analysis/fromcapdump.cc \
../../elements/analysis/fromdagdump.cc \
../../elements/analysis/fromipsumdump.cc \
../../elements/analysis/fromnetflowsumdump.cc \
../../elements/analysis/fromnlanrdump.cc \
../../elements/analysis/fromtcpdump.cc \
../../elements/analysis/ipsumdump_anno.cc \
../../elements/analysis/ipsumdump_icmp.cc \
../../elements/analysis/ipsumdump_link.cc \
../../elements/analysis/ipsumdump_ip.cc \
../../elements/analysis/ipsumdump_payload.cc \
../../elements/analysis/ipsumdump_tcp.cc \
../../elements/analysis/ipsumdump_udp.cc \
../../elements/analysis/ipsumdumpinfo.cc \
../../elements/analysis/settimestampdelta.cc \
../../elements/analysis/storetimestamp.cc \
../../elements/analysis/storeudptimeseqrecord.cc \
../../elements/analysis/timefilter.cc \
../../elements/analysis/timerange.cc \
../../elements/analysis/timesortedsched.cc \
../../elements/analysis/timestampaccum.cc \
../../elements/analysis/toipflowdumps.cc \
../../elements/analysis/toipsumdump.cc \
../../elements/app/ftpportmapper.cc \
../../elements/aqm/adaptivered.cc \
../../elements/aqm/red.cc \
../../elements/ethernet/arpfaker.cc \
../../elements/ethernet/arpprint.cc \
../../elements/ethernet/arpquerier.cc \
../../elements/ethernet/arpresponder.cc \
../../elements/ethernet/arptable.cc \
../../elements/ethernet/checkarpheader.cc \
../../elements/ethernet/ensureether.cc \
../../elements/ethernet/etherencap.cc \
../../elements/ethernet/ethermirror.cc \
../../elements/ethernet/etherpausesource.cc \
../../elements/ethernet/ethervlanencap.cc \
../../elements/ethernet/hostetherfilter.cc \
../../elements/ethernet/ip6ndadvertiser.cc \
../../elements/ethernet/ip6ndsolicitor.cc \
../../elements/ethernet/setvlananno.cc \
../../elements/ethernet/storeetheraddress.cc \
../../elements/ethernet/stripethervlanheader.cc \
../../elements/ethernet/vlandecap.cc \
../../elements/ethernet/vlanencap.cc \
../../elements/icmp/checkicmpheader.cc \
../../elements/icmp/icmperror.cc \
../../elements/icmp/icmpipencap.cc \
../../elements/icmp/icmppingencap.cc \
../../elements/icmp/icmppingresponder.cc \
../../elements/icmp/icmppingrewriter.cc \
../../elements/icmp/icmprewriter.cc \
../../elements/icmp/icmpsendpings.cc \
../../elements/ip/checkipheader.cc \
../../elements/ip/checkipheader2.cc \
../../elements/ip/decipttl.cc \
../../elements/ip/directiplookup.cc \
../../elements/ip/fixipsrc.cc \
../../elements/ip/getipaddress.cc \
../../elements/ip/ipaddrpairrewriter.cc \
../../elements/ip/ipaddrrewriter.cc \
../../elements/ip/ipclassifier.cc \
../../elements/ip/ipencap.cc \
../../elements/ip/ipfieldinfo.cc \
../../elements/ip/ipfilter.cc \
../../elements/ip/ipfragmenter.cc \
../../elements/ip/ipgwoptions.cc \
../../elements/ip/ipinputcombo.cc \
../../elements/ip/ipmirror.cc \
../../elements/ip/ipnameinfo.cc \
../../elements/ip/ipoutputcombo.cc \
../../elements/ip/ipprint.cc \
../../elements/ip/ipratemon.cc \
../../elements/ip/ipreassembler.cc \
../../elements/ip/iprewriterbase.cc \
../../elements/ip/iprwmapping.cc \
../../elements/ip/iproutetable.cc \
../../elements/ip/iprwpattern.cc \
../../elements/ip/iprwpatterns.cc \
../../elements/ip/lineariplookup.cc \
../../elements/ip/lookupiproute.cc \
../../elements/ip/lookupiproutelinux.cc \
../../elements/ip/markipce.cc \
../../elements/ip/markipheader.cc \
../../elements/ip/poundradixiplookup.cc \
../../elements/ip/radixiplookup.cc \
../../elements/ip/rangeiplookup.cc \
../../elements/ip/rfc2507c.cc \
../../elements/ip/rfc2507d.cc \
../../elements/ip/ripsend.cc \
../../elements/ip/rripmapper.cc \
../../elements/ip/setipaddress.cc \
../../elements/ip/setipchecksum.cc \
../../elements/ip/setipdscp.cc \
../../elements/ip/setipecn.cc \
../../elements/ip/setrandipaddress.cc \
../../elements/ip/siphmapper.cc \
../../elements/ip/sortediplookup.cc \
../../elements/ip/storeipaddress.cc \
../../elements/ip/stripipheader.cc \
../../elements/ip/truncateippayload.cc \
../../elements/ip/unstripipheader.cc \
../../elements/ip6/addresstranslator.cc \
../../elements/ip6/checkip6header.cc \
../../elements/ip6/decip6hlim.cc \
../../elements/ip6/getip6address.cc \
../../elements/ip6/icmp6error.cc \
../../elements/ip6/ip6encap.cc \
../../elements/ip6/ip6fragmenter.cc \
../../elements/ip6/ip6mirror.cc \
../../elements/ip6/ip6print.cc \
../../elements/ip6/ip6routetable.cc \
../../elements/ip6/lookupip6route.cc \
../../elements/ip6/markip6header.cc \
../../elements/ip6/protocoltranslator46.cc \
../../elements/ip6/protocoltranslator64.cc \
../../elements/ip6/setip6address.cc \
../../elements/ip6/setip6dscp.cc \
../../elements/ipsec/aes.cc \
../../elements/ipsec/des.cc  \
../../elements/ipsec/desp.cc \
../../elements/ipsec/esp.cc \
../../elements/ipsec/hmacsha1.cc \
../../elements/ipsec/ipsecencap.cc \
../../elements/ipsec/ipsecroutetable.cc \
../../elements/ipsec/radixipseclookup.cc \
../../elements/ipsec/satable.cc \
../../elements/ipsec/sha1.cc \
../../elements/simple/simpleidle.cc \
../../elements/simple/simplepriosched.cc \
../../elements/simple/simplerrsched.cc \
../../elements/simple/simplepullswitch.cc \
../../elements/standard/align.cc \
../../elements/standard/annotationinfo.cc \
../../elements/standard/averagecounter.cc \
../../elements/standard/bandwidthmeter.cc \
../../elements/standard/bandwidthshaper.cc \
../../elements/standard/block.cc \
../../elements/standard/burster.cc \
../../elements/standard/bwratedsplitter.cc \
../../elements/standard/bwratedunqueue.cc \
../../elements/standard/checkcrc32.cc \
../../elements/standard/checklength.cc \
../../elements/standard/checkpaint.cc \
../../elements/standard/classification.cc \
../../elements/standard/classifier.cc \
../../elements/standard/clickyinfo.cc \
../../elements/standard/compblock.cc \
../../elements/standard/counter.cc \
../../elements/standard/delayshaper.cc \
../../elements/standard/delayunqueue.cc \
../../elements/standard/devirtualizeinfo.cc \
../../elements/standard/discard.cc \
../../elements/standard/discardnofree.cc \
../../elements/standard/drivermanager.cc \
../../elements/standard/dropbroadcasts.cc \
../../elements/standard/drr.cc \
../../elements/standard/flowinfo.cc \
../../elements/standard/frontdropqueue.cc \
../../elements/standard/fullnotequeue.cc \
../../elements/standard/hashswitch.cc \
../../elements/standard/hub.cc \
../../elements/standard/idle.cc \
../../elements/standard/infinitesource.cc \
../../elements/standard/linkunqueue.cc \
../../elements/standard/markmacheader.cc \
../../elements/standard/messageelement.cc \
../../elements/standard/meter.cc \
../../elements/standard/mixedqueue.cc \
../../elements/standard/msqueue.cc \
../../elements/standard/notifierqueue.cc \
../../elements/standard/nullelement.cc \
../../elements/standard/nulls.cc \
../../elements/standard/paint.cc \
../../elements/standard/paintswitch.cc \
../../elements/standard/painttee.cc \
../../elements/standard/print.cc \
../../elements/standard/priosched.cc \
../../elements/standard/pullswitch.cc \
../../elements/standard/quicknotequeue.cc \
../../elements/standard/quitwatcher.cc \
../../elements/standard/randomerror.cc \
../../elements/standard/randomsample.cc \
../../elements/standard/randomsource.cc \
../../elements/standard/randomswitch.cc \
../../elements/standard/ratedsource.cc \
../../elements/standard/ratedsplitter.cc \
../../elements/standard/ratedunqueue.cc \
../../elements/standard/rrsched.cc \
../../elements/standard/rrswitch.cc \
../../elements/standard/script.cc \
../../elements/standard/setannobyte.cc \
../../elements/standard/setcrc32.cc \
../../elements/standard/setpackettype.cc \
../../elements/standard/settimestamp.cc \
../../elements/standard/shaper.cc \
../../elements/standard/simplequeue.cc \
../../elements/standard/staticpullswitch.cc \
../../elements/standard/staticswitch.cc \
../../elements/standard/storedata.cc \
../../elements/standard/stridesched.cc \
../../elements/standard/strideswitch.cc \
../../elements/standard/strip.cc \
../../elements/standard/striptonet.cc \
../../elements/standard/suppressor.cc \
../../elements/standard/switch.cc \
../../elements/standard/tee.cc \
../../elements/standard/threadsafequeue.cc \
../../elements/standard/timedsink.cc \
../../elements/standard/timedsource.cc \
../../elements/standard/timedunqueue.cc \
../../elements/standard/truncate.cc \
../../elements/standard/unqueue.cc \
../../elements/standard/unqueue2.cc \
../../elements/standard/unstrip.cc \
../../elements/tcpudp/checktcpheader.cc \
../../elements/tcpudp/checkudpheader.cc \
../../elements/tcpudp/dynudpipencap.cc \
../../elements/tcpudp/iprewriter.cc \
../../elements/tcpudp/settcpchecksum.cc \
../../elements/tcpudp/setudpchecksum.cc \
../../elements/tcpudp/tcpfragmenter.cc \
../../elements/tcpudp/tcpipsend.cc \
../../elements/tcpudp/tcprewriter.cc \
../../elements/tcpudp/udpip6encap.cc \
../../elements/tcpudp/udpipencap.cc \
../../elements/tcpudp/udprewriter.cc \
../../elements/test/bhmtest.cc \
../../elements/test/biginttest.cc \
../../elements/test/blockthread.cc \
../../elements/test/checkpacket.cc \
../../elements/test/clptest.cc \
../../elements/test/comparepackets.cc \
../../elements/test/confparsetest.cc \
../../elements/test/cryptotest.cc \
../../elements/test/errortest.cc \
../../elements/test/functiontest.cc \
../../elements/test/handlertask.cc \
../../elements/test/hashtabletest.cc \
../../elements/test/heaptest.cc \
../../elements/test/listtest.cc \
../../elements/test/neighborhoodtest.cc \
../../elements/test/notifierdebug.cc \
../../elements/test/nulltask.cc \
../../elements/test/packettest.cc \
../../elements/test/queueyanktest.cc \
../../elements/test/randomseed.cc \
../../elements/test/schedordertest.cc \
../../elements/test/sorttest.cc \
../../elements/test/timertest.cc \
../../elements/test/tokenbuckettest.cc \
../../elements/test/upstreamnotifier.cc \
../../elements/test/vectortest.cc \
../../elements/threads/balancedthreadsched.cc \
../../elements/threads/spinlockacquire.cc \
../../elements/test/queuethreadtest.cc \
../../elements/threads/spinlockinfo.cc \
../../elements/threads/spinlockrelease.cc \
../../elements/threads/staticthreadsched.cc \
../../elements/userlevel/changeuid.cc \
../../elements/userlevel/chattersocket.cc \
../../elements/userlevel/controlsocket.cc \
../../elements/userlevel/fakepcap.cc \
../../elements/userlevel/fromdevice.cc \
../../elements/userlevel/fromdump.cc \
../../elements/userlevel/fromhost.cc \
../../elements/userlevel/fromrawsocket.cc \
../../elements/userlevel/fromsocket.cc \
../../elements/userlevel/handlerproxy.cc \
../../elements/userlevel/kernelfilter.cc \
../../elements/userlevel/kerneltap.cc \
../../elements/userlevel/kerneltun.cc \
../../elements/userlevel/khandlerproxy.cc \
../../elements/userlevel/progressbar.cc \
../../elements/userlevel/rawsocket.cc \
../../elements/userlevel/socket.cc \
../../elements/userlevel/todevice.cc \
../../elements/userlevel/todump.cc \
../../elements/userlevel/tohost.cc \
../../elements/userlevel/torawsocket.cc \
../../elements/userlevel/tosocket.cc \
../../elements/userlevel/umlswitch.cc \
../../elements/xia/clone.cc \
../../elements/xia/ip6randomize.cc \
../../elements/xia/iprandomize.cc \
../../elements/xia/markxiaheader.cc \
../../elements/xia/printstats.cc \
../../elements/xia/xarpquerier.cc \
../../elements/xia/xarpresponder.cc \
../../elements/xia/xarptable.cc \
../../elements/xia/xcmp.cc \
../../elements/xia/xcmppingconverter.cc \
../../elements/xia/xcmppingsource.cc \
../../elements/xia/xiacache.cc \
../../elements/xia/xiacheckdest.cc \
../../elements/xia/xiacontentmodule.cc \
../../elements/xia/xiadechlim.cc \
../../elements/xia/xiadhcpserver.cc \
../../elements/xia/xiaencap.cc \
../../elements/xia/xiafastpath.cc \
../../elements/xia/xiainserthash.cc \
../../elements/xia/xiaipencap.cc \
../../elements/xia/xianexthop.cc \
../../elements/xia/xiapaint.cc \
../../elements/xia/xiapaintswitch.cc \
../../elements/xia/xiapingresponder.cc \
../../elements/xia/xiapingsource.cc \
../../elements/xia/xiapingupdate.cc \
../../elements/xia/xiaprint.cc \
../../elements/xia/xiarandomize.cc \
../../elements/xia/xiarpc.cc \
../../elements/xia/xiaselectpath.cc \
../../elements/xia/xiatransport.cc \
../../elements/xia/xiaxidroutetable.cc \
../../elements/xia/xiaxidtypeclassifier.cc \
../../elements/xia/xiaxidtypecounter.cc \
../../elements/xia/xroute.cc \
../../elements/xia/xtransport.cc \
../../userlevel/elements.cc \
../../../api/xsocket/xia.pb.cc \
../../userlevel/click.cc

LOCAL_CPP_EXTENSION := .c .cc

LOCAL_C_INCLUDES += api/include api/xsocket/ api/xsocket/minini android-deps/protobuf/src click/include click/tools/lib

LOCAL_SHARED_LIBRARIES := \
libdagaddr libprotobuf libz libcutils libutils libpthread
LOCAL_LDLIBS := -lz -lm

LOCAL_CPPFLAGS := -D__MTCLICK__ -DHAVE_IFADDRS_H=0 -DCLICK_ANDROID -DCLICK_USERLEVEL -DHAVE_USER_MULTITHREAD -Wno-psabi -L$(LOCAL_PATH)/src/google/protobuf/io_engine/lib -frtti 
LOCAL_CFLAGS := -Wno-psabi -DHAVE_IFADDRS_H=0 -DCLICK_ANDROID -DCLICK_USERLEVEL -DHAVE_USER_MULTITHREAD -Wno-psabi -L$(LOCAL_PATH)/src/google/protobuf/io_engine/lib -frtti
LOCAL_CXX_FLAGS := -Wno-psabi -DHAVE_IFADDRS_H=0 -DCLICK_ANDROID -DCLICK_USERLEVEL -DHAVE_USER_MULTITHREAD -Wno-psabi -L$(LOCAL_PATH)/src/google/protobuf/io_engine/lib -frtti

include $(BUILD_EXECUTABLE)
